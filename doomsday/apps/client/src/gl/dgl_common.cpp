/** @file dgl_common.cpp  Misc Drawing Routines
 *
 * @authors Copyright © 2004-2017 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2007-2015 Daniel Swanson <danij@dengine.net>
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
 * General Public License along with this program; if not, see:
 * http://www.gnu.org/licenses</small>
 */

#define DENG_NO_API_MACROS_GL

#include "de_base.h"
#include "gl/gl_main.h"

#include <cmath>
#include <cstdlib>
#include <de/concurrency.h>
#include <de/GLInfo>
#include <de/GLState>
#include <de/GLUniform>
#include <de/Matrix>
#include <doomsday/res/Textures>

#include "api_gl.h"
#include "gl/gl_defer.h"
#include "gl/gl_draw.h"
#include "gl/gl_texmanager.h"

#include "render/r_draw.h"

using namespace de;

struct DGLState
{
    int matrixMode = 0;
    QVector<Matrix4f> matrixStacks[4];

    int activeTexture = 0;
    bool enableTexture[2] { true, false };
    int textureModulation = 1;
    Vector4f textureModulationColor;

    bool enableFog = false;
    DGLenum fogMode = DGL_LINEAR;
    float fogStart = 0;
    float fogEnd = 0;
    float fogDensity = 0;
    Vector4f fogColor;
    bool flushBacktrace = false;

    DGLState()
    {
        // The matrix stacks initially contain identity matrices.
        for (auto &stack : matrixStacks)
        {
            stack.append(Matrix4f());
        }
    }

    int stackIndex(DGLenum id) const
    {
        switch (id)
        {
        case DGL_TEXTURE0:
            return 2;

        case DGL_TEXTURE1:
            return 3;

        case DGL_TEXTURE:
            return 2 + activeTexture;

        default: {
            int const index = int(id) - DGL_MODELVIEW;
            DENG2_ASSERT(index >= 0 && index < 2);
            return index; }
        }
    }

    void pushMatrix()
    {
        auto &stack = matrixStacks[matrixMode];
        stack.push_back(stack.back());
    }

    void popMatrix()
    {
        auto &stack = matrixStacks[matrixMode];
        DENG2_ASSERT(stack.size() > 1);
        stack.pop_back();
    }

    void loadMatrix(Matrix4f const &mat)
    {
        auto &stack = matrixStacks[matrixMode];
        DENG2_ASSERT(!stack.isEmpty());
        stack.back() = mat;
    }

    void multMatrix(Matrix4f const &mat)
    {
        auto &stack = matrixStacks[matrixMode];
        DENG2_ASSERT(!stack.isEmpty());
        stack.back() = stack.back() * mat;
    }
};

static DGLState dgl;

Matrix4f DGL_Matrix(DGLenum matrixMode)
{
    return dgl.matrixStacks[dgl.stackIndex(matrixMode)].back();
}

void DGL_SetModulationColor(Vector4f const &modColor)
{
    dgl.textureModulationColor = modColor;
}

Vector4f DGL_ModulationColor()
{
    return dgl.textureModulationColor;
}

void DGL_FogParams(GLUniform &fogRange, GLUniform &fogColor)
{
    if (dgl.enableFog)
    {
        fogColor = Vector4f(dgl.fogColor[0],
                            dgl.fogColor[1],
                            dgl.fogColor[2],
                            1.f);

        // TODO: Implement EXP and EXP2 fog modes. This is LINEAR.

        Rangef const depthPlanes = GL_DepthClipRange();
        float const fogDepth = dgl.fogEnd - dgl.fogStart;
        fogRange = Vector4f(dgl.fogStart,
                            fogDepth,
                            depthPlanes.start,
                            depthPlanes.end);
    }
    else
    {
        fogColor = Vector4f();
    }
}

void DGL_DepthFunc(DGLenum depthFunc)
{
    using namespace de::gl;

    static const Comparison funcs[] = {
        Never,
        Always,
        Equal,
        NotEqual,
        Less,
        Greater,
        LessOrEqual,
        GreaterOrEqual
    };

    DENG2_ASSERT(depthFunc >= DGL_NEVER && depthFunc <= DGL_GEQUAL);

    const auto f = funcs[depthFunc - DGL_NEVER];
    if (GLState::current().depthFunc() != f)
    {
        DGL_Flush();
        GLState::current().setDepthFunc(f);
    }
}

void DGL_CullFace(DGLenum cull)
{
    const auto c =
        ( cull == DGL_NONE  ? gl::None
        : cull == DGL_BACK  ? gl::Back
        : cull == DGL_FRONT ? gl::Front
                            : gl::None );

    if (GLState::current().cull() != c)
    {
        DGL_Flush();
        GLState::current().setCull(c);
    }
}

