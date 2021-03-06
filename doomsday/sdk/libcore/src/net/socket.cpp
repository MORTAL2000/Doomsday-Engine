/*
 * The Doomsday Engine Project -- libcore
 *
 * Copyright © 2004-2017 Jaakko Keränen <jaakko.keranen@iki.fi>
 *
 * @par License
 * LGPL: http://www.gnu.org/licenses/lgpl.html
 *
 * <small>This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
 * General Public License for more details. You should have received a copy of
 * the GNU Lesser General Public License along with this program; if not, see:
 * http://www.gnu.org/licenses</small>
 */

/**
 * @page sysNetwork Low-Level Networking
 *
 * On server-side connected clients can be either in "unjoined" mode or
 * "joined" mode. The former is for querying information about the server's
 * status, while the latter one is for clients participating in the on-going
 * game.
 *
 * In unjoined mode, messages are not compressed.
 * @todo Document uncompressed message format.
 *
 * @section netProtocol Network Protocol
 *
 * In joined mode, the network protocol works as follows. All messages are sent
 * over a TCP socket. Every message consists of a header and the message payload.
 * The content of these depends on the compressed message size.
 *
 * @par 1&ndash;127 bytes
 * Very small messages, such as the position updates that a client streams
 * to the server, are encoded with Huffman codes (see huffman.h). If
 * the Huffman coded payload happens to exceed 127 bytes, the message is
 * switched to the medium format (see below). Message structure:
 * - 1 byte: payload size
 * - @em n bytes: payload contents (Huffman)
 *
 * @par 128&ndash;4095 bytes
 * Medium-sized messages are compressed either using a fast zlib deflate level,
 * or Huffman codes if it yields better compression.
 * If the deflated message size exceeds 4095 bytes, the message is switched to
 * the large format (see below). Message structure:
 * - 1 byte: 0x80 | (payload size & 0x7f)
 * - 1 byte: (payload size >> 7) | (0x40 for deflated, otherwise Huffman)
 * - @em n bytes: payload contents (as produced by ZipFile::compressAtLevel()).
 *
 * @par >= 4096 bytes (up to 4MB)
 * Large messages are compressed using the best zlib deflate level.
 * Message structure:
 * - 1 byte: 0x80 | (payload size & 0x7f)
 * - 1 byte: 0x80 | (payload size >> 7) & 0x7f
 * - 1 byte: payload size >> 14
 * - @em n bytes: payload contents (as produced by ZipFile::compressAtLevel()).
 *
 * Messages larger than or equal to 2^22 bytes (about 4MB) must be broken into
 * smaller pieces before sending.
 *
 * @see Protocol_Send()
 * @see Protocol_Receive()
 */

#include "de/Socket"
#include "de/Async"
#include "de/Message"
#include "de/Writer"
#include "de/Reader"
#include "de/data/huffman.h"

#include <QThread>

namespace de {

struct Counters
{
    duint64 sentUncompressedBytes = 0;
    duint64 sentBytes = 0;
    duint64 sentPeriodBytes = 0;
    double outputBytesPerSecond = 0;
    Time periodStartedAt;
};
static LockableT<Counters> counters;
static TimeSpan const sendPeriodDuration = 5;

/// Maximum number of channels.
static duint const MAX_CHANNELS = 2;

static int const MAX_SIZE_SMALL  = 127; // bytes
static int const MAX_SIZE_MEDIUM = 4095; // bytes
static int const MAX_SIZE_BIG    = 10*MAX_SIZE_MEDIUM;
static int const MAX_SIZE_LARGE  = DENG2_SOCKET_MAX_PAYLOAD_SIZE;

/// Threshold for input data size: messages smaller than this are first compressed
/// with Doomsday's Huffman codes. If the result is smaller than the deflated data,
/// the Huffman coded payload is used (unless it doesn't fit in a medium-sized packet).
static int const MAX_HUFFMAN_INPUT_SIZE = 4096; // bytes

#define TRMF_CONTINUE           0x80
#define TRMF_DEFLATED           0x40
#define TRMF_SIZE_MASK          0x7f
#define TRMF_SIZE_MASK_MEDIUM   0x3f
#define TRMF_SIZE_SHIFT         7

namespace internal {

/**
 * Network message header.
 */
struct MessageHeader : public ISerializable
{
    int size;
    bool isHuffmanCoded;
    bool isDeflated;
    duint channel; /// @todo include in the written header

