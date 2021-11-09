/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2013 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup GHOST
 * Declaration of GHOST_Context class.
 */

#pragma once

#include "GHOST_IContext.h"
#include "GHOST_Types.h"

#include "glew-mx.h"

#include <cstdlib>  // for NULL

class GHOST_Context : public GHOST_IContext {
 public:
  /**
   * Constructor.
   * \param stereoVisual: Stereo visual for quad buffered stereo.
   */
  GHOST_Context(bool stereoVisual) : m_stereoVisual(stereoVisual)
  {
  }

  /**
   * Destructor.
   */
  virtual ~GHOST_Context()
  {
  }

  /**
   * Swaps front and back buffers of a window.
   * \return A boolean success indicator.
   */
  virtual GHOST_TSuccess swapBuffers() = 0;

  /**
   * Activates the drawing context of this window.
   * \return A boolean success indicator.
   */
  virtual GHOST_TSuccess activateDrawingContext() = 0;

  /**
   * Release the drawing context of the calling thread.
   * \return A boolean success indicator.
   */
  virtual GHOST_TSuccess releaseDrawingContext() = 0;

  /**
   * Call immediately after new to initialize.  If this fails then immediately delete the object.
   * \return Indication as to whether initialization has succeeded.
   */
  virtual GHOST_TSuccess initializeDrawingContext() = 0;

  /**
   * Updates the drawing context of this window. Needed
   * whenever the window is changed.
   * \return Indication of success.
   */
  virtual GHOST_TSuccess updateDrawingContext()
  {
    return GHOST_kFailure;
  }

  /**
   * Checks if it is OK for a remove the native display
   * \return Indication as to whether removal has succeeded.
   */
  virtual GHOST_TSuccess releaseNativeHandles() = 0;

  /**
   * Sets the swap interval for #swapBuffers.
   * \param interval: The swap interval to use.
   * \return A boolean success indicator.
   */
  virtual GHOST_TSuccess setSwapInterval(int /*interval*/)
  {
    return GHOST_kFailure;
  }

  /**
   * Gets the current swap interval for #swapBuffers.
   * \param intervalOut: Variable to store the swap interval if it can be read.
   * \return Whether the swap interval can be read.
   */
  virtual GHOST_TSuccess getSwapInterval(int &)
  {
    return GHOST_kFailure;
  }

  /**
   * Stereo visual created. Only necessary for 'real' stereo support,
   * ie quad buffered stereo. This is not always possible, depends on
   * the graphics h/w
   */
  inline bool isStereoVisual() const
  {
    return m_stereoVisual;
  }

  /**
   * Returns if the context is rendered upside down compared to OpenGL.
   */
  virtual inline bool isUpsideDown() const
  {
    return false;
  }

  /**
   * Gets the OpenGL frame-buffer associated with the OpenGL context
   * \return The ID of an OpenGL frame-buffer object.
   */
  virtual unsigned int getDefaultFramebuffer()
  {
    return 0;
  }

  /**
   * Gets the Vulkan context related resource handles.
   * \return  A boolean success indicator.
   */
  virtual GHOST_TSuccess getVulkanHandles(void * /*r_instance*/,
                                          void * /*r_physical_device*/,
                                          void * /*r_device*/,
                                          uint32_t * /*r_graphic_queue_familly*/)
  {
    return GHOST_kFailure;
  };

  /**
   * Gets the Vulkan framebuffer related resource handles associated with the Vulkan context.
   * Needs to be called after each swap events as the framebuffer will change.
   * \return  A boolean success indicator.
   */
  virtual GHOST_TSuccess getVulkanBackbuffer(void * /*image*/,
                                             void * /*framebuffer*/,
                                             void * /*command_buffer*/,
                                             void * /*render_pass*/,
                                             void * /*extent*/,
                                             uint32_t * /*fb_id*/)
  {
    return GHOST_kFailure;
  }

 protected:
  void initContextGLEW();

  bool m_stereoVisual;

  static void initClearGL();

#ifdef WITH_CXX_GUARDEDALLOC
  MEM_CXX_CLASS_ALLOC_FUNCS("GHOST:GHOST_Context")
#endif
};

#ifdef _WIN32
bool win32_chk(bool result, const char *file = NULL, int line = 0, const char *text = NULL);
bool win32_silent_chk(bool result);

#  ifndef NDEBUG
#    define WIN32_CHK(x) win32_chk((x), __FILE__, __LINE__, #    x)
#  else
#    define WIN32_CHK(x) win32_chk(x)
#  endif

#  define WIN32_CHK_SILENT(x, silent) ((silent) ? win32_silent_chk(x) : WIN32_CHK(x))
#endif /* _WIN32 */