#if 0
/**
 * Requires a texture environment mode that can add and multiply.
 * Nvidia's and ATI's appropriate extensions are supported, other cards will
 * not be able to utilize multitextured lights.
 */
static void envAddColoredAlpha(int activate, GLenum addFactor)
{
    DENG_ASSERT_IN_MAIN_THREAD();
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    if(activate)
    {
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                  GLInfo::extensions().NV_texture_env_combine4? GL_COMBINE4_NV : GL_COMBINE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);

        // Combine: texAlpha * constRGB + 1 * prevRGB.
        if(GLInfo::extensions().NV_texture_env_combine4)
        {
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, addFactor);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_ZERO);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_ONE_MINUS_SRC_COLOR);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE3_RGB_NV, GL_PREVIOUS);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND3_RGB_NV, GL_SRC_COLOR);
        }
        else if(GLInfo::extensions().ATI_texture_env_combine3)
        {   // MODULATE_ADD_ATI: Arg0 * Arg2 + Arg1.
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE_ADD_ATI);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, addFactor);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_CONSTANT);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        }
        else
        {   // This doesn't look right.
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, addFactor);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        }
    }
    else
    {
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
}

/**
 * Setup the texture environment for single-pass multiplicative lighting.
 * The last texture unit is always used for the texture modulation.
 * TUs 1...n-1 are used for dynamic lights.
 */
static void envModMultiTex(int activate)
{
    DENG_ASSERT_IN_MAIN_THREAD();
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    // Setup TU 2: The modulated texture.
    LIBGUI_GL.glActiveTexture(GL_TEXTURE1);
    LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // Setup TU 1: The dynamic light.
    LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
    envAddColoredAlpha(activate, GL_SRC_ALPHA);

    // This is a single-pass mode. The alpha should remain unmodified
    // during the light stage.
    if(activate)
    {
        // Replace: primAlpha.
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
    }
}
#endif

void DGL_ModulateTexture(int mode)
{
    dgl.textureModulation = mode;

    switch (mode)
    {
    default:
        qDebug() << "DGL_ModulateTexture: texture modulation mode" << mode << "not implemented";
        break;

    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
        break;
    }

#if 0
    DENG_ASSERT_IN_MAIN_THREAD();
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    switch(mode)
    {
    case 0:
        // No modulation: just replace with texture.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        break;

    case 1:
        // Normal texture modulation with primary color.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        break;

    case 12:
        // Normal texture modulation on both stages. TU 1 modulates with
        // primary color, TU 2 with TU 1.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        break;

    case 2:
    case 3:
        // Texture modulation and interpolation.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
        if(mode == 2)
        {   // Used with surfaces that have a color.
            // TU 2: Modulate previous with primary color.
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PRIMARY_COLOR);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

        }
        else
        {   // Mode 3: Used with surfaces with no primary color.
            // TU 2: Pass through.
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        }
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

        // TU 1: Interpolate between texture 1 and 2, using the constant
        // alpha as the factor.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_CONSTANT);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);

        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        break;

    case 4:
        // Apply sector light, dynamic light and texture.
        envModMultiTex(true);
        break;

    case 5:
    case 10:
        // Sector light * texture + dynamic light.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE1);
        envAddColoredAlpha(true, mode == 5 ? GL_SRC_ALPHA : GL_SRC_COLOR);

        // Alpha remains unchanged.
        if(GLInfo::extensions().NV_texture_env_combine4)
        {
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_ADD);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_ZERO);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PREVIOUS);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE2_ALPHA, GL_ZERO);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE3_ALPHA_NV, GL_ZERO);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND3_ALPHA_NV, GL_SRC_ALPHA);
        }
        else
        {
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        }

        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        break;

    case 6:
        // Simple dynlight addition (add to primary color).
        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        envAddColoredAlpha(true, GL_SRC_ALPHA);
        break;

    case 7:
        // Dynlight addition without primary color.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_ALPHA);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
        break;

    case 8:
    case 9:
        // Texture and Detail.
        LIBGUI_GL.glActiveTexture(GL_TEXTURE1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 2);

        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        if(mode == 8)
        {
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
        else
        {   // Mode 9: Ignore primary color.
            LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        }
        break;

    case 11:
        // Normal modulation, alpha of 2nd stage.
        // Tex0: texture
        // Tex1: shiny texture
        LIBGUI_GL.glActiveTexture(GL_TEXTURE1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

        LIBGUI_GL.glActiveTexture(GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_TEXTURE1);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_TEXTURE0);
        LIBGUI_GL.glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        break;

    default:
        break;
    }
