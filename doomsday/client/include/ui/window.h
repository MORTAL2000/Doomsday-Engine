/** @file window.h Window management. @ingroup base
 *
 * @authors Copyright © 2003-2013 Jaakko Keränen <jaakko.keranen@iki.fi>
 * @authors Copyright © 2005-2013 Daniel Swanson <danij@dengine.net>
 * @authors Copyright © 2008 Jamie Jones <jamie_jones_au@yahoo.com.au>
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

#ifndef LIBDENG_BASE_WINDOW_H
#define LIBDENG_BASE_WINDOW_H

#ifndef __CLIENT__
#  error "window.h requires __CLIENT__"
#endif

#include "dd_types.h"
#include "resource/image.h"
#include "canvaswindow.h"
#include <de/Error>

/// Minimum width of a window (fullscreen too? -ds)
#define WINDOW_MIN_WIDTH        320

/// Minimum height of a window (fullscreen too? -ds)
#define WINDOW_MIN_HEIGHT       240

/**
 * @defgroup doomsdayWindowFlags Doomsday window flags.
 */
///@{
#define DDWF_VISIBLE            0x01
#define DDWF_CENTERED           0x02
#define DDWF_MAXIMIZED          0x04
#define DDWF_FULLSCREEN         0x08
///@}

/**
 * Logical window attribute identifiers.
 */
enum windowattribute_e
{
    DDWA_END = 0, ///< Marks the end of an attribute list (not a valid attribute by itself)

    DDWA_X = 1,
    DDWA_Y,
    DDWA_WIDTH,
    DDWA_HEIGHT,
    DDWA_CENTERED,
    DDWA_MAXIMIZED,
    DDWA_FULLSCREEN,
    DDWA_VISIBLE,
    DDWA_COLOR_DEPTH_BITS
};

/// Determines whether @a x is a valid window attribute id.
#define VALID_WINDOW_ATTRIBUTE(x)   ((x) >= DDWA_X && (x) <= DDWA_VISIBLE)

/**
 * Window and window management.
 *
 * @deprecated  Windows will be represented by CanvasWindow instances.
 */
class Window
{
public:
    /// Required/referenced Window instance is missing. @ingroup errors
    DENG2_ERROR(MissingWindowError);

    /**
     * Initialize the window manager.
     * Tasks include; checking the system environment for feature enumeration.
     */
    static void initialize();

    /**
     * Shutdown the window manager.
     */
    static void shutdown();

    /**
     * Constructs a new window using the default configuration. Note that the
     * default configuration is saved persistently when the engine shuts down
     * and is restored when the engine is restarted.
     *
     * Command line options (e.g., -xpos) can be used to modify the window
     * configuration.
     *
     * @param title  Text for the window title.
     *
     * @note Ownership of the Window is @em not given to the caller.
     */
    static Window *create(char const *title);

    /**
     * Returns @c true iff a main window is available.
     */
    static bool haveMain();

    /**
     * Returns the main window.
     */
    static Window &main();

    /**
     * Returns a pointer to the @em main window.
     *
     * @see haveMain()
     */
    inline static Window *mainPtr() { return haveMain()? &main() : 0; }

    /**
     * Returns a pointer to the window associated with unique index @a idx.
     */
    static Window *byIndex(uint idx);

public:
    bool isFullscreen() const;

    bool isCentered() const;

    bool isMaximized() const;

    int x() const;

    int y() const;

    /**
     * Returns the current width of the window in pixels.
     */
    int width() const;

    /**
     * Returns the current height of the window in pixels.
     */
    int height() const;

    int normalX() const;

    int normalY() const;

    int normalWidth() const;

    int normalHeight() const;

    /**
     * Returns the dimensions of the window in pixels.
     */
    Size2Raw const &dimensions() const;

    int colorDepthBits() const;

    /**
     * Sets the title of a window.
     *
     * @param title         New title for the window.
     */
    void setTitle(char const *title) const; /// @todo semantically !const

    void show(bool show = true);

