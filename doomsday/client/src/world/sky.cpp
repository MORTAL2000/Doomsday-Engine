/** @file sky.cpp  Sky behavior logic for the world system.
 *
 * @authors Copyright © 2003-2013 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2006-2014 Daniel Swanson <danij@dengine.net>
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

#include "de_base.h"
#include "world/sky.h"

#include <cmath>
#include <QtAlgorithms>
#include <de/Log>

#ifdef __CLIENT__
#  include "clientapp.h"
#  include "gl/gl_main.h"
#  include "gl/gl_tex.h"
#  include "render/rend_main.h"    // rendSkyLightAuto
#  include "render/skydrawable.h"  // SkyDrawable::layerMaterialSpec

#  include "MaterialSnapshot"
#  include "Texture"
#endif

#define NUM_LAYERS  2

using namespace de;

DENG2_PIMPL_NOREF(Sky::Layer)
{
    bool active        = false;
    bool masked        = false;
    Material *material = nullptr;
    float offset       = 0;
    float fadeoutLimit = 0;

    Sky &sky;
    Instance(Sky &sky) : sky(sky) {}

    DENG2_PIMPL_AUDIENCE(ActiveChange)
    DENG2_PIMPL_AUDIENCE(MaskedChange)
    DENG2_PIMPL_AUDIENCE(MaterialChange)
};

DENG2_AUDIENCE_METHOD(Sky::Layer, ActiveChange)
DENG2_AUDIENCE_METHOD(Sky::Layer, MaskedChange)
DENG2_AUDIENCE_METHOD(Sky::Layer, MaterialChange)

Sky::Layer::Layer(Sky &sky, Material *material) : d(new Instance(sky))
{
    setMaterial(material);
}

Sky &Sky::Layer::sky() const
{
    return d->sky;
}

bool Sky::Layer::isActive() const
{
    return d->active;
}

void Sky::Layer::setActive(bool yes)
{
    if(d->active == yes) return;

    d->active = yes;
    DENG2_FOR_AUDIENCE2(ActiveChange, i)
    {
        i->skyLayerActiveChanged(*this);
    }
}

bool Sky::Layer::isMasked() const
{
    return d->masked;
}

void Sky::Layer::setMasked(bool yes)
{
    if(d->masked == yes) return;

    d->masked = yes;
    DENG2_FOR_AUDIENCE2(MaskedChange, i)
    {
        i->skyLayerMaskedChanged(*this);
    }
}

Material *Sky::Layer::material() const
{
    return d->material;
}

void Sky::Layer::setMaterial(Material *newMaterial)
{
    if(d->material == newMaterial) return;

    d->material = newMaterial;
    DENG2_FOR_AUDIENCE2(MaterialChange, i)
    {
        i->skyLayerMaterialChanged(*this);
    }
}

float Sky::Layer::offset() const
{
    return d->offset;
}

void Sky::Layer::setOffset(float newOffset)
{
    d->offset = newOffset;
}

float Sky::Layer::fadeOutLimit() const
{
    return d->fadeoutLimit;
}

void Sky::Layer::setFadeoutLimit(float newLimit)
{
    d->fadeoutLimit = newLimit;
}

DENG2_PIMPL(Sky)
, DENG2_OBSERVES(Layer, ActiveChange)
#ifdef __CLIENT__
, DENG2_OBSERVES(Layer, MaterialChange)
, DENG2_OBSERVES(Layer, MaskedChange)
#endif
{
    Layers layers;
    int firstActiveLayer            = -1;  /// @c -1= 'no active layers'.
    bool needFirstActiveLayerUpdate = true;

    float height        = 0;
    float horizonOffset = 0;

#ifdef __CLIENT__
    bool ambientColorDefined    = false;  /// @c true= pre-defined in a MapInfo def.
    bool needUpdateAmbientColor = true;   /// @c true= update if not pre-defined.
    Vector3f ambientColor;
#endif

    Instance(Public *i) : Base(i)
    {
        for(int i = 0; i < NUM_LAYERS; ++i)
        {
            layers.append(new Layer(self));
            Layer *layer = layers.last();

            layer->audienceForActiveChange()   += this;
#ifdef __CLIENT__
            layer->audienceForMaskedChange()   += this;
            layer->audienceForMaterialChange() += this;
#endif
        }
    }

    ~Instance()
    {
        qDeleteAll(layers);
    }

    void updateFirstActiveLayerIfNeeded()
    {
        if(!needFirstActiveLayerUpdate) return;
        needFirstActiveLayerUpdate = false;

        firstActiveLayer = -1; // -1 denotes 'no active layers'.
        for(int i = 0; i < layers.count(); ++i)
        {
            if(layers[i]->isActive())
            {
                firstActiveLayer = i;
                break;
            }
        }
    }

    /// Observes Layer ActiveChange
    void skyLayerActiveChanged(Layer & /*layer*/)
    {
        needFirstActiveLayerUpdate = true;
#ifdef __CLIENT__
        needUpdateAmbientColor     = true;
#endif
    }