#endif
}

/*void GL_BlendOp(int op)
{
    if(!GL_state.features.blendSubtract)
        return;

    DENG_ASSERT_IN_MAIN_THREAD();
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    LIBGUI_GL.glBlendEquationEXT(op);
}*/

void GL_SetVSync(dd_bool on)
{
    // Outside the main thread we'll need to defer the call.
    if (!Sys_InMainThread())
    {
        GL_DeferSetVSync(on);
        return;
    }

    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    GLInfo::setSwapInterval(on ? 1 : 0);
}

#undef DGL_SetScissor
DENG_EXTERN_C void DGL_SetScissor(RectRaw const *rect)
{
    if(!rect) return;

    DENG2_ASSERT_IN_RENDER_THREAD();
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    GameWidget &game = ClientWindow::main().game();

    // Note that the game is unaware of the game widget position, assuming that (0,0)
    // is the top left corner of the drawing area. Fortunately, the current viewport
    // has been set to cover the game widget area, so we can set the scissor relative
    // to it.

    auto const norm = GuiWidget::normalizedRect(Rectanglei(rect->origin.x, rect->origin.y,
                                                           rect->size.width, rect->size.height),
                                                Rectanglei::fromSize(game.rule().recti().size()));

    DGL_Flush();
    GLState::current().setNormalizedScissor(norm);
}

#undef DGL_SetScissor2
DENG_EXTERN_C void DGL_SetScissor2(int x, int y, int width, int height)
{
    RectRaw rect;
    rect.origin.x = x;
    rect.origin.y = y;
    rect.size.width  = width;
    rect.size.height = height;
    DGL_SetScissor(&rect);
}

#undef DGL_GetIntegerv
dd_bool DGL_GetIntegerv(int name, int *v)
{
    //DENG_ASSERT_IN_MAIN_THREAD();
    //DENG_ASSERT_GL_CONTEXT_ACTIVE();

    float color[4];
    switch(name)
    {
    case DGL_ACTIVE_TEXTURE:
        *v = dgl.activeTexture;
        break;

    case DGL_TEXTURE_2D:
        *v = (dgl.enableTexture[dgl.activeTexture]? 1 : 0);
        break;

    case DGL_TEXTURE0:
        *v = dgl.enableTexture[0]? 1 : 0;
        break;

    case DGL_TEXTURE1:
        *v = dgl.enableTexture[1]? 1 : 0;
        break;

    case DGL_MODULATE_TEXTURE:
        *v = dgl.textureModulation;
        break;

//    case DGL_MODULATE_ADD_COMBINE:
//        qDebug() << "DGL_GetIntegerv: tex env not available";
//        //*v = GLInfo::extensions().NV_texture_env_combine4 || GLInfo::extensions().ATI_texture_env_combine3;
//        break;

    case DGL_SCISSOR_TEST:
        *(GLint *) v = GLState::current().scissor();
        break;

    case DGL_FOG:
        *v = (dgl.enableFog? 1 : 0);
        break;

    case DGL_FOG_MODE:
        *v = int(dgl.fogMode);
        break;

    case DGL_CURRENT_COLOR_R:
        DGL_CurrentColor(color);
        *v = int(color[0] * 255);
        break;

    case DGL_CURRENT_COLOR_G:
        DGL_CurrentColor(color);
        *v = int(color[1] * 255);
        break;

    case DGL_CURRENT_COLOR_B:
        DGL_CurrentColor(color);
        *v = int(color[2] * 255);
        break;

    case DGL_CURRENT_COLOR_A:
        DGL_CurrentColor(color);
        *v = int(color[3] * 255);
        break;

    case DGL_CURRENT_COLOR_RGBA:
        DGL_CurrentColor(color);
        for (int i = 0; i < 4; ++i)
        {
            v[i] = int(color[i] * 255);
        }
        break;

    case DGL_FLUSH_BACKTRACE:
        *v = dgl.flushBacktrace ? 1 : 0;
        break;

    default:
        return false;
    }

    return true;
}