    /**
     * Sets or changes one or more window attributes.
     *
     * @param attribs  Array of values:
     *      <pre>[ attribId, value, attribId, value, ..., 0 ]</pre>
     *      The array must be zero-terminated, as that indicates where the array
     *      ends (see windowattribute_e).
     *
     * @return @c true, if the attributes were set and the window was successfully
     * updated. @c false, if there was an error with the values -- in this case all
     * the window's attributes remain unchanged.
     */
    bool changeAttributes(int *attribs);

    /**
     * Request drawing the contents of the window as soon as possible.
     */
    void draw();

    /**
     * Make the content of the framebuffer visible.
     */
    void swapBuffers() const;

    /**
     * Grab the contents of the window into an OpenGL texture.
     *
     * @param halfSized  If @c true, scales the image to half the full size.
     *
     * @return OpenGL texture name. Caller is reponsible for deleting the texture.
     */
    uint grabAsTexture(bool halfSized = false) const;

    /**
     * Grabs the contents of the window and saves it into an image file.
     *
     * @param fileName  Name of the file to save. May include a file extension
     *                  that indicates which format to use (e.g, "screenshot.jpg").
     *                  If omitted, defaults to PNG.
     *
     * @return @c true if successful, otherwise @c false.
     */
    bool grabToFile(char const *fileName) const;

    /**
     * Grab the contents of the window into an image.
     *
     * @param image  Grabbed image contents. Caller gets ownership; call GL_DestroyImage().
     */
    void grab(image_t *image, bool halfSized = false) const;

    /**
     * Saves the window's state into a persistent storage so that it can be later
     * on restored. Used at shutdown time to save window geometry.
     */
    void saveState();

    /**
     * Restores the window's state from persistent storage. Used at engine startup
     * to determine the default window geometry.
     */
    void restoreState();

    /**
     * Activates or deactivates the window mouse trap. When trapped, the mouse cursor is
     * not visible and all mouse motions are interpreted as deltas.
     *
     * @param enable  @c true, if the mouse is to be trapped in the window.
     *
     * @note Effectively a wrapper for Canvas::trapMouse().
     */
    void trapMouse(bool enable = true) const; /// @todo semantically !const

    bool isMouseTrapped() const;

    /**
     * Determines whether the contents of a window should be drawn during the
     * execution of the main loop callback, or should we wait for an update event
     * from the windowing system.
     */
    bool shouldRepaintManually() const;

    void updateCanvasFormat();

    /**
     * Activates the window's GL context so that OpenGL API calls can be made.
     * The GL context is automatically active during the drawing of the window's
     * contents; at other times it needs to be manually activated.
     */
    void glActivate();

    /**
     * Dectivates the window's GL context after OpenGL API calls have been done.
     * The GL context is automatically deactived after the drawing of the window's
     * contents; at other times it needs to be manually deactivated.
     */
    void glDone();

    void *nativeHandle() const;

    /**
     * Returns the window's native widget, if one exists.
     */
    QWidget *widget();

    CanvasWindow *canvasWindow();

    /**
     * Utility to call after changing the size of a CanvasWindow. This will update
     * the Window state.
     *
     * @deprecated In the future, size management will be done internally in
     * CanvasWindow/WindowSystem.
     */
    void updateAfterResize();

private:
    /**
     * Constructs a new window using the default configuration. Note that the
     * default configuration is saved persistently when the engine shuts down and
     * is restored when the engine is restarted.
     *
     * Command line options (e.g., -xpos) can be used to modify the window
     * configuration.
     *
     * @param title  Text for the window title.
     */
    Window(char const *title = "");

    /**
     * Close and destroy the window. Its state is saved persistently and used as
     * the default configuration the next time the same window is created.
     */
    ~Window();

private:
    DENG2_PRIVATE(d)
};

/**
 * Currently active window. There is always one active window, so no need
 * to worry about NULLs. The easiest way to get information about the
 * window where drawing is done.
 */
extern Window const *theWindow;

// A helpful macro that changes the origin of the screen
// coordinate system.
#define FLIP(y) (theWindow->height() - (y+1))

#endif // LIBDENG_BASE_WINDOW_H
