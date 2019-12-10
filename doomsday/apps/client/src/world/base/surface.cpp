/** @file surface.cpp  World map surface.
 *
 * @authors Copyright © 2003-2017 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2006-2015 Daniel Swanson <danij@dengine.net>
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

#include "world/surface.h"

#include "world/clientserverworld.h" // ddMapSetup
#include "world/map.h"
#include "Plane"
#ifdef __CLIENT__
#  include "Decoration"
#endif

#include "misc/r_util.h" // R_NameForBlendMode()

#ifdef __CLIENT__
#  include "gl/gl_tex.h"
#  include "render/rend_main.h"
#  include "resource/clienttexture.h"

#  include "dd_loop.h" // frameTimePos

#  include <doomsday/resource/texturemanifest.h>
#endif
#include <doomsday/world/MaterialManifest>
#include <doomsday/world/Material>
#include <de/Log>
#include <de/vector1.h>

using namespace de;
using namespace world;

#ifdef DENG_DEBUG
static inline bool Surface_isSideMiddle(Surface const &suf)
{
    return suf.parent().type() == DMU_SIDE
           && &suf == &suf.parent().as<LineSide>().middle();
}

static inline bool Surface_isSectorExtraPlane(Surface const &suf)
{
    if (suf.parent().type() != DMU_PLANE) return false;
    auto const &plane = suf.parent().as<Plane>();
    return !(plane.isSectorFloor() || plane.isSectorCeiling());
}
#endif

DENG2_PIMPL(Surface)
{
    dint flags = 0;                             ///< @ref sufFlags

    Matrix3f tangentMatrix { Matrix3f::Zero };  ///< Tangent space vectors.
    bool needUpdateTangentMatrix = false;       ///< @c true= marked for update.

    Material *material = nullptr;               ///< Currently bound material.
    bool materialIsMissingFix = false;          ///< @c true= @ref material is a "missing fix".

    Vector2f origin;                            ///< @em sharp offset to surface-space origin.
    Vector3f color;
    dfloat opacity = 0;
    blendmode_t blendMode { BM_NORMAL };

#ifdef __CLIENT__
    Vector2f oldOrigin[2];                      ///< Old @em sharp surface space material origins, for smoothing.
    Vector2f originSmoothed;                    ///< @em smoothed surface space material origin.
    Vector2f originSmoothedDelta;               ///< Delta between @em sharp and @em smoothed.
    MaterialAnimator *matAnimator = nullptr;
#endif

    Impl(Public *i) : Base(i)
    {}

#ifdef __CLIENT__
    ~Impl()
    {
        // Stop scroll interpolation for this surface.
        self().map().scrollingSurfaces().remove(thisPublic);
    }
#endif

    inline MapElement &owner() const { return self().parent(); }

#ifdef DENG_DEBUG
    bool isSideMiddle() const
    {
        return owner().type() == DMU_SIDE
               && thisPublic == &owner().as<LineSide>().middle();
    }

    bool isSectorExtraPlane() const
    {
        if (owner().type() != DMU_PLANE) return false;
        auto const &plane = owner().as<Plane>();
        return !(plane.isSectorFloor() || plane.isSectorCeiling());
    }
#endif

    void updateTangentMatrix()
    {
        needUpdateTangentMatrix = false;

        dfloat values[9];
        Vector3f normal = tangentMatrix.column(2);
        V3f_Set(values + 6, normal.x, normal.y, normal.z);
        V3f_BuildTangents(values, values + 3, values + 6);

        tangentMatrix = Matrix3f(values);
    }

#ifdef __CLIENT__
    void notifyOriginSmoothedChanged()
    {
        DENG2_FOR_PUBLIC_AUDIENCE2(OriginSmoothedChange, i) i->surfaceOriginSmoothedChanged(self());
    }
#endif

    DENG2_PIMPL_AUDIENCE(ColorChange)
    DENG2_PIMPL_AUDIENCE(MaterialChange)
    DENG2_PIMPL_AUDIENCE(NormalChange)
    DENG2_PIMPL_AUDIENCE(OpacityChange)
    DENG2_PIMPL_AUDIENCE(OriginChange)
#ifdef __CLIENT__
    DENG2_PIMPL_AUDIENCE(OriginSmoothedChange)
#endif
};

DENG2_AUDIENCE_METHOD(Surface, ColorChange)
DENG2_AUDIENCE_METHOD(Surface, MaterialChange)
DENG2_AUDIENCE_METHOD(Surface, NormalChange)
DENG2_AUDIENCE_METHOD(Surface, OpacityChange)
DENG2_AUDIENCE_METHOD(Surface, OriginChange)
#ifdef __CLIENT__
DENG2_AUDIENCE_METHOD(Surface, OriginSmoothedChange)
#endif

Surface::Surface(MapElement &owner, dfloat opacity, Vector3f const &color)
    : MapElement(DMU_SURFACE, &owner)
    , d(new Impl(this))
{
    d->color   = color;
    d->opacity = opacity;
}

String Surface::description() const
{
    auto desc = String(    _E(l) "Material: "        _E(.)_E(i) "%1" _E(.)
                       " " _E(l) "Material Origin: " _E(.)_E(i) "%2" _E(.)
                       " " _E(l) "Normal: "          _E(.)_E(i) "%3" _E(.)
                       " " _E(l) "Opacity: "         _E(.)_E(i) "%4" _E(.)
                       " " _E(l) "Blend Mode: "      _E(.)_E(i) "%5" _E(.)
                       " " _E(l) "Tint Color: "      _E(.)_E(i) "%6" _E(.))
                  .arg(hasMaterial() ? material().manifest().composeUri().asText() : "None")
                  .arg(origin().asText())
                  .arg(normal().asText())
                  .arg(opacity())
                  .arg(String(R_NameForBlendMode(blendMode())))
                  .arg(color().asText());

#ifdef DENG2_DEBUG
    desc.prepend(String(_E(b) "Surface " _E(.) "[0x%1]\n").arg(de::dintptr(this), 0, 16));
#endif
    return desc;
}

Matrix3f const &Surface::tangentMatrix() const
{
    // Perform any scheduled update now.
    if (d->needUpdateTangentMatrix)
    {
        d->updateTangentMatrix();
    }
    return d->tangentMatrix;
}

Surface &Surface::setNormal(Vector3f const &newNormal)
{
    Vector3f const oldNormal = normal();
    Vector3f const newNormalNormalized = newNormal.normalize();
    if (oldNormal != newNormalNormalized)
    {
        for (dint i = 0; i < 3; ++i)
        {
            d->tangentMatrix.at(i, 2) = newNormalNormalized[i];
        }

        // We'll need to recalculate the tangents when next referenced.
        d->needUpdateTangentMatrix = true;
        DENG2_FOR_AUDIENCE2(NormalChange, i) i->surfaceNormalChanged(*this);
    }
    return *this;
}

bool Surface::hasMaterial() const
{
    return d->material != nullptr;
}

bool Surface::hasFixMaterial() const
{
    return hasMaterial() && d->materialIsMissingFix;
}

Material &Surface::material() const
{
    if (d->material) return *d->material;
    /// @throw MissingMaterialError Attempted with no material bound.
    throw MissingMaterialError("Surface::material", "No material is bound");
}

Material *Surface::materialPtr() const
{
    return d->material;
}

Surface &Surface::setMaterial(Material *newMaterial, bool isMissingFix)
{
    // Sides of selfreferencing map lines should never receive fix materials.
    DENG2_ASSERT(!(isMissingFix && (parent().type() == DMU_SIDE && parent().as<LineSide>().line().isSelfReferencing())));

    if (d->material == newMaterial)
        return *this;

    d->materialIsMissingFix = false;
    d->material = newMaterial;
    if (d->material && isMissingFix)
    {
        d->materialIsMissingFix = true;
    }

    // During map setup we log missing material fixes.
    if (::ddMapSetup && d->materialIsMissingFix && d->material)
    {
        if (d->owner().type() == DMU_SIDE)
        {
            auto &side = d->owner().as<LineSide>();
            dint section = (  this == &side.middle() ? LineSide::Middle
                            : this == &side.bottom() ? LineSide::Bottom
                            :                          LineSide::Top);

            LOGDEV_MAP_WARNING("%s of Line #%d is missing a material for the %s section."
                               "\n  %s was chosen to complete the definition.")
                << Line::sideIdAsText(side.sideId()).upperFirstChar()
                << side.line().indexInMap()
                << LineSide::sectionIdAsText(section)
                << d->material->manifest().composeUri().asText();
        }
    }

#ifdef __CLIENT__
    d->matAnimator = nullptr;
#endif

    // Notify interested parties.
    DENG2_FOR_AUDIENCE2(MaterialChange, i)
    {
        i->surfaceMaterialChanged(*this);
    }
    return *this;
}

Vector2f const &Surface::origin() const
{
    return d->origin;
}

Surface &Surface::setOrigin(Vector2f const &newOrigin)
{
    if (d->origin != newOrigin)
    {
        d->origin = newOrigin;
#ifdef __CLIENT__
        if (::ddMapSetup)
        {
            // During map setup we'll apply this immediately to the visual origin also.
            d->originSmoothed = d->origin;
            d->originSmoothedDelta = Vector2f();

            d->oldOrigin[0] = d->oldOrigin[1] = d->origin;
        }
#endif

        DENG2_FOR_AUDIENCE2(OriginChange, i) i->surfaceOriginChanged(*this);

#ifdef __CLIENT__
        if (!::ddMapSetup)
        {
            map().scrollingSurfaces().insert(this);
        }
#endif
    }
    return *this;
}

bool Surface::materialMirrorX() const
{
    return (d->flags & DDSUF_MATERIAL_FLIPH) != 0;
}

bool Surface::materialMirrorY() const
{
    return (d->flags & DDSUF_MATERIAL_FLIPV) != 0;
}

Vector2f Surface::materialScale() const
{
    return Vector2f(materialMirrorX()? -1 : 1, materialMirrorY()? -1 : 1);
}

de::Uri Surface::composeMaterialUri() const
{
    if (!hasMaterial()) return de::Uri();
    return material().manifest().composeUri();
}

void Surface::setDecorationState(IDecorationState *state)
{
    _decorationState.reset(state);
}

dfloat Surface::opacity() const
{
    return d->opacity;
}

Surface &Surface::setOpacity(dfloat newOpacity)
{
    DENG2_ASSERT(Surface_isSideMiddle(*this) || Surface_isSectorExtraPlane(*this));  // sanity check

    newOpacity = de::clamp(0.f, newOpacity, 1.f);
    if (!de::fequal(d->opacity, newOpacity))
    {
        d->opacity = newOpacity;
        DENG2_FOR_AUDIENCE2(OpacityChange, i) i->surfaceOpacityChanged(*this);
    }
    return *this;
}

Vector3f const &Surface::color() const
{
    return d->color;
}

Surface &Surface::setColor(Vector3f const &newColor)
{
    Vector3f const newColorClamped(de::clamp(0.f, newColor.x, 1.f),
                                   de::clamp(0.f, newColor.y, 1.f),
                                   de::clamp(0.f, newColor.z, 1.f));

    if (d->color != newColorClamped)
    {
        d->color = newColorClamped;
        DENG2_FOR_AUDIENCE2(ColorChange, i) i->surfaceColorChanged(*this);
    }
    return *this;
}

blendmode_t Surface::blendMode() const
{
    return d->blendMode;
}

Surface &Surface::setBlendMode(blendmode_t newBlendMode)
{
    d->blendMode = newBlendMode;
    return *this;
}

dint Surface::property(DmuArgs &args) const
{
    switch (args.prop)
    {
    case DMU_MATERIAL: {
        Material *mat = (d->materialIsMissingFix ? nullptr : d->material);
        args.setValue(DMT_SURFACE_MATERIAL, &mat, 0);
        break; }

    case DMU_OFFSET_X:
        args.setValue(DMT_SURFACE_OFFSET, &d->origin.x, 0);
        break;

    case DMU_OFFSET_Y:
        args.setValue(DMT_SURFACE_OFFSET, &d->origin.y, 0);
        break;

    case DMU_OFFSET_XY:
        args.setValue(DMT_SURFACE_OFFSET, &d->origin.x, 0);
        args.setValue(DMT_SURFACE_OFFSET, &d->origin.y, 1);
        break;

    case DMU_TANGENT_X:
        args.setValue(DMT_SURFACE_TANGENT, &d->tangentMatrix.at(0, 0), 0);
        break;

    case DMU_TANGENT_Y:
        args.setValue(DMT_SURFACE_TANGENT, &d->tangentMatrix.at(1, 0), 0);
        break;

    case DMU_TANGENT_Z:
        args.setValue(DMT_SURFACE_TANGENT, &d->tangentMatrix.at(2, 0), 0);
        break;

    case DMU_TANGENT_XYZ:
        args.setValue(DMT_SURFACE_TANGENT, &d->tangentMatrix.at(0, 0), 0);
        args.setValue(DMT_SURFACE_TANGENT, &d->tangentMatrix.at(1, 0), 1);
        args.setValue(DMT_SURFACE_TANGENT, &d->tangentMatrix.at(2, 0), 2);
        break;

    case DMU_BITANGENT_X:
        args.setValue(DMT_SURFACE_BITANGENT, &d->tangentMatrix.at(0, 1), 0);
        break;

    case DMU_BITANGENT_Y:
        args.setValue(DMT_SURFACE_BITANGENT, &d->tangentMatrix.at(1, 1), 0);
        break;

    case DMU_BITANGENT_Z:
        args.setValue(DMT_SURFACE_BITANGENT, &d->tangentMatrix.at(2, 1), 0);
        break;

    case DMU_BITANGENT_XYZ:
        args.setValue(DMT_SURFACE_BITANGENT, &d->tangentMatrix.at(0, 1), 0);
        args.setValue(DMT_SURFACE_BITANGENT, &d->tangentMatrix.at(1, 1), 1);
        args.setValue(DMT_SURFACE_BITANGENT, &d->tangentMatrix.at(2, 1), 2);
        break;

    case DMU_NORMAL_X:
        args.setValue(DMT_SURFACE_NORMAL, &d->tangentMatrix.at(0, 2), 0);
        break;

    case DMU_NORMAL_Y:
        args.setValue(DMT_SURFACE_NORMAL, &d->tangentMatrix.at(1, 2), 0);
        break;

    case DMU_NORMAL_Z:
        args.setValue(DMT_SURFACE_NORMAL, &d->tangentMatrix.at(2, 2), 0);
        break;

    case DMU_NORMAL_XYZ:
        args.setValue(DMT_SURFACE_NORMAL, &d->tangentMatrix.at(0, 2), 0);
        args.setValue(DMT_SURFACE_NORMAL, &d->tangentMatrix.at(1, 2), 1);
        args.setValue(DMT_SURFACE_NORMAL, &d->tangentMatrix.at(2, 2), 2);
        break;

    case DMU_COLOR:
        args.setValue(DMT_SURFACE_RGBA, &d->color.x, 0);
        args.setValue(DMT_SURFACE_RGBA, &d->color.y, 1);
        args.setValue(DMT_SURFACE_RGBA, &d->color.z, 2);
        args.setValue(DMT_SURFACE_RGBA, &d->opacity, 2);
        break;

    case DMU_COLOR_RED:
        args.setValue(DMT_SURFACE_RGBA, &d->color.x, 0);
        break;

    case DMU_COLOR_GREEN:
        args.setValue(DMT_SURFACE_RGBA, &d->color.y, 0);
        break;

    case DMU_COLOR_BLUE:
        args.setValue(DMT_SURFACE_RGBA, &d->color.z, 0);
        break;

    case DMU_ALPHA:
        args.setValue(DMT_SURFACE_RGBA, &d->opacity, 0);
        break;

    case DMU_BLENDMODE:
        args.setValue(DMT_SURFACE_BLENDMODE, &d->blendMode, 0);
        break;

    case DMU_FLAGS:
        args.setValue(DMT_SURFACE_FLAGS, &d->flags, 0);
        break;

    default:
        return MapElement::property(args);
    }

    return false;  // Continue iteration.
}

dint Surface::setProperty(DmuArgs const &args)
{
    switch (args.prop)
    {
    case DMU_BLENDMODE: {
        blendmode_t newBlendMode;
        args.value(DMT_SURFACE_BLENDMODE, &newBlendMode, 0);
        setBlendMode(newBlendMode);
        break; }

    case DMU_FLAGS:
        args.value(DMT_SURFACE_FLAGS, &d->flags, 0);
        break;

    case DMU_COLOR: {
        Vector3f newColor = d->color;
        args.value(DMT_SURFACE_RGBA, &newColor.x, 0);
        args.value(DMT_SURFACE_RGBA, &newColor.y, 1);
        args.value(DMT_SURFACE_RGBA, &newColor.z, 2);
        setColor(newColor);
        break; }

    case DMU_COLOR_RED: {
        Vector3f newColor = d->color;
        args.value(DMT_SURFACE_RGBA, &newColor.x, 0);
        setColor(newColor);
        break; }

    case DMU_COLOR_GREEN: {
        Vector3f newColor = d->color;
        args.value(DMT_SURFACE_RGBA, &newColor.y, 0);
        setColor(newColor);
        break; }

    case DMU_COLOR_BLUE: {
        Vector3f newColor = d->color;
        args.value(DMT_SURFACE_RGBA, &newColor.z, 0);
        setColor(newColor);
        break; }

    case DMU_ALPHA: {
        dfloat newOpacity;
        args.value(DMT_SURFACE_RGBA, &newOpacity, 0);
        setOpacity(newOpacity);
        break; }

    case DMU_MATERIAL: {
        Material *newMaterial;
        args.value(DMT_SURFACE_MATERIAL, &newMaterial, 0);
        setMaterial(newMaterial);
        break; }

    case DMU_OFFSET_X: {
        Vector2f newOrigin = d->origin;
        args.value(DMT_SURFACE_OFFSET, &newOrigin.x, 0);
        setOrigin(newOrigin);
        break; }

    case DMU_OFFSET_Y: {
        Vector2f newOrigin = d->origin;
        args.value(DMT_SURFACE_OFFSET, &newOrigin.y, 0);
        setOrigin(newOrigin);
        break; }

    case DMU_OFFSET_XY: {
        Vector2f newOrigin = d->origin;
        args.value(DMT_SURFACE_OFFSET, &newOrigin.x, 0);
        args.value(DMT_SURFACE_OFFSET, &newOrigin.y, 1);
        setOrigin(newOrigin);
        break; }

    default:
        return MapElement::setProperty(args);
    }

    return false;  // Continue iteration.
}

#ifdef __CLIENT__

MaterialAnimator *Surface::materialAnimator() const
{
    if (!d->material) return nullptr;

    if (!d->matAnimator)
    {
        d->matAnimator = &d->material->as<ClientMaterial>()
                .getAnimator(Rend_MapSurfaceMaterialSpec());
    }
    return d->matAnimator;
}

void Surface::resetLookups()
{
    d->matAnimator = nullptr;
}

Vector2f const &Surface::originSmoothed() const
{
    return d->originSmoothed;
}

Vector2f const &Surface::originSmoothedAsDelta() const
{
    return d->originSmoothedDelta;
}

void Surface::lerpSmoothedOrigin()
{
    // $smoothmaterialorigin
    d->originSmoothedDelta = d->oldOrigin[0] * (1 - ::frameTimePos)
        + origin() * ::frameTimePos - origin();

    // Visible material origin.
    d->originSmoothed = origin() + d->originSmoothedDelta;

    d->notifyOriginSmoothedChanged();
}

void Surface::resetSmoothedOrigin()
{
    // $smoothmaterialorigin
    d->originSmoothed = d->oldOrigin[0] = d->oldOrigin[1] = origin();
    d->originSmoothedDelta = Vector2f();

    d->notifyOriginSmoothedChanged();
}

void Surface::updateOriginTracking()
{
    // $smoothmaterialorigin
    d->oldOrigin[0] = d->oldOrigin[1];
    d->oldOrigin[1] = origin();

    if (d->oldOrigin[0] != d->oldOrigin[1])
    {
        dfloat moveDistance = de::abs(Vector2f(d->oldOrigin[1] - d->oldOrigin[0]).length());

        if (moveDistance >= MAX_SMOOTH_MATERIAL_MOVE)
        {
            // Too fast: make an instantaneous jump.
            d->oldOrigin[0] = d->oldOrigin[1];
        }
    }
}

dfloat Surface::glow(Vector3f &color) const
{
    if (!hasMaterial() || material().isSkyMasked())
    {
        color = Vector3f();
        return 0;
    }

    MaterialAnimator &matAnimator = *materialAnimator(); //material().as<ClientMaterial>().getAnimator(Rend_MapSurfaceMaterialSpec());

    // Ensure we've up to date info about the material.
    matAnimator.prepare();

    TextureVariant *texture = matAnimator.texUnit(MaterialAnimator::TU_LAYER0).texture;
    if (!texture) return 0;
    auto const *avgColorAmplified = reinterpret_cast<averagecolor_analysis_t const *>(texture->base().analysisDataPointer(ClientTexture::AverageColorAmplifiedAnalysis));
    if (!avgColorAmplified)
    {
        //throw Error("Surface::glow", "Texture \"" + texture->base().manifest().composeUri().asText() + "\" has no AverageColorAmplifiedAnalysis");
        return 0;
    }

    color = Vector3f(avgColorAmplified->color.rgb);
    return matAnimator.glowStrength() * glowFactor; // Global scale factor.
}

#endif // __CLIENT__

Surface::IDecorationState::~IDecorationState()
{}