#undef DGL_GetInteger
int DGL_GetInteger(int name)
{
    int values[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    DGL_GetIntegerv(name, values);
    return values[0];
}

#undef DGL_SetInteger
dd_bool DGL_SetInteger(int name, int value)
{
    switch(name)
    {
    case DGL_ACTIVE_TEXTURE:
        DENG_ASSERT_GL_CONTEXT_ACTIVE();
        DENG2_ASSERT(value >= 0);
        DENG2_ASSERT(value < MAX_TEX_UNITS);
        dgl.activeTexture = value;
        LIBGUI_GL.glActiveTexture(GLenum(GL_TEXTURE0 + value));
        break;

    case DGL_MODULATE_TEXTURE:
        DGL_ModulateTexture(value);
        break;

    case DGL_FLUSH_BACKTRACE:
        dgl.flushBacktrace = true;
        break;

    default:
        return false;
    }

    return true;
}

#undef DGL_GetFloatv
dd_bool DGL_GetFloatv(int name, float *v)
{
    //DENG_ASSERT_IN_MAIN_THREAD();
    //DENG_ASSERT_GL_CONTEXT_ACTIVE();

    float color[4];
    switch (name)
    {
    case DGL_CURRENT_COLOR_R:
        DGL_CurrentColor(color);
        *v = color[0];
        break;

    case DGL_CURRENT_COLOR_G:
        DGL_CurrentColor(color);
        *v = color[1];
        break;

    case DGL_CURRENT_COLOR_B:
        DGL_CurrentColor(color);
        *v = color[2];
        break;

    case DGL_CURRENT_COLOR_A:
        DGL_CurrentColor(color);
        *v = color[3];
        break;

    case DGL_CURRENT_COLOR_RGBA:
        DGL_CurrentColor(v);
        break;

    case DGL_FOG_START:
        v[0] = dgl.fogStart;
        break;

    case DGL_FOG_END:
        v[0] = dgl.fogEnd;
        break;

    case DGL_FOG_DENSITY:
        v[0] = dgl.fogDensity;
        break;

    case DGL_FOG_COLOR:
        for (int i = 0; i < 4; ++i)
        {
            v[i] = dgl.fogColor[i];
        }
        break;

    case DGL_LINE_WIDTH:
        v[0] = GL_state.currentLineWidth;
        break;

    case DGL_POINT_SIZE:
        v[0] = GL_state.currentPointSize;
        break;

    default:
        return false;
    }

    return true;
}

#undef DGL_GetFloat
float DGL_GetFloat(int name)
{
    float value = 0.f;
    DGL_GetFloatv(name, &value);
    return value;
}

#undef DGL_SetFloat
dd_bool DGL_SetFloat(int name, float value)
{
    switch(name)
    {
    case DGL_LINE_WIDTH:
        if (!fequal(value, GL_state.currentLineWidth))
        {
            DGL_Flush();
            GL_state.currentLineWidth = value;
        }
        break;

    case DGL_POINT_SIZE:
        GL_state.currentPointSize = value;
#if defined (DENG_OPENGL)
        LIBGUI_ASSERT_GL_CONTEXT_ACTIVE();
        LIBGUI_GL.glPointSize(value);
#endif
        break;

    case DGL_ALPHA_LIMIT:
        // No flushing required.
        GLState::current().setAlphaLimit(value);
        break;

    default:
        return false;
    }

    return true;
}

#undef DGL_PushState
void DGL_PushState(void)
{
    DGL_Flush();
    GLState::push();
}

#undef DGL_PopState
void DGL_PopState(void)
{
    DGL_Flush();
    GLState::pop();
}

#undef DGL_Enable
int DGL_Enable(int cap)
{
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    switch (cap)
    {
        case DGL_BLEND:
            if (!GLState::current().blend())
            {
                DGL_Flush();
                GLState::current().setBlend(true);
            }
            break;

        case DGL_ALPHA_TEST:
            // No flushing required.
            GLState::current().setAlphaTest(true);
            break;

        case DGL_DEPTH_TEST:
            if (!GLState::current().depthTest())
            {
                DGL_Flush();
                GLState::current().setDepthTest(true);
            }
            break;

        case DGL_DEPTH_WRITE:
            if (!GLState::current().depthWrite())
            {
                DGL_Flush();
                GLState::current().setDepthWrite(true);
            }
            break;

        case DGL_TEXTURE_2D: dgl.enableTexture[dgl.activeTexture] = true; break;

        case DGL_TEXTURE0:
            DGL_SetInteger(DGL_ACTIVE_TEXTURE, 0);
            dgl.enableTexture[0] = true;
            break;

        case DGL_TEXTURE1:
            DGL_SetInteger(DGL_ACTIVE_TEXTURE, 1);
            dgl.enableTexture[1] = true;
            break;

        case DGL_FOG:
            if (!dgl.enableFog)
            {
                DGL_Flush();
                dgl.enableFog = true;
            }
            break;

        case DGL_SCISSOR_TEST:
            //glEnable(GL_SCISSOR_TEST);
            break;

        case DGL_LINE_SMOOTH:
#if defined(DENG_OPENGL)
            Deferred_glEnable(GL_LINE_SMOOTH);
#endif
            break;

        case DGL_POINT_SMOOTH:
            //Deferred_glEnable(GL_POINT_SMOOTH);
            // TODO: Not needed?
            break;

        default: DENG_ASSERT(!"DGL_Enable: Invalid cap"); return 0;
    }

    LIBGUI_ASSERT_GL_OK();
    return 1;
}

#undef DGL_Disable
void DGL_Disable(int cap)
{
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    switch (cap)
    {
        case DGL_BLEND:
            if (GLState::current().blend())
            {
                DGL_Flush();
                GLState::current().setBlend(false);
            }
            break;

        case DGL_DEPTH_TEST:
            if (GLState::current().depthTest())
            {
                DGL_Flush();
                GLState::current().setDepthTest(false);
            }
            break;

        case DGL_DEPTH_WRITE:
            if (GLState::current().depthWrite())
            {
                DGL_Flush();
                GLState::current().setDepthWrite(false);
            }
            break;

        case DGL_ALPHA_TEST:
            // No flushing required.
            GLState::current().setAlphaTest(false);
            break;

        case DGL_TEXTURE_2D: dgl.enableTexture[dgl.activeTexture] = false; break;

        case DGL_TEXTURE0:
            DGL_SetInteger(DGL_ACTIVE_TEXTURE, 0);
            dgl.enableTexture[0] = false;
            break;

        case DGL_TEXTURE1:
            DGL_SetInteger(DGL_ACTIVE_TEXTURE, 1);
            dgl.enableTexture[1] = false;
            break;

        case DGL_FOG:
            if (dgl.enableFog)
            {
                DGL_Flush();
                dgl.enableFog = false;
            }
            break;

        case DGL_SCISSOR_TEST:
            DGL_Flush();
            GLState::current().clearScissor();
            break;

        case DGL_LINE_SMOOTH:
#if defined(DENG_OPENGL)
            Deferred_glDisable(GL_LINE_SMOOTH);
#endif
            break;

        case DGL_POINT_SMOOTH:
#if defined(DENG_OPENGL)
            Deferred_glDisable(GL_POINT_SMOOTH);
#endif
            break;

        default: DENG_ASSERT(!"DGL_Disable: Invalid cap"); break;
    }

    LIBGUI_ASSERT_GL_OK();
}

#undef DGL_BlendOp
void DGL_BlendOp(int op)
{
    const auto glop = op == DGL_SUBTRACT ? gl::Subtract :
                                           op == DGL_REVERSE_SUBTRACT ? gl::ReverseSubtract :
                                                                        gl::Add;
    if (GLState::current().blendOp() != glop)
    {
        DGL_Flush();
        GLState::current().setBlendOp(glop);
    }
}

#undef DGL_BlendFunc
void DGL_BlendFunc(int param1, int param2)
{
    DENG2_ASSERT_IN_RENDER_THREAD();
    DENG_ASSERT_GL_CONTEXT_ACTIVE();

    const auto src = param1 == DGL_ZERO ? gl::Zero :
                                          param1 == DGL_ONE                 ? gl::One  :
                                          param1 == DGL_DST_COLOR           ? gl::DestColor :
                                          param1 == DGL_ONE_MINUS_DST_COLOR ? gl::OneMinusDestColor :
                                          param1 == DGL_SRC_ALPHA           ? gl::SrcAlpha :
                                          param1 == DGL_ONE_MINUS_SRC_ALPHA ? gl::OneMinusSrcAlpha :
                                          param1 == DGL_DST_ALPHA           ? gl::DestAlpha :
                                          param1 == DGL_ONE_MINUS_DST_ALPHA ? gl::OneMinusDestAlpha :
                                                                              gl::Zero;

    const auto dst = param2 == DGL_ZERO ? gl::Zero :
                                          param2 == DGL_ONE                 ? gl::One :
                                          param2 == DGL_SRC_COLOR           ? gl::SrcColor :
                                          param2 == DGL_ONE_MINUS_SRC_COLOR ? gl::OneMinusSrcColor :
                                          param2 == DGL_SRC_ALPHA           ? gl::SrcAlpha :
                                          param2 == DGL_ONE_MINUS_SRC_ALPHA ? gl::OneMinusSrcAlpha :
                                          param2 == DGL_DST_ALPHA           ? gl::DestAlpha :
                                          param2 == DGL_ONE_MINUS_DST_ALPHA ? gl::OneMinusDestAlpha :
                                                                              gl::Zero;

    auto &st = GLState::current();
    if (st.blendFunc() != gl::BlendFunc(src, dst))
    {
        DGL_Flush();
        GLState::current().setBlendFunc(src, dst);
    }
}

#undef DGL_BlendMode
void DGL_BlendMode(blendmode_t mode)
{
    GL_BlendMode(mode);
}

#undef DGL_SetNoMaterial
void DGL_SetNoMaterial(void)
{
    GL_SetNoTexture();
}

static gl::Wrapping DGL_ToGLWrapCap(DGLint cap)
{
    switch(cap)
    {
    case DGL_CLAMP:
    case DGL_CLAMP_TO_EDGE:
        return gl::ClampToEdge;

    case DGL_REPEAT:
        return gl::Repeat;

    default:
        DENG2_ASSERT(!"DGL_ToGLWrapCap: Unknown cap value");
        break;
    }
    return gl::ClampToEdge;
}

#undef DGL_SetMaterialUI
void DGL_SetMaterialUI(world_Material *mat, DGLint wrapS, DGLint wrapT)
{
    GL_SetMaterialUI2(reinterpret_cast<world::Material *>(mat),
                      DGL_ToGLWrapCap(wrapS),
                      DGL_ToGLWrapCap(wrapT));
}

#undef DGL_SetPatch
void DGL_SetPatch(patchid_t id, DGLint wrapS, DGLint wrapT)
{
    try
    {
        res::TextureManifest &manifest = res::Textures::get().textureScheme("Patches").findByUniqueId(id);
        if(!manifest.hasTexture()) return;

        res::Texture &tex = manifest.texture();
        TextureVariantSpec const &texSpec =
            Rend_PatchTextureSpec(0 | (tex.isFlagged(res::Texture::Monochrome)        ? TSF_MONOCHROME : 0)
                                    | (tex.isFlagged(res::Texture::UpscaleAndSharpen) ? TSF_UPSCALE_AND_SHARPEN : 0),
                                  DGL_ToGLWrapCap(wrapS), DGL_ToGLWrapCap(wrapT));
        GL_BindTexture(static_cast<ClientTexture &>(tex).prepareVariant(texSpec));
    }
    catch(res::TextureScheme::NotFoundError const &er)
    {
        // Log but otherwise ignore this error.
        LOG_RES_WARNING("Cannot use patch ID %i: %s") << id << er.asText();
    }
}

#undef DGL_SetPSprite
void DGL_SetPSprite(world_Material *mat)
{
    GL_SetPSprite(reinterpret_cast<world::Material *>(mat), 0, 0);
}

#undef DGL_SetPSprite2
void DGL_SetPSprite2(world_Material *mat, int tclass, int tmap)
{
    GL_SetPSprite(reinterpret_cast<world::Material *>(mat), tclass, tmap);
}

#undef DGL_SetRawImage
void DGL_SetRawImage(lumpnum_t lumpNum, DGLint wrapS, DGLint wrapT)
{
    GL_SetRawImage(lumpNum, DGL_ToGLWrapCap(wrapS), DGL_ToGLWrapCap(wrapT));
}

#undef DGL_MatrixMode
void DGL_MatrixMode(DGLenum mode)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.matrixMode = dgl.stackIndex(mode);
}