    MessageHeader() : size(0), isHuffmanCoded(false), isDeflated(false), channel(0)
    {}

    void operator >> (Writer &writer) const
    {
        if (size <= MAX_SIZE_SMALL && !isDeflated)
        {
            writer << dbyte(size);
        }
        else if (size <= MAX_SIZE_MEDIUM)
        {
            writer << dbyte(TRMF_CONTINUE | (size & TRMF_SIZE_MASK));
            writer << dbyte((isDeflated? TRMF_DEFLATED : 0) | (size >> TRMF_SIZE_SHIFT));
        }
        else if (size <= MAX_SIZE_LARGE)
        {
            DENG2_ASSERT(isDeflated);

            writer << dbyte(TRMF_CONTINUE | (size & TRMF_SIZE_MASK));
            writer << dbyte(TRMF_CONTINUE | ((size >> TRMF_SIZE_SHIFT) & TRMF_SIZE_MASK));
            writer << dbyte(size >> (2*TRMF_SIZE_SHIFT));
        }
        else
        {
            // Not supported.
            DENG2_ASSERT(false);
        }
    }

    /**
    * Throws an exception if the header is malformed/incomplete.
    */
    void operator << (Reader &reader)
    {
        // Start reading the header.
        dbyte b;
        reader >> b;

        size = b & TRMF_SIZE_MASK;

        isDeflated = false;
        isHuffmanCoded = true;

        if (b & TRMF_CONTINUE) // More follows...
        {
            reader >> b;

            if (b & TRMF_CONTINUE) // Yet more to come...
            {
                // Large header.
                isDeflated = true;
                isHuffmanCoded = false;
                size |= ((b & TRMF_SIZE_MASK) << TRMF_SIZE_SHIFT);

                reader >> b;

                size |= (b << (2*TRMF_SIZE_SHIFT));
            }
            else
            {
                // Medium header.
                if (b & TRMF_DEFLATED)
                {
                    isDeflated = true;
                    isHuffmanCoded = false;
                }
                size |= ((b & TRMF_SIZE_MASK_MEDIUM) << TRMF_SIZE_SHIFT);
            }
        }
    }
};

} // namespace internal

using namespace internal;

DENG2_PIMPL_NOREF(Socket)
{
    Address peer;
    bool quiet = false;
    bool retainOrder = true;

    enum ReceptionState { ReceivingHeader, ReceivingPayload };
    ReceptionState receptionState = ReceivingHeader;
    Block receivedBytes;
    MessageHeader incomingHeader;

    /// Number of the active channel.
    /// @todo Channel is not used at the moment.
    duint activeChannel = 0;

    /// Pointer to the internal socket data.
    QTcpSocket *socket = nullptr;

    /// Buffer for incoming received messages.
    QList<Message *> receivedMessages;

    /// Number of bytes waiting to be written to the socket.
    dint64 bytesToBeWritten = 0;

    /// Number of bytes written to the socket so far.
    dint64 totalBytesWritten = 0;

    ~Impl()
    {
        // Delete received messages left in the buffer.
        foreach (Message *msg, receivedMessages) delete msg;
    }

    void serializeMessage(MessageHeader &header, Block &payload)
    {
        Block huffData;

        // Let's find the appropriate compression method of the payload. First see
        // if the encoded contents are under 128 bytes as Huffman codes.
        if (payload.size() <= MAX_HUFFMAN_INPUT_SIZE) // Potentially short enough.
        {
            huffData = codec::huffmanEncode(payload);
            if (int(huffData.size()) <= MAX_SIZE_SMALL)
            {
                // We'll use this.
                header.isHuffmanCoded = true;
                header.size = huffData.size();
                payload = huffData;
            }
            // Even if that didn't seem suitable, we'll keep it to compare against
            // the deflated payload.
        }

        /// @todo Messages broadcasted to multiple recipients are separately
        /// compressed for each TCP send -- should do only one compression per
        /// message.

        if (!header.size) // Try deflate.
        {
            int const level = 1; //(payload.size() < MAX_SIZE_BIG? 1 /*fast*/ : 9 /*best*/);
            Block const deflated = payload.compressed(level);

            if (!deflated.size())
            {
                throw ProtocolError("Socket::send:", "Failed to deflate message payload");
            }
            if (deflated.size() > MAX_SIZE_LARGE)
            {
                throw ProtocolError("Socket::send",
                                    QString("Compressed payload is too large (%1 bytes)").arg(deflated.size()));
            }

            // Choose the smallest compression.
            if (huffData.size() && huffData.size() <= deflated.size() && int(huffData.size()) <= MAX_SIZE_MEDIUM)
            {
                // Huffman yielded smaller payload.
                header.isHuffmanCoded = true;
                header.size = huffData.size();
                payload = huffData;
            }
            else
            {
                // Use the deflated payload.
                header.isDeflated = true;
                header.size = deflated.size();
                payload = deflated;
            }
        }
    }

    void sendMessage(MessageHeader const &header,
                     Block const &payload)
    {
        DENG2_ASSERT(socket != nullptr);
        DENG2_ASSERT(QThread::currentThread() == socket->thread());

        // Write the message header.
        Block dest;
        Writer(dest) << header;
        socket->write(dest);

        // Update totals (for statistics).
        dsize const total = dest.size() + payload.size();
        bytesToBeWritten  += total;
        totalBytesWritten += total;

        socket->write(payload);

        // Update total counters, too.
        {
            DENG2_GUARD(counters);
            counters.value.sentPeriodBytes += total;
            counters.value.sentBytes       += total;
            // Update Bps counter.
            if (!counters.value.periodStartedAt.isValid()
                || counters.value.periodStartedAt.since() > sendPeriodDuration)
            {
                counters.value.outputBytesPerSecond = double(counters.value.sentPeriodBytes)
                                                    / sendPeriodDuration;
                counters.value.sentPeriodBytes = 0;
                counters.value.periodStartedAt = Time::currentHighPerformanceTime();
            }
        }
    }

    void serializeAndSendMessage(IByteArray const &packet)
    {
        Block payload = packet;
        {
            DENG2_GUARD(counters);
            counters.value.sentUncompressedBytes += payload.size();
        }

        if (!retainOrder && packet.size() >= MAX_SIZE_BIG)
        {
            async([this, payload] ()
            {
                // Prepare for sending in a background thread, since it may take a moment.
                MessageHeader header;
                Block msgData = payload;
                serializeMessage(header, msgData);
                return std::make_pair(header, msgData);
            },
            [this] (std::pair<MessageHeader, Block> msg)
            {
                if (socket)
                {
                    // Write to socket in main thread.
                    sendMessage(msg.first, msg.second);
                }
            });
        }
        else
        {
            MessageHeader header;
            serializeMessage(header, payload);
            sendMessage(header, payload);
        }
    }

    /**
     * Checks the incoming bytes and sees if any messages can be formed.
     */
    void deserializeMessages()
    {
        forever
        {
            if (receptionState == ReceivingHeader)
            {
                if (receivedBytes.size() < 2)
                {
                    // A message must be at least two bytes long (header + payload).
                    return;
                }
                try
                {
                    Reader reader(receivedBytes);
                    reader >> incomingHeader;
                    receptionState = ReceivingPayload;

                    // Remove the read bytes from the buffer.
                    receivedBytes.remove(0, reader.offset());
                }
                catch (de::Error const &)
                {
                    // It seems we don't have a full header yet.
                    return;
                }
            }

            if (receptionState == ReceivingPayload)
            {
                if (int(receivedBytes.size()) >= incomingHeader.size)
                {
                    // Extract the payload from the incoming buffer.
                    Block payload = receivedBytes.left(incomingHeader.size);
                    receivedBytes.remove(0, incomingHeader.size);

                    // We have the full payload, but it still may need to uncompressed.
                    if (incomingHeader.isHuffmanCoded)
                    {
                        payload = codec::huffmanDecode(payload);
                        if (!payload.size())
                        {
                            throw ProtocolError("Socket::Impl::deserializeMessages", "Huffman decoding failed");
                        }
                    }
                    else if (incomingHeader.isDeflated)
                    {
                        payload = qUncompress(payload);
                        if (!payload.size())
                        {
                            throw ProtocolError("Socket::Impl::deserializeMessages", "Deflate failed");
                        }
                    }

                    receivedMessages << new Message(Address(socket->peerAddress(), socket->peerPort()),
                                                    incomingHeader.channel, payload);

                    // We can proceed to the next message.
                    receptionState = ReceivingHeader;
                    incomingHeader = MessageHeader();
                }
                else
                {
                    // Let's wait until more is available.
                    return;
                }
            }
        }
    }
};

Socket::Socket() : d(new Impl)
{
    d->socket = new QTcpSocket;
    initialize();

    QObject::connect(d->socket, SIGNAL(connected()), this, SIGNAL(connected()));
}

Socket::Socket(Address const &address, TimeSpan const &timeOut) : d(new Impl) // blocking
{
    LOG_AS("Socket");

    d->socket = new QTcpSocket;
    initialize();

    // Now that the signals have been set...
    d->socket->connectToHost(address.host(), address.port());
    if (!d->socket->waitForConnected(int(timeOut.asMilliSeconds())))
    {
        QString msg = d->socket->errorString();
        delete d->socket;
        d.reset();

        // Timed out!
        /// @throw ConnectionError Connection did not open in time.
        throw ConnectionError("Socket", "Opening the connection to " + address.asText() + " failed: " + msg);
    }

    LOG_NET_NOTE("Connection opened to %s") << address.asText();

    d->peer = address;

    DENG2_ASSERT(d->socket->isOpen() && d->socket->isWritable() &&
                 d->socket->state() == QAbstractSocket::ConnectedState);
}

void Socket::open(Address const &address) // non-blocking
{
    DENG2_ASSERT(d->socket);
    DENG2_ASSERT(d->socket->state() == QAbstractSocket::UnconnectedState);

    LOG_AS("Socket");
    if (!d->quiet) LOG_NET_MSG("Opening connection to %s") << address.asText();

    d->socket->connectToHost(address.host(), address.port());
    d->peer = address;
}

void Socket::open(String const &domainNameWithOptionalPort,
                  duint16 defaultPort) // non-blocking
{
    String str = domainNameWithOptionalPort;
    duint16 port = defaultPort;
    if (str.contains(':'))
    {
        int pos = str.lastIndexOf(':');
        port = duint16(str.mid(pos + 1).toInt());
        if (!port) port = defaultPort;
        str = str.left(pos);
    }
    if (str == "localhost")
    {
        open(Address(str.toLatin1(), port));
        return;
    }

    QHostAddress host(str);
    if (!host.isNull())
    {
        // Looks like a regular IP address.
        open(Address(str.toLatin1(), port));
        return;
    }

    d->peer.setPort(port);

    // Looks like we will need to look this up.
    QHostInfo::lookupHost(str, this, SLOT(hostResolved(QHostInfo)));
}

void Socket::reconnect()
{
    DENG2_ASSERT(!isOpen());

    open(d->peer);
}

Socket::Socket(QTcpSocket *existingSocket) : d(new Impl)
{
    d->socket = existingSocket;
    initialize();

    // Maybe we missed an earlier signal, since we are only now getting ownership.
    readIncomingBytes();
}

Socket::~Socket()
{
    close();
    delete d->socket;
}

void Socket::socketDestroyed()
{
    // The socket is gone...
    d->socket = 0;
}

void Socket::initialize()
{
    // Options.
    d->socket->setSocketOption(QTcpSocket::LowDelayOption, 1); // prefer short buffering

    QObject::connect(d->socket, SIGNAL(bytesWritten(qint64)), this, SLOT(bytesWereWritten(qint64)));
    QObject::connect(d->socket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));
    QObject::connect(d->socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(socketError(QAbstractSocket::SocketError)), Qt::DirectConnection);
    QObject::connect(d->socket, SIGNAL(readyRead()), this, SLOT(readIncomingBytes()));
    QObject::connect(d->socket, SIGNAL(destroyed()), this, SLOT(socketDestroyed()));

