/** @file font.cpp  Font with metrics.
 *
 * @authors Copyright (c) 2013-2017 Jaakko Keränen <jaakko.keranen@iki.fi>
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

#include "de/Font"

#include <de/ConstantRule>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QThreadStorage>

//#if (defined(MACOSX) && defined(MACOS_10_7)) || defined (DENG_IOS)
//#  include "../src/text/coretextnativefont_macx.h"
//namespace de { typedef CoreTextNativeFont PlatformFont; }
//#else
#  include "../src/text/qtnativefont.h"
namespace de { typedef QtNativeFont PlatformFont; }
//#endif

namespace de {

namespace internal
{
    struct FontParams
    {
        String family;
        float size;
        NativeFont::Spec spec;

        FontParams() {}

        FontParams(PlatformFont const &font)
        {
            family         = font.family();
            size           = font.size();
            spec.weight    = font.weight();
            spec.style     = font.style();
            spec.transform = font.transform();
        }

        bool operator==(FontParams const &other) const
        {
            return fequal(size, other.size)
                && spec == other.spec
                && family == other.family;
        }
    };

    static uint qHash(FontParams const &params)
    {
        return ::qHash(params.family)
             ^ ::qHash(int(100 * params.size))
             ^ ::qHash(params.spec.weight)
             ^ ::qHash(int(params.spec.style))
             ^ uint(params.spec.transform);
    }
}

namespace internal
{
    struct ThreadFonts
    {
        PlatformFont font;
        QHash<internal::FontParams, PlatformFont *> fontMods;

        ~ThreadFonts() {
            qDeleteAll(fontMods);
        }
    };
}

/**
 * Each thread uses its own independent set of native font instances. This allows
 * background threads to freely measure and render text using the native font
 * instances without any synchronization. Also note that these background threads are
 * pooled, so there is only a fixed total number of threads accessing these objects.
 */
static QThreadStorage<QHash<Font *, internal::ThreadFonts>> fontsForThread;

DENG2_PIMPL(Font)
{
    QFont referenceFont;
    internal::ThreadFonts *threadFonts = nullptr;

    ConstantRule *heightRule;
    ConstantRule *ascentRule;
    ConstantRule *descentRule;
    ConstantRule *lineSpacingRule;
    int ascent = 0;

    Impl(Public *i) : Base(i)
    {
        createRules();
    }

    Impl(Public *i, QFont const &qfont) : Base(i), referenceFont(qfont)
    {
#if 0
        // Development aid: list all available fonts and styles.
        QFontDatabase db;
        foreach (QString fam, db.families())
        {
            qDebug() << "FONT FAMILY:" << fam;
            qDebug() << "\tStyles:" << db.styles(fam);
        }
#endif
        createRules();
        updateMetrics();
    }

    ~Impl()
    {
        fontsForThread.localData().remove(thisPublic); // FIXME: not removed by all threads...

        releaseRef(heightRule);
        releaseRef(ascentRule);
        releaseRef(descentRule);
        releaseRef(lineSpacingRule);
    }

    void createRules()
    {
        heightRule      = new ConstantRule(0);
        ascentRule      = new ConstantRule(0);
        descentRule     = new ConstantRule(0);
        lineSpacingRule = new ConstantRule(0);
    }

    /**
     * Initializes the current thread's platform fonts for this Font.
     */
    internal::ThreadFonts &getThreadFonts()
    {
        auto &hash = fontsForThread.localData();
        auto found = hash.find(thisPublic);
        if (found != hash.end())
        {
            auto &tf = found.value();
            if (fequal(tf.font.size(), float(referenceFont.pointSizeF())))
            {
                return tf;
            }
            // Size has changed, re-initialize the font.
            qDeleteAll(tf.fontMods);
            tf.fontMods.clear();
        }
        hash[thisPublic].font = PlatformFont(referenceFont);
        return hash[thisPublic];
    }

    void updateMetrics()
    {
        auto &plat = getThreadFonts();

        ascent = plat.font.ascent();
        if (plat.font.weight() != NativeFont::Normal)
        {
            // Use the ascent of the normal weight for non-normal weights;
            // we need to align content to baseline regardless of weight.
            PlatformFont normalized(plat.font);
            normalized.setWeight(NativeFont::Normal);
            ascent = normalized.ascent();
        }

        ascentRule     ->set(ascent);
        descentRule    ->set(plat.font.descent());
        heightRule     ->set(plat.font.height());
        lineSpacingRule->set(plat.font.lineSpacing());
    }

    PlatformFont &getFontMod(internal::FontParams const &params)
    {
        auto &plat = getThreadFonts();

        auto found = plat.fontMods.constFind(params);
        if (found != plat.fontMods.constEnd())
        {
            return *found.value();
        }

        auto *mod = new PlatformFont;
        mod->setFamily(params.family);
        mod->setSize(params.size);
        mod->setStyle(params.spec.style);
        mod->setWeight(params.spec.weight);
        mod->setTransform(params.spec.transform);
        plat.fontMods.insert(params, mod);
        return *mod;
    }

    /**
     * Produces a font based on this one but with the attribute modifications applied
     * from a rich format range.
     *
     * @param rich  Rich formatting.
     *
     * @return  Font with applied formatting.
     */
    PlatformFont const &alteredFont(RichFormat::Iterator const &rich)
    {
        auto &plat = getThreadFonts();

        if (!rich.isDefault())
        {
            internal::FontParams modParams(plat.font);

            // Size change.
            if (!fequal(rich.sizeFactor(), 1.f))
            {
                modParams.size *= rich.sizeFactor();
            }

            // Style change (including monospace).
            switch (rich.style())
            {
            case RichFormat::OriginalStyle:
                break;

            case RichFormat::Regular:
                modParams.family = plat.font.family();
                modParams.spec.style = NativeFont::Regular;
                break;

            case RichFormat::Italic:
                modParams.family = plat.font.family();
                modParams.spec.style = NativeFont::Italic;
                break;

            case RichFormat::Monospace:
                if (rich.format.format().hasStyle())
                {
                    if (Font const *altFont = rich.format.format().style().richStyleFont(rich.style()))
                    {
                        modParams = internal::FontParams(altFont->d->getThreadFonts().font);
                    }
                }
                break;
            }

            // Weight change.
            if (rich.weight() != RichFormat::OriginalWeight)
            {
                modParams.spec.weight = (rich.weight() == RichFormat::Normal? NativeFont::Normal :
                                         rich.weight() == RichFormat::Bold?   NativeFont::Bold   :
                                                                              NativeFont::Light);
            }
            return getFontMod(modParams);
        }

        // No alterations applied.
        return plat.font;
    }
};