#undef DGL_PushMatrix
void DGL_PushMatrix(void)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.pushMatrix();
}

#undef DGL_PopMatrix
void DGL_PopMatrix(void)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.popMatrix();
}

#undef DGL_LoadIdentity
void DGL_LoadIdentity(void)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.loadMatrix(Matrix4f());
}

#undef DGL_LoadMatrix
void DGL_LoadMatrix(float const *matrix4x4)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.loadMatrix(Matrix4f(matrix4x4));
}

#undef DGL_Translatef
void DGL_Translatef(float x, float y, float z)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.multMatrix(Matrix4f::translate(Vector3f(x, y, z)));
}

#undef DGL_Rotatef
void DGL_Rotatef(float angle, float x, float y, float z)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.multMatrix(Matrix4f::rotate(angle, Vector3f(x, y, z)));
}

#undef DGL_Scalef
void DGL_Scalef(float x, float y, float z)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.multMatrix(Matrix4f::scale(Vector3f(x, y, z)));
}

#undef DGL_Ortho
void DGL_Ortho(float left, float top, float right, float bottom, float znear, float zfar)
{
    //DENG2_ASSERT_IN_RENDER_THREAD();

    dgl.multMatrix(Matrix4f::ortho(left, right, top, bottom, znear, zfar));
}