    /*QObject::connect(d->socket, &QAbstractSocket::stateChanged, [] (QAbstractSocket::SocketState state)
    {
        qDebug() << "socket state:" << state;
    });*/
}

void Socket::close()
{
    if (!d->socket) return;

    if (d->socket->state() == QAbstractSocket::ConnectedState)
    {
        // All pending data will be written to the socket before closing.
        d->socket->disconnectFromHost();
    }
    else
    {
        d->socket->abort();
    }

    if (d->socket->state() != QAbstractSocket::UnconnectedState)
    {
        // Make sure the socket is disconnected before the return.
        d->socket->waitForDisconnected();
    }

    d->socket->close();
}

void Socket::setQuiet(bool noLogOutput)
{
    d->quiet = noLogOutput;
}

void Socket::resetCounters()
{
    DENG2_GUARD(counters);
    counters.value = Counters();
}

duint64 Socket::sentUncompressedBytes()
{
    DENG2_GUARD(counters);
    return counters.value.sentUncompressedBytes;
}

duint64 Socket::sentBytes()
{
    DENG2_GUARD(counters);
    return counters.value.sentBytes;
}

double Socket::outputBytesPerSecond()
{
    DENG2_GUARD(counters);
    return counters.value.outputBytesPerSecond;
}

duint Socket::channel() const
{
    return d->activeChannel;
}

