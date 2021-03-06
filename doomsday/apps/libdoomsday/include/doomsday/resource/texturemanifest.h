/** @file texturemanifest.h  Description of a logical texture resource.
 *
 * @authors Copyright © 2010-2013 Daniel Swanson <danij@dengine.net>
 *
 * @par License
 * GPL: http://www.gnu.org/licenses/gpl.html
 *
 * <small>This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details. You should have received a copy of the GNU
 * General Public License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA</small>
 */

#ifndef LIBDOOMSDAY_RESOURCE_TEXTUREMANIFEST_H
#define LIBDOOMSDAY_RESOURCE_TEXTUREMANIFEST_H

#include <de/Error>
#include <de/Observers>
#include <de/PathTree>
#include <de/Vector>
#include <functional>

#include "../uri.h"
#include "texture.h"

namespace res {

class TextureScheme;

/**
 * Description for a would-be logical Texture resource.
 *
 * Models a reference to and the associated metadata for a logical texture
 * in the texture resource collection.
 *
 * @see TextureScheme, Texture
 * @ingroup resource
 */
class LIBDOOMSDAY_PUBLIC TextureManifest : public de::PathTree::Node
{
public:
    /// Required texture instance is missing. @ingroup errors
    DENG2_ERROR(MissingTextureError);

    /// Required resource URI is not defined. @ingroup errors
    DENG2_ERROR(MissingResourceUriError);

    DENG2_DEFINE_AUDIENCE(Deletion,       void textureManifestBeingDeleted   (TextureManifest const &manifest))
    DENG2_DEFINE_AUDIENCE(UniqueIdChange, void textureManifestUniqueIdChanged(TextureManifest &manifest))
    DENG2_DEFINE_AUDIENCE(TextureDerived, void textureManifestTextureDerived (TextureManifest &manifest, Texture &texture))

    typedef std::function<Texture * (TextureManifest &)> TextureConstructor;

public:
    static void setTextureConstructor(TextureConstructor constructor);

    TextureManifest(de::PathTree::NodeArgs const &args);

    /**
     * Derive a new logical Texture instance by interpreting the manifest.
     * The first time a texture is derived from the manifest, said texture
     * is assigned to the manifest (ownership is assumed).
     */
    Texture *derive();

    void setScheme(TextureScheme &ownerScheme);

    /**
     * Returns the owning scheme of the manifest.
     */
    TextureScheme &scheme() const;

    /// Convenience method for returning the name of the owning scheme.
    de::String const &schemeName() const;

    /**
     * Compose a URI of the form "scheme:path" for the TextureManifest.
     *
     * The scheme component of the URI will contain the symbolic name of
     * the scheme for the TextureManifest.
     *
     * The path component of the URI will contain the percent-encoded path
     * of the TextureManifest.
     */
    inline de::Uri composeUri(QChar sep = '/') const
    {
        return de::Uri(schemeName(), path(sep));
    }

    /**
     * Compose a URN of the form "urn:scheme:uniqueid" for the texture
     * TextureManifest.
     *
     * The scheme component of the URI will contain the identifier 'urn'.
     *
     * The path component of the URI is a string which contains both the
     * symbolic name of the scheme followed by the unique id of the texture
     * TextureManifest, separated with a colon.
     *
     * @see uniqueId(), setUniqueId()
     */
    inline de::Uri composeUrn() const
    {
        return de::Uri("urn", de::String("%1:%2").arg(schemeName()).arg(uniqueId(), 0, 10));
    }

    /**
     * Returns a textual description of the manifest.
     *
     * @return Human-friendly description the manifest.
     */
    de::String description(de::Uri::ComposeAsTextFlags uriCompositionFlags = de::Uri::DefaultComposeAsTextFlags) const;

    /**
     * Returns a textual description of the source of the manifest.
     *
     * @return Human-friendly description of the source of the manifest.
     */
    de::String sourceDescription() const;

    /**
     * Returns @c true if a URI to an associated resource is defined.
     */
    bool hasResourceUri() const;

    /**
     * Returns the URI to the associated resource.
     */
    de::Uri resourceUri() const;

    /**
     * Change the resource URI associated with the manifest.
     *
     * @return  @c true iff @a newUri differed to the existing URI, which
     *          was subsequently changed.
     */
    bool setResourceUri(de::Uri const &newUri);

    /**
     * Returns the scheme-unique identifier for the manifest.
     */
    int uniqueId() const;

    /**
     * Change the unique identifier property of the manifest.
     *
     * @return  @c true iff @a newUniqueId differed to the existing unique
     *          identifier, which was subsequently changed.
     */
    bool setUniqueId(int newUniqueId);

    /**
     * Returns the logical dimensions property of the manifest.
     */
    de::Vector2ui const &logicalDimensions() const;

    /**
     * Change the logical dimensions property of the manifest.
     *
     * @param newDimensions  New logical dimensions. Components can be @c 0 in
     * which case their value will be inherited from the pixel dimensions of
     * the image at load time.
     */
    bool setLogicalDimensions(de::Vector2ui const &newDimensions);

    /**
     * Returns the world origin offset property of the manifest.
     */
    de::Vector2i const &origin() const;

    /**
     * Change the world origin offest property of the manifest.
     *
     * @param newOrigin  New origin offset.
     */
    void setOrigin(de::Vector2i const &newOrigin);

    /**
     * Returns the texture flags property of the manifest.
     */
    Texture::Flags flags() const;

    /**
     * Change the texture flags property of the manifest.
     *
     * @param flagsToChange  Flags to change the value of.
     * @param operation      Logical operation to perform on the flags.
     */
    void setFlags(Texture::Flags flagsToChange, de::FlagOp operation = de::SetFlags);

    /**
     * Returns @c true if a Texture is presently associated with the manifest.
     *
     * @see texture(), texturePtr()
     */
    bool hasTexture() const;

    /**
     * Returns the logical Texture associated with the manifest.
     *
     * @see hasTexture()
     */
    Texture &texture() const;

    /**
     * Returns a pointer to the associated Texture resource; otherwise @c 0.
     *
     * @see hasTexture()
     */
    Texture *texturePtr() const;

    /**
     * Change the logical Texture associated with the manifest.
     *
     * @param newTexture  New logical Texture to associate.
     */
    void setTexture(Texture *newTexture);

    /**
     * Clear the logical Texture associated with the manifest.
     */
    inline void clearTexture() { setTexture(nullptr); }

private:
    DENG2_PRIVATE(d)
};

} // namespace res

#endif // LIBDOOMSDAY_RESOURCE_TEXTUREMANIFEST_H
