/*
 * The Doomsday Engine Project
 *
 * Copyright © 2010-2017 Jaakko Keränen <jaakko.keranen@iki.fi>
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

#ifndef LIBDENG2_IDENTIFIEDPACKET_H
#define LIBDENG2_IDENTIFIEDPACKET_H

#include "../libcore.h"
#include "../Packet"

namespace de {

/**
 * Network packet that is identified with a unique identifier.
 *
 * @ingroup net
 */
class DENG2_PUBLIC IdentifiedPacket : public Packet
{
public:
    typedef duint64 Id;

public:
    /**
     * Constructs a new identified packet.
     *
     * @param type  Type of the packet.
     * @param i     Identifier. If zero, a new identifier is generated.
     */
    IdentifiedPacket(Type const &type, Id i = 0);

    void setId(Id id);

    /// Returns the id of the packet.
    Id id() const;

    // Implements ISerializable.
    void operator >> (Writer &to) const;
    void operator << (Reader &from);

private:
    mutable Id _id;
};

} // namespace de

#endif // LIBDENG2_IDENTIFIEDPACKET_H