void Socket::setChannel(duint number)
{
    DENG2_ASSERT(number < MAX_CHANNELS);
    d->activeChannel = min(number, MAX_CHANNELS - 1);
}

void Socket::setRetainOrder(bool retainOrder)
{
    d->retainOrder = retainOrder;
}

void Socket::send(IByteArray const &packet)
{
    send(packet, d->activeChannel);
}

Socket &Socket::operator << (IByteArray const &packet)
{
    send(packet, d->activeChannel);
    return *this;
}

void Socket::send(IByteArray const &packet, duint /*channel*/)
{
    if (!d->socket)
    {
        /// @throw DisconnectedError Sending is not possible because the socket has been closed.
        throw DisconnectedError("Socket::send", "Socket is unavailable");
    }

    // Sockets must be used only in their own thread.
    DENG2_ASSERT(thread() == QThread::currentThread());

    d->serializeAndSendMessage(packet);
}

void Socket::readIncomingBytes()
{
    if (!d->socket) return;

    auto available = d->socket->bytesAvailable();
    if (available > 0)
    {
        d->receivedBytes += d->socket->read(d->socket->bytesAvailable());
    }

    d->deserializeMessages();

    // Notification about available messages.
    if (!d->receivedMessages.isEmpty())
    {
        emit messagesReady();
    }
}