#undef DGL_Fogi
void DGL_Fogi(DGLenum property, int value)
{
    switch (property)
    {
    case DGL_FOG_MODE:
        dgl.fogMode = DGLenum(value);
        break;
    }
}

#undef DGL_Fogfv
void DGL_Fogfv(DGLenum property, float const *values)
{
    switch (property)
    {
    case DGL_FOG_START:
        dgl.fogStart = values[0];
        break;

    case DGL_FOG_END:
        dgl.fogEnd = values[0];
        break;

    case DGL_FOG_DENSITY:
        dgl.fogDensity = values[0];
        break;

    case DGL_FOG_COLOR:
        dgl.fogColor = Vector4f(values);
        break;
    }
}

#undef DGL_Fogf
void DGL_Fogf(DGLenum property, float value)
{
    DGL_Fogfv(property, &value);
}

#undef DGL_DeleteTextures
void DGL_DeleteTextures(int num, DGLuint const *names)
{
    if(!num || !names) return;

    Deferred_glDeleteTextures(num, names);
}

#undef DGL_Bind
int DGL_Bind(DGLuint texture)
{
    GL_BindTextureUnmanaged(texture);
    DENG_ASSERT(!Sys_GLCheckError());
    return 0;
}

#undef DGL_NewTextureWithParams
DGLuint DGL_NewTextureWithParams(dgltexformat_t format, int width, int height,
    uint8_t const *pixels, int flags, int minFilter, int magFilter,
    int anisoFilter, int wrapS, int wrapT)
{
#if defined (DENG_OPENGL_ES)
#  define GL_CLAMP GL_CLAMP_TO_EDGE
#endif
    return GL_NewTextureWithParams(format, width, height, (uint8_t *)pixels, flags, 0,
                                    (minFilter == DGL_LINEAR                 ? GL_LINEAR :
                                     minFilter == DGL_NEAREST                ? GL_NEAREST :
                                     minFilter == DGL_NEAREST_MIPMAP_NEAREST ? GL_NEAREST_MIPMAP_NEAREST :
                                     minFilter == DGL_LINEAR_MIPMAP_NEAREST  ? GL_LINEAR_MIPMAP_NEAREST :
                                     minFilter == DGL_NEAREST_MIPMAP_LINEAR  ? GL_NEAREST_MIPMAP_LINEAR :
                                                                               GL_LINEAR_MIPMAP_LINEAR),
                                    (magFilter == DGL_LINEAR                 ? GL_LINEAR : GL_NEAREST),
                                    anisoFilter,
                                    (wrapS == DGL_CLAMP         ? GL_CLAMP :
                                     wrapS == DGL_CLAMP_TO_EDGE ? GL_CLAMP_TO_EDGE : GL_REPEAT),
                                    (wrapT == DGL_CLAMP         ? GL_CLAMP :
                                     wrapT == DGL_CLAMP_TO_EDGE ? GL_CLAMP_TO_EDGE : GL_REPEAT));
#if defined (DENG_OPENGL_ES)
#  undef GL_CLAMP
#endif
}