Font::Font() : d(new Impl(this))
{}

Font::Font(Font const &other) : d(new Impl(this, other.d->referenceFont))
{}

Font::Font(QFont const &font) : d(new Impl(this, font))
{}

void Font::initialize(const QFont &font)
{
    d->referenceFont = font;
    d->updateMetrics();
}

Rectanglei Font::measure(String const &textLine) const
{
    return measure(textLine, RichFormat::fromPlainText(textLine));
}

Rectanglei Font::measure(String const &textLine, RichFormatRef const &format) const
{
    Rectanglei bounds;
    int advance = 0;

    RichFormat::Iterator iter(format);
    while (iter.hasNext())
    {
        iter.next();
        if (iter.range().isEmpty()) continue;

        PlatformFont const &altFont = d->alteredFont(iter);

        String const part = textLine.substr(iter.range());
        Rectanglei rect = altFont.measure(part);

        // Combine to the total bounds.
        rect.moveTopLeft(Vector2i(advance, rect.top()));
        bounds |= rect;

        advance += altFont.width(part);
    }

    return bounds;
}

int Font::advanceWidth(String const &textLine) const
{
    return advanceWidth(textLine, RichFormat::fromPlainText(textLine));
}

int Font::advanceWidth(String const &textLine, RichFormatRef const &format) const
{
    int advance = 0;
    RichFormat::Iterator iter(format);
    while (iter.hasNext())
    {
        iter.next();
        if (iter.range().isEmpty()) continue;

        advance += d->alteredFont(iter).width(textLine.substr(iter.range()));
    }
    return advance;
}

QImage Font::rasterize(String const &textLine,
                       Vector4ub const &foreground,
                       Vector4ub const &background) const
{
    return rasterize(textLine, RichFormat::fromPlainText(textLine), foreground, background);
}

QImage Font::rasterize(String const &textLine,
                       RichFormatRef const &format,
                       Vector4ub const &foreground,
                       Vector4ub const &background) const
{
    if (textLine.isEmpty())
    {
        return QImage();
    }

    auto const &plat = d->getThreadFonts();

#ifdef LIBGUI_ACCURATE_TEXT_BOUNDS
    Rectanglei const bounds = measure(textLine, format);
#else
    Rectanglei const bounds(0, 0,
                            advanceWidth(textLine, format),
                            plat.font.height());
#endif

    QColor bgColor(background.x, background.y, background.z, background.w);

    Vector4ub fg = foreground;
    Vector4ub bg = background;

    QImage img(QSize(bounds.width(),
                     de::max(duint(plat.font.height()), bounds.height())),
               QImage::Format_ARGB32);
    img.fill(bgColor.rgba());

    QPainter painter(&img);
    painter.setCompositionMode(QPainter::CompositionMode_Source);

    // Composite the final image by drawing each rich range first into a separate
    // bitmap and then drawing those into the final image.
    int advance = 0;
    RichFormat::Iterator iter(format);
    while (iter.hasNext())
    {
        iter.next();
        if (iter.range().isEmpty()) continue;

        PlatformFont const *font = &plat.font;

        if (iter.isDefault())
        {
            fg = foreground;
            bg = background;
        }
        else
        {
            font = &d->alteredFont(iter);

            if (iter.colorIndex() != RichFormat::OriginalColor)
            {
                fg = iter.color();
                bg = Vector4ub(fg, 0);
            }
            else
            {
                fg = foreground;
                bg = background;
            }
        }

        String const part = textLine.substr(iter.range());

#ifdef WIN32
        // Kludge: No light-weight fonts available, so reduce opacity to give the
        // illusion of thinness.
        if (iter.weight() == RichFormat::Light)
        {
            if (Vector3ub(60, 60, 60) > fg) // dark
                fg.w *= .66f;
            else if (Vector3ub(230, 230, 230) < fg) // light
                fg.w *= .85f;
            else
                fg.w *= .925f;
        }
#endif

        QImage fragment = font->rasterize(part, fg, bg);
        Rectanglei const bounds = font->measure(part);

        painter.drawImage(QPoint(advance + bounds.left(), d->ascent + bounds.top()), fragment);
        advance += font->width(part);
    }
    return img;
}

Rule const &Font::height() const
{
    return *d->heightRule;
}

Rule const &Font::ascent() const
{
    return *d->ascentRule;
}

Rule const &Font::descent() const
{
    return *d->descentRule;
}

Rule const &Font::lineSpacing() const
{
    return *d->lineSpacingRule;
}

} // namespace de