void Socket::hostResolved(QHostInfo const &info)
{
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty())
    {
        LOG_NET_ERROR("Could not resolve host: ") << info.errorString();
        emit disconnected();
    }
    else
    {
        // Now we know where to connect.
        open(Address(info.addresses().first(), d->peer.port()));

        emit addressResolved();
    }
}

Message *Socket::receive()
{
    if (d->receivedMessages.isEmpty())
    {
        return nullptr;
    }
    return d->receivedMessages.takeFirst();
}

Message *Socket::peek()
{
    if (d->receivedMessages.isEmpty())
    {
        return nullptr;
    }
    return d->receivedMessages.first();
}

void Socket::flush()
{
    if (!d->socket) return;

    // Wait until data has been written.
    d->socket->flush();
    d->socket->waitForBytesWritten();
}

Address Socket::peerAddress() const
{
    if (isOpen() && d->socket->state() == QTcpSocket::ConnectedState)
    {
        return Address(d->socket->peerAddress(), d->socket->peerPort());
    }
    return d->peer;
}

bool Socket::isOpen() const
{
    return d->socket && d->socket->state() != QTcpSocket::UnconnectedState;
}

bool Socket::isLocal() const
{
    return peerAddress().isLocal();
}

void Socket::socketDisconnected()
{
    emit disconnected();
}

void Socket::socketError(QAbstractSocket::SocketError socketError)
{
    if (socketError != QAbstractSocket::SocketTimeoutError)
    {
        LOG_AS("Socket");
        if (!d->quiet) LOG_NET_WARNING(d->socket->errorString());

        emit error(d->socket->errorString());
        emit disconnected();
    }
}

bool Socket::hasIncoming() const
{
    return !d->receivedMessages.empty();
}

dsize Socket::bytesBuffered() const
{
    return dsize(d->bytesToBeWritten);
}

void Socket::bytesWereWritten(qint64 bytes)
{
    d->bytesToBeWritten -= bytes;
    DENG2_ASSERT(d->bytesToBeWritten >= 0);

    if (d->bytesToBeWritten == 0)
    {
        emit allSent();
    }
}

} // namespace de