// dgl_draw.cpp
DENG_EXTERN_C void DGL_Begin(dglprimtype_t mode);
DENG_EXTERN_C void DGL_End(void);
DENG_EXTERN_C void DGL_Color3ub(DGLubyte r, DGLubyte g, DGLubyte b);
DENG_EXTERN_C void DGL_Color3ubv(const DGLubyte* vec);
DENG_EXTERN_C void DGL_Color4ub(DGLubyte r, DGLubyte g, DGLubyte b, DGLubyte a);
DENG_EXTERN_C void DGL_Color4ubv(const DGLubyte* vec);
DENG_EXTERN_C void DGL_Color3f(float r, float g, float b);
DENG_EXTERN_C void DGL_Color3fv(const float* vec);
DENG_EXTERN_C void DGL_Color4f(float r, float g, float b, float a);
DENG_EXTERN_C void DGL_Color4fv(const float* vec);
DENG_EXTERN_C void DGL_TexCoord2f(byte target, float s, float t);
DENG_EXTERN_C void DGL_TexCoord2fv(byte target, float const *vec);
DENG_EXTERN_C void DGL_Vertex2f(float x, float y);
DENG_EXTERN_C void DGL_Vertex2fv(const float* vec);
DENG_EXTERN_C void DGL_Vertex3f(float x, float y, float z);
DENG_EXTERN_C void DGL_Vertex3fv(const float* vec);
DENG_EXTERN_C void DGL_Vertices2ftv(int num, const dgl_ft2vertex_t* vec);
DENG_EXTERN_C void DGL_Vertices3ftv(int num, const dgl_ft3vertex_t* vec);
DENG_EXTERN_C void DGL_Vertices3fctv(int num, const dgl_fct3vertex_t* vec);
DENG_EXTERN_C void DGL_DrawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a);
DENG_EXTERN_C void DGL_DrawRect(const RectRaw* rect);
DENG_EXTERN_C void DGL_DrawRect2(int x, int y, int w, int h);
DENG_EXTERN_C void DGL_DrawRectf(const RectRawf* rect);
DENG_EXTERN_C void DGL_DrawRectf2(double x, double y, double w, double h);
DENG_EXTERN_C void DGL_DrawRectf2Color(double x, double y, double w, double h, float r, float g, float b, float a);
DENG_EXTERN_C void DGL_DrawRectf2Tiled(double x, double y, double w, double h, int tw, int th);
DENG_EXTERN_C void DGL_DrawCutRectfTiled(const RectRawf* rect, int tw, int th, int txoff, int tyoff, const RectRawf* cutRect);
DENG_EXTERN_C void DGL_DrawCutRectf2Tiled(double x, double y, double w, double h, int tw, int th, int txoff, int tyoff, double cx, double cy, double cw, double ch);
DENG_EXTERN_C void DGL_DrawQuadOutline(const Point2Raw* tl, const Point2Raw* tr, const Point2Raw* br, const Point2Raw* bl, const float color[4]);
DENG_EXTERN_C void DGL_DrawQuad2Outline(int tlX, int tlY, int trX, int trY, int brX, int brY, int blX, int blY, const float color[4]);