#ifdef __CLIENT__

    /**
     * @todo Move to SkyDrawable and have it simply update this component once the
     * ambient color has been calculated.
     *
     * @todo Re-implement me by rendering the sky to a low-quality cubemap and use
     * that to obtain the lighting characteristics.
     */
    void updateAmbientColorIfNeeded()
    {
        if(!needUpdateAmbientColor) return;

        needUpdateAmbientColor = false;

        // By default the ambient color is pure white.
        ambientColor = Vector3f(1, 1, 1);

        updateFirstActiveLayerIfNeeded();
        if(firstActiveLayer < 0) return;

        Vector3f avgMaterialColor;
        Vector3f bottomCapColor;
        Vector3f topCapColor;

        int avgCount = 0;
        for(int i = firstActiveLayer; i < layers.count(); ++i)
        {
            Layer &layer = *layers[i];

            // Inactive layers won't be drawn.
            if(!layer.isActive()) continue;

            // A material is required for drawing.
            if(!layer.material()) continue;
            Material *mat = layer.material();

            // Prepare and ensure the material has at least a primary texture.
            MaterialSnapshot const &ms = mat->prepare(SkyDrawable::layerMaterialSpec(layer.isMasked()));
            if(ms.hasTexture(MTU_PRIMARY))
            {
                Texture const &tex = ms.texture(MTU_PRIMARY).generalCase();
                averagecolor_analysis_t const *avgColor = reinterpret_cast<averagecolor_analysis_t const *>(tex.analysisDataPointer(Texture::AverageColorAnalysis));
                if(!avgColor) throw Error("calculateSkyAmbientColor", QString("Texture \"%1\" has no AverageColorAnalysis").arg(ms.texture(MTU_PRIMARY).generalCase().manifest().composeUri()));

                if(i == firstActiveLayer)
                {
                    averagecolor_analysis_t const *avgLineColor = reinterpret_cast<averagecolor_analysis_t const *>(tex.analysisDataPointer(Texture::AverageTopColorAnalysis));
                    if(!avgLineColor) throw Error("calculateSkyAmbientColor", QString("Texture \"%1\" has no AverageTopColorAnalysis").arg(tex.manifest().composeUri()));

                    topCapColor = Vector3f(avgLineColor->color.rgb);

                    avgLineColor = reinterpret_cast<averagecolor_analysis_t const *>(tex.analysisDataPointer(Texture::AverageBottomColorAnalysis));
                    if(!avgLineColor) throw Error("calculateSkyAmbientColor", QString("Texture \"%1\" has no AverageBottomColorAnalysis").arg(tex.manifest().composeUri()));

                    bottomCapColor = Vector3f(avgLineColor->color.rgb);
                }

                avgMaterialColor += Vector3f(avgColor->color.rgb);
                ++avgCount;
            }
        }

        if(avgCount)
        {
            // The caps cover a large amount of the sky sphere, so factor it in too.
            // Each cap is another unit.
            ambientColor = (avgMaterialColor + topCapColor + bottomCapColor) / (avgCount + 2);
        }
    }

    /// Observes Layer MaterialChange
    void skyLayerMaterialChanged(Layer &layer)
    {
        // We may need to recalculate the ambient color of the sky.
        if(!layer.isActive()) return;
        //if(ambientColorDefined) return;

        needUpdateAmbientColor = true;
    }

    /// Observes Layer MaskedChange
    void skyLayerMaskedChanged(Layer &layer)
    {
        // We may need to recalculate the ambient color of the sky.
        if(!layer.isActive()) return;
        //if(ambientColorDefined) return;

        needUpdateAmbientColor = true;
    }

#endif // __CLIENT__

    DENG2_PIMPL_AUDIENCE(HeightChange)
    DENG2_PIMPL_AUDIENCE(HorizonOffsetChange)
};

DENG2_AUDIENCE_METHOD(Sky, HeightChange)
DENG2_AUDIENCE_METHOD(Sky, HorizonOffsetChange)

Sky::Sky(defn::Sky const *definition) : MapElement(DMU_SKY), d(new Instance(this))
{
    configure(definition);
}

Sky::Layers const &Sky::layers() const
{
    return d->layers;
}

