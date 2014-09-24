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
#include <math.h>
#include <sys/time.h>

#include "shader-data.h"

#define GRID_WIDTH 100
#define GRID_HEIGHT 100

struct mct_window {
        Display *display;
        Window win;
        GLXContext context;
        GLXWindow glx_window;
};

struct mct_draw_state {
        GLuint grid_buffer;
        GLuint grid_array;

        GLuint prog;

        GLuint band_pos_location;
};

struct mct_vertex {
        float x, y;
        float distance;
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

static void
mct_window_make_current(struct mct_window *window)
{
        glXMakeCurrent(window->display, window->glx_window, window->context);
}

static void
mct_window_swap(struct mct_window *window)
{
        mct_window_make_current(window);
        glXSwapBuffers(window->display, window->glx_window);
}

static struct mct_window *
mct_window_new(Display *display, int width, int height)
{
        static const int context_attribs[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
                GLX_CONTEXT_MINOR_VERSION_ARB, 3,
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

        window->glx_window = glXCreateWindow(display, fb_config,
                                             window->win, NULL);

        window->context = ctx;
        window->display = display;

        return window;
}

static void
mct_window_free(struct mct_window *window)
{
        glXDestroyContext(window->display, window->context);
        glXDestroyWindow(window->display, window->glx_window);
        XDestroyWindow(window->display, window->win);
        free(window);
}

static void
make_grid(GLuint *buffer,
          GLuint *array,
          int width,
          int height)
{
        struct mct_vertex *vertex;
        float sw, sh;
        float blx, bly;
        float cx, cy;
        float distance;
        int x, y;

        glGenBuffers(1, buffer);
        glBindBuffer(GL_ARRAY_BUFFER, *buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof (struct mct_vertex) * width * height * 4,
                     NULL,
                     GL_STATIC_DRAW);
        vertex = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

        sw = 2.0f / width;
        sh = 2.0f / height;

        for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++) {
                        blx = x * 2.0f / width - 1.0f;
                        bly = y * 2.0f / height - 1.0f;

                        cx = blx + sw / 2.0f;
                        cy = bly + sh / 2.0f;
                        distance = sqrtf(cx * cx + cy * cy);

                        vertex[0].x = blx;
                        vertex[0].y = bly;
                        vertex[0].distance = distance;

                        vertex[1].x = blx + sw;
                        vertex[1].y = bly;
                        vertex[1].distance = distance;

                        vertex[2].x = blx;
                        vertex[2].y = bly + sh;
                        vertex[2].distance = distance;

                        vertex[3].x = blx + sw;
                        vertex[3].y = bly + sh;
                        vertex[3].distance = distance;

                        vertex += 4;
                }
        }

        glUnmapBuffer(GL_ARRAY_BUFFER);

        glGenVertexArrays(1, array);
        glBindVertexArray(*array);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, /* index */
                              2, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* normalized */
                              sizeof (struct mct_vertex),
                              (void *) offsetof(struct mct_vertex, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, /* index */
                              1, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* normalized */
                              sizeof (struct mct_vertex),
                              (void *) offsetof(struct mct_vertex, distance));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
}

static struct mct_draw_state *
mct_draw_state_new(void)
{
        struct mct_draw_state *draw_state;
        GLuint prog;

        prog = shader_data_load_program(GL_VERTEX_SHADER,
                                        "vertex-shader.glsl",
                                        GL_FRAGMENT_SHADER,
                                        "fragment-shader.glsl",
                                        GL_NONE);

        if (prog == 0)
                return NULL;

        draw_state = malloc(sizeof *draw_state);

        make_grid(&draw_state->grid_buffer,
                  &draw_state->grid_array,
                  GRID_WIDTH, GRID_HEIGHT);

        draw_state->prog = prog;

        draw_state->band_pos_location =
                glGetUniformLocation(prog, "band_pos");

        return draw_state;
}

static void
mct_draw_state_start(struct mct_draw_state *draw_state)
{
        struct timeval tv;

        glBindVertexArray(draw_state->grid_array);
        glUseProgram(draw_state->prog);

        gettimeofday(&tv, NULL);

        glUniform1f(draw_state->band_pos_location,
                    tv.tv_usec / 1000000.0f);
}

static void
mct_draw_state_draw_rectangle(struct mct_draw_state *draw_state,
                              int x, int y)
{
        glDrawArrays(GL_TRIANGLE_STRIP,
                     (y * GRID_WIDTH + x) * 4,
                     4 /* count */);
}

static void
mct_draw_state_end(struct mct_draw_state *draw_state)
{
        glUseProgram(0);
        glBindVertexArray(0);
}

static void
mct_draw_state_free(struct mct_draw_state *draw_state)
{
        glDeleteVertexArrays(1, &draw_state->grid_array);
        glDeleteBuffers(1, &draw_state->grid_buffer);
        glDeleteProgram(draw_state->prog);

        free(draw_state);
}

static void
draw_all(struct mct_draw_state *draw_state)
{
        int x, y;

        mct_draw_state_start(draw_state);

        for (y = 0; y < GRID_HEIGHT; y++) {
                for (x = 0; x < GRID_WIDTH; x++) {
                        mct_draw_state_draw_rectangle(draw_state, x, y);
                }
        }

        mct_draw_state_end(draw_state);
}

int
main(int argc, char **argv)
{
        struct mct_window *window;
        struct mct_draw_state *draw_state;
        Display *display;

        display = XOpenDisplay(NULL);

        if (display == NULL) {
                fprintf(stderr, "XOpenDisplay failed\n");
                return EXIT_FAILURE;
        }

        window = mct_window_new(display, 640, 640);

        if (window) {
                mct_window_make_current(window);

                draw_state = mct_draw_state_new();

                XMapWindow(display, window->win);

                if (draw_state) {
                        while (true) {
                                draw_all(draw_state);
                                mct_window_swap(window);
                        }

                        mct_draw_state_free(draw_state);
                }

                mct_window_free(window);
        }

        XCloseDisplay(display);

        return EXIT_SUCCESS;
}