// gl_draw.cpp
DENG_EXTERN_C void GL_UseFog(int yes);
DENG_EXTERN_C void GL_SetFilter(dd_bool enable);
DENG_EXTERN_C void GL_SetFilterColor(float r, float g, float b, float a);
DENG_EXTERN_C void GL_ConfigureBorderedProjection2(dgl_borderedprojectionstate_t* bp, int flags, int width, int height, int availWidth, int availHeight, scalemode_t overrideMode, float stretchEpsilon);
DENG_EXTERN_C void GL_ConfigureBorderedProjection(dgl_borderedprojectionstate_t* bp, int flags, int width, int height, int availWidth, int availHeight, scalemode_t overrideMode);
DENG_EXTERN_C void GL_BeginBorderedProjection(dgl_borderedprojectionstate_t* bp);
DENG_EXTERN_C void GL_EndBorderedProjection(dgl_borderedprojectionstate_t* bp);
DENG_EXTERN_C void GL_ResetViewEffects();

DENG_DECLARE_API(GL) =
{
    { DE_API_GL },
    DGL_Enable,
    DGL_Disable,
    DGL_PushState,
    DGL_PopState,
    DGL_GetIntegerv,
    DGL_GetInteger,
    DGL_SetInteger,
    DGL_GetFloatv,
    DGL_GetFloat,
    DGL_SetFloat,
    DGL_Ortho,
    DGL_SetScissor,
    DGL_SetScissor2,
    DGL_MatrixMode,
    DGL_PushMatrix,
    DGL_PopMatrix,
    DGL_LoadIdentity,
    DGL_LoadMatrix,
    DGL_Translatef,
    DGL_Rotatef,
    DGL_Scalef,
    DGL_Begin,
    DGL_End,
    DGL_SetNoMaterial,
    DGL_SetMaterialUI,
    DGL_SetPatch,
    DGL_SetPSprite,
    DGL_SetPSprite2,
    DGL_SetRawImage,
    DGL_BlendOp,
    DGL_BlendFunc,
    DGL_BlendMode,
    DGL_Color3ub,
    DGL_Color3ubv,
    DGL_Color4ub,
    DGL_Color4ubv,
    DGL_Color3f,
    DGL_Color3fv,
    DGL_Color4f,
    DGL_Color4fv,
    DGL_TexCoord2f,
    DGL_TexCoord2fv,
    DGL_Vertex2f,
    DGL_Vertex2fv,
    DGL_Vertex3f,
    DGL_Vertex3fv,
    DGL_Vertices2ftv,
    DGL_Vertices3ftv,
    DGL_Vertices3fctv,
    DGL_DrawLine,
    DGL_DrawRect,
    DGL_DrawRect2,
    DGL_DrawRectf,
    DGL_DrawRectf2,
    DGL_DrawRectf2Color,
    DGL_DrawRectf2Tiled,
    DGL_DrawCutRectfTiled,
    DGL_DrawCutRectf2Tiled,
    DGL_DrawQuadOutline,
    DGL_DrawQuad2Outline,
    DGL_NewTextureWithParams,
    DGL_Bind,
    DGL_DeleteTextures,
    DGL_Fogi,
    DGL_Fogf,
    DGL_Fogfv,
    GL_UseFog,
    GL_SetFilter,
    GL_SetFilterColor,
    GL_ConfigureBorderedProjection2,
    GL_ConfigureBorderedProjection,
    GL_BeginBorderedProjection,
    GL_EndBorderedProjection,
    GL_ResetViewEffects,
};
