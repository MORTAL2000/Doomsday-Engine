/** @file materials.h Material Collection.
 *
 * @author Copyright &copy; 2003-2013 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @author Copyright &copy; 2005-2013 Daniel Swanson <danij@dengine.net>
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

#ifndef LIBDENG_RESOURCE_MATERIALS_H
#define LIBDENG_RESOURCE_MATERIALS_H

#include "def_data.h"
#include "material.h"

#ifdef __cplusplus

#include <de/Error>
#include <de/Path>
#include <de/String>
#include "uri.hh"
#include "resource/materialmanifest.h"
#include "resource/materialscheme.h"
#include "resource/materialvariantspec.h"

namespace de {

class MaterialManifest;

/**
 * Specialized resource collection for a set of materials.
 *
 * - Pointers to Material are @em eternal, they are always valid and continue
 *   to reference the same logical material data even after engine reset.
 *
 * - Public material identifiers (materialid_t) are similarly eternal.
 *
 * - Material name bindings are semi-independant from the materials. There
 *   may be multiple name bindings for a given material (aliases). The only
 *   requirement is that their symbolic names must be unique among those in
 *   the same scheme.
 *
 * @ingroup resource
 */
class Materials
{
public:
    typedef class MaterialScheme Scheme;
    typedef class MaterialManifest Manifest;

    /**
     * Defines a group of one or more materials.
     */
    class Group
    {
    public:
        /// All materials in the group.
        typedef QList<Material *> Materials;

    public:
        /// An invalid group member reference was specified. @ingroup errors
        DENG2_ERROR(InvalidMaterialError);

    public:
        Group(int id);

        /**
         * Returns the group's unique identifier.
         */
        int id() const;

        /**
         * Returns the total number of materials in the group.
         */
        int materialCount() const;

        /**
         * Lookup a material in the group by number.
         *
         * @param number  Material number to lookup.
         * @return  Found material.
         */
        Material &material(int number);

        /**
         * Extend the group by adding a new material to the end of the group.
         *
         * @param material  Material to add.
         */
        void addMaterial(Material &material);

        /**
         * Returns @c true iff @a material is part of this group.
         *
         * @param mat  Material to search for.
         */
        bool hasMaterial(Material &material) const;

        /**
         * Provides access to the material list for efficient traversal.
         */
        Materials const &allMaterials() const;

    private:
        /// Unique identifier.
        int id_;

        /// All materials in the group.
        Materials materials;
    };

    /**
     * Flags determining URI validation logic.
     *
     * @see validateUri()
     */
    enum UriValidationFlag
    {
        AnyScheme  = 0x1 ///< The scheme of the URI may be of zero-length; signifying "any scheme".
    };
    Q_DECLARE_FLAGS(UriValidationFlags, UriValidationFlag)

    /// Material system subspace schemes.
    typedef QList<Scheme*> Schemes;

    /// Material groups.
    typedef QList<Group> Groups;

public:
    /// The referenced texture was not found. @ingroup errors
    DENG2_ERROR(NotFoundError);

    /// An unknown scheme was referenced. @ingroup errors
    DENG2_ERROR(UnknownSchemeError);

    /// An unknown group was referenced. @ingroup errors
    DENG2_ERROR(UnknownGroupError);

public:
    /**
     * Constructs a new material collection.
     */
    Materials();

    virtual ~Materials();

    /// Register the console commands, variables, etc..., of this module.
    static void consoleRegister();

    /**
     * Returns the total number of unique materials in the collection.
     */
    uint size() const;

    /**
     * Returns the total number of unique materials in the collection.
     *
     * Same as size()
     */
    inline uint count() const {
        return size();
    }

    /// Process all outstanding tasks in the cache queue.
    void processCacheQueue();

    /// Empty the Material cache queue, cancelling all outstanding tasks.
    void purgeCacheQueue();

    /// To be called during a definition database reset to clear all links to defs.
    void clearDefinitionLinks();

    /**
     * Process a tic of @a elapsed length, animating materials and anim-groups.
     * @param elapsed  Length of tic to be processed.
     */
    void ticker(timespan_t elapsed);

    /**
     * Lookup a material manifest by unique identifier.
     *
     * @param id  Unique identifier for the manifest to be looked up. Note
     *            that @c 0 is not a valid identifier.
     *
     * @return  The found manifest; otherwise @c 0.
     */
    Manifest *toManifest(materialid_t id);

    /**
     * Lookup a subspace scheme by symbolic name.
     *
     * @param name  Symbolic name of the scheme.
     * @return  Scheme associated with @a name.
     *
     * @throws UnknownSchemeError If @a name is unknown.
     */
    Scheme &scheme(String name) const;

    /**
     * Create a new subspace scheme.
     *
     * @param name  Unique symbolic name of the new scheme. Must be at least
     *              @c Scheme::min_name_length characters long.
     */
    Scheme &createScheme(String name);