void Sky::configure(defn::Sky const *def)
{
    LOG_AS("Sky::configure");

    setHeight(def? def->getf("height") : DEFAULT_SKY_HEIGHT);
    setHorizonOffset(def? def->getf("horizonOffset") : DEFAULT_SKY_HORIZON_OFFSET);

    for(int i = 0; i < d->layers.count(); ++i)
    {
        Record const *lyrDef = def? &def->layer(i) : 0;
        Layer &lyr = *d->layers[i];

        lyr.setMasked(lyrDef? (lyrDef->geti("flags") & SLF_MASK) : false);
        lyr.setOffset(lyrDef? lyrDef->getf("offset") : DEFAULT_SKY_SPHERE_XOFFSET);
        lyr.setFadeoutLimit(lyrDef? lyrDef->getf("colorLimit") : DEFAULT_SKY_SPHERE_FADEOUT_LIMIT);

        de::Uri const matUri = lyrDef? de::Uri(lyrDef->gets("material")) : de::Uri(DEFAULT_SKY_SPHERE_MATERIAL, RC_NULL);
        Material *mat = 0;
        try
        {
            mat = App_ResourceSystem().materialPtr(matUri);
        }
        catch(MaterialManifest::MissingMaterialError const &er)
        {
            // Log if a material is specified but otherwise ignore this error.
            if(lyrDef)
            {
                LOG_RES_WARNING(er.asText() + ". Unknown material \"%s\" in definition layer %i, using default")
                        << matUri << i;
            }
        }
        lyr.setMaterial(mat);

        lyr.setActive(lyrDef? (lyrDef->geti("flags") & SLF_ENABLE) : i == 0);
    }

#ifdef __CLIENT__
    if(def)
    {
        Vector3f ambientColor = Vector3f(def->get("color")).max(Vector3f(0, 0, 0));
        if(ambientColor != Vector3f(0, 0, 0))
        {
            setAmbientColor(ambientColor);
        }
    }
    else
    {
        d->ambientColor           = Vector3f(1, 1, 1);
        d->ambientColorDefined    = false;
        d->needUpdateAmbientColor = true;
    }

    // Models are set up using the data in the definition (will override the sphere by default).
    ClientApp::renderSystem().sky().setupModels(def);
#endif
}

float Sky::height() const
{
    return d->height;
}

void Sky::setHeight(float newHeight)
{
    newHeight = de::clamp(0.f, newHeight, 1.f);
    if(!de::fequal(d->height, newHeight))
    {
        d->height = newHeight;
        DENG2_FOR_AUDIENCE2(HeightChange, i)
        {
            i->skyHeightChanged(*this);
        }
    }
}

float Sky::horizonOffset() const
{
    return d->horizonOffset;
}

void Sky::setHorizonOffset(float newOffset)
{
    if(!de::fequal(d->horizonOffset, newOffset))
    {
        d->horizonOffset = newOffset;
        DENG2_FOR_AUDIENCE2(HorizonOffsetChange, i)
        {
            i->skyHorizonOffsetChanged(*this);
        }
    }
}

int Sky::firstActiveLayer() const
{
    d->updateFirstActiveLayerIfNeeded();
    return d->firstActiveLayer;
}

int Sky::property(DmuArgs &args) const
{
    switch(args.prop)
    {
    case DMU_FLAGS: {
        int flags = 0;
        if(layer(0)->isActive()) flags |= SKYF_LAYER0_ENABLED;
        if(layer(1)->isActive()) flags |= SKYF_LAYER1_ENABLED;

        args.setValue(DDVT_INT, &flags, 0);
        break; }

    case DMU_HEIGHT:
        args.setValue(DDVT_FLOAT, &d->height, 0);
        break;

    /*case DMU_HORIZONOFFSET:
        args.setValue(DDVT_FLOAT, &d->horizonOffset, 0);
        break;*/

    default: return MapElement::property(args);
    }

    return false; // Continue iteration.
}

int Sky::setProperty(DmuArgs const &args)
{
    switch(args.prop)
    {
    case DMU_FLAGS: {
        int flags = 0;
        if(layer(0)->isActive()) flags |= SKYF_LAYER0_ENABLED;
        if(layer(1)->isActive()) flags |= SKYF_LAYER1_ENABLED;

        args.value(DDVT_INT, &flags, 0);

        layer(0)->setActive(flags & SKYF_LAYER0_ENABLED);
        layer(1)->setActive(flags & SKYF_LAYER1_ENABLED);
        break; }

    case DMU_HEIGHT: {
        float newHeight = d->height;
        args.value(DDVT_FLOAT, &newHeight, 0);

        setHeight(newHeight);
        break; }

    /*case DMU_HORIZONOFFSET: {
        float newOffset = d->horizonOffset;
        args.value(DDVT_FLOAT, &d->horizonOffset, 0);

        setHorizonOffset(newOffset);
        break; }*/

    default: return MapElement::setProperty(args);
    }

    return false; // Continue iteration.
}

#ifdef __CLIENT__

Vector3f const &Sky::ambientColor() const
{
    static Vector3f const white(1, 1, 1);

    if(d->ambientColorDefined || rendSkyLightAuto)
    {
        if(!d->ambientColorDefined)
        {
            d->updateAmbientColorIfNeeded();
        }
        return d->ambientColor;
    }
    return white;
}

void Sky::setAmbientColor(Vector3f const &newColor)
{
    d->ambientColor = newColor.min(Vector3f(1, 1, 1)).max(Vector3f(0, 0, 0));
    d->ambientColorDefined = true;
}

#endif // __CLIENT__
