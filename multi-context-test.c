/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Neil Roberts <neil@linux.intel.com>
 *
 */

#include "config.h"

#include <epoxy/gl.h>
#include <stdbool.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct mct_window {
        Display *display;
        Window win;
        GLXContext context;
};

static bool
check_glx_extension(Display *display, const char *ext_name)
{
        int ext_len = strlen(ext_name);
        const char *extensions =
                glXQueryExtensionsString(display, DefaultScreen(display));
        const char *extensions_end;

        extensions_end = extensions + strlen(extensions);

        while (extensions < extensions_end) {
                const char *end = strchr(extensions, ' ');

                if (end == NULL)
                        end = extensions_end;

                if (end - extensions == ext_len &&
                    !memcmp(extensions, ext_name, ext_len))
                        return true;

                extensions = end + 1;
        }

        return false;
}

static GLXFBConfig
choose_fb_config(Display *display)
{
        GLXFBConfig *configs;
        int n_configs;
        GLXFBConfig ret;
        static const int attrib_list[] = {
                GLX_DOUBLEBUFFER, True,
                0
        };

        configs = glXChooseFBConfig(display, DefaultScreen(display),
                                    attrib_list, &n_configs);

        if (configs == NULL) {
                ret = NULL;
        } else {
                if (n_configs < 1)
                        ret = NULL;
                else
                        ret = configs[0];

                XFree(configs);
        }

        return ret;
}

static struct mct_window *
mct_window_new(Display *display, int width, int height)
{
        static const int context_attribs[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
                GLX_CONTEXT_MINOR_VERSION_ARB, 2,
                GLX_CONTEXT_PROFILE_MASK_ARB,
                GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                GLX_CONTEXT_FLAGS_ARB,
                GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
                None
        };
        GLXFBConfig fb_config;
        GLXContext ctx;
        int scrnum = 0;
        XSetWindowAttributes attr;
        unsigned long mask;
        Window root;
        XVisualInfo *visinfo;
        struct mct_window *window;
        PFNGLXCREATECONTEXTATTRIBSARBPROC create_context_attribs;

        if (!check_glx_extension(display, "GLX_ARB_create_context")) {
                fprintf(stderr,
                        "GLX_ARB_create_context is not supported\n");
                return NULL;
        }

        fb_config = choose_fb_config(display);

        if (fb_config == NULL) {
                fprintf(stderr,
                        "No suitable GLXFBConfig found\n");
                return NULL;
        }

        visinfo = glXGetVisualFromFBConfig(display, fb_config);

        if (visinfo == NULL) {
                fprintf(stderr,
                         "FB config does not have an associated visual\n");
                return NULL;
        }

        create_context_attribs =
                (void *) glXGetProcAddress((const GLubyte *)
                                           "glXCreateContextAttribsARB");
        ctx = create_context_attribs(display,
                                     fb_config,
                                     NULL, /* share_context */
                                     True, /* direct */
                                     context_attribs);

        if (ctx == NULL) {
                fprintf(stderr,
                        "Error: glXCreateContextAttribs failed\n");
                return NULL;
        }

        window = malloc(sizeof *window);

        root = RootWindow(display, scrnum);

        /* window attributes */
        attr.background_pixel = 0;
        attr.border_pixel = 0;
        attr.colormap =
            XCreateColormap(display, root, visinfo->visual, AllocNone);
        attr.event_mask =
            StructureNotifyMask | ExposureMask | PointerMotionMask |
            KeyPressMask;
        mask = CWBorderPixel | CWColormap | CWEventMask;

        window->win = XCreateWindow(display, root, 0, 0, width, height,
                                    0, visinfo->depth, InputOutput,
                                    visinfo->visual, mask, &attr);

        window->context = ctx;
        window->display = display;

        return window;
}

static void
mct_window_free(struct mct_window *window)
{
        glXDestroyContext(window->display, window->context);
        XDestroyWindow(window->display, window->win);
        free(window);
}

int
main(int argc, char **argv)
{
        struct mct_window *window;
        Display *display;

        display = XOpenDisplay(NULL);

        if (display == NULL) {
                fprintf(stderr, "XOpenDisplay failed\n");
                return EXIT_FAILURE;
        }

        window = mct_window_new(display, 640, 480);

        mct_window_free(window);

        XCloseDisplay(display);

        return EXIT_SUCCESS;
}