    /**
     * Returns @c true iff a Scheme exists with the symbolic @a name.
     */
    bool knownScheme(String name) const;

    /**
     * Returns a list of all the schemes for efficient traversal.
     */
    Schemes const &allSchemes() const;

    /**
     * Clear all materials in all schemes.
     * @see Scheme::clear().
     */
    inline void clearAllSchemes()
    {
        Schemes schemes = allSchemes();
        DENG2_FOR_EACH(Schemes, i, schemes){ (*i)->clear(); }
    }

    /**
     * Validate @a uri to determine if it is well-formed and is usable as a
     * search argument.
     *
     * @param uri       Uri to be validated.
     * @param flags     Validation flags.
     * @param quiet     @c true= Do not output validation remarks to the log.
     *
     * @return  @c true if @a Uri passes validation.
     *
     * @todo Should throw de::Error exceptions -ds
     */
    bool validateUri(Uri const &uri, UriValidationFlags flags = 0,
                     bool quiet = false) const;

    /**
     * Determines if a manifest exists for a material on @a path.
     * @return @c true, if a manifest exists; otherwise @a false.
     */
    bool has(Uri const &path) const;

    /**
     * Find the material manifest on @a path.
     *
     * @param search  The search term.
     * @return Found material manifest.
     */
    Manifest &find(Uri const &search) const;

    /**
     * Update @a material according to the supplied definition @a def.
     * To be called after an engine update/reset.
     *
     * @param material  Material to be updated.
     * @param def  Material definition to update using.
     */
    void rebuild(Material &material, ded_material_t *def = 0);

    /**
     * Create a new Material unless an existing Material is found at the path
     * (and within the same scheme) as that specified in @a def, in which case
     * it is returned instead.
     *
     * @note: May fail on invalid definitions (return= @c NULL).
     *
     * @param def  Material definition to construct from.
     * @return  The newly-created/existing material; otherwise @c NULL.
     */
    Material *newFromDef(ded_material_t &def);

    Manifest &newManifest(Scheme &scheme, Path const &path);

    /**
     * Prepare a material variant specification in accordance to the specified
     * usage context. If incomplete context information is supplied, suitable
     * default values will be chosen in their place.
     *
     * @param materialContext   Material (usage) context identifier.
     * @param flags             @ref textureVariantSpecificationFlags
     * @param border            Border size in pixels (all edges).
     * @param tClass            Color palette translation class.
     * @param tMap              Color palette translation map.
     * @param wrapS             GL texture wrap/clamp mode on the horizontal axis (texture-space).
     * @param wrapT             GL texture wrap/clamp mode on the vertical axis (texture-space).
     * @param minFilter         Logical DGL texture minification level.
     * @param magFilter         Logical DGL texture magnification level.
     * @param anisoFilter       @c -1= User preference else a logical DGL anisotropic filter level.
     * @param mipmapped         @c true= use mipmapping.
     * @param gammaCorrection   @c true= apply gamma correction to textures.
     * @param noStretch         @c true= disallow stretching of textures.
     * @param toAlpha           @c true= convert textures to alpha data.
     *
     * @return  Rationalized (and interned) copy of the final specification.
     */
    MaterialVariantSpec const &variantSpecForContext(materialcontext_t materialContext,
        int flags, byte border, int tClass, int tMap, int wrapS, int wrapT,
        int minFilter, int magFilter, int anisoFilter,
        bool mipmapped, bool gammaCorrection, bool noStretch, bool toAlpha);

    /**
     * Add a variant of @a material to the cache queue for deferred preparation.
     *
     * @param material      Base Material from which to derive a variant.
     * @param spec          Specification for the desired derivation of @a material.
     * @param cacheGroups   @c true= variants for all Materials in any applicable groups
     *                      are desired, else just this specific Material.
     */
    void cache(Material &material, MaterialVariantSpec const &spec,
               bool cacheGroups = true);

    /**
     * To be called to reset all animations back to their initial state.
     */
    void resetAllMaterialAnimations();

    /**
     * Lookup a material group by unique @a number.
     */
    Group &group(int number) const;

    /**
     * Create a new material group.
     * @return  Unique identifier associated with the new group.
     */
    int newGroup();

    /**
     * To be called to destroy all materials groups when they are no longer needed.
     */
    void clearAllGroups();

    /**
     * Provides access to the list of material groups for efficient traversal.
     */
    Groups const &allGroups() const;

    /**
     * Returns the total number of material groups in the collection.
     */
    inline int groupCount() const {
        return allGroups().count();
    }

private:
    struct Instance;
    Instance *d;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Materials::UriValidationFlags)

} // namespace de

de::Materials *App_Materials();

extern "C" {
#endif // __cplusplus

/*
 * C wrapper API:
 */

/// Initialize this module. Cannot be re-initialized, must shutdown first.
void Materials_Init(void);

/// Shutdown this module.
void Materials_Shutdown(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LIBDENG_RESOURCE_MATERIALS_H */
