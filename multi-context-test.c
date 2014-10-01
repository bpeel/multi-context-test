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
#include <time.h>

#include "shader-data.h"

#define GRID_WIDTH 100
#define GRID_HEIGHT 100

#define N_WINDOWS 3

#ifndef GL_CONTEXT_RELEASE_BEHAVIOR
#define GL_CONTEXT_RELEASE_BEHAVIOR       0x82FB
#endif
#ifndef GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH
#define GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH 0x82FC
#endif

#ifndef GLX_CONTEXT_RELEASE_BEHAVIOR_ARB
#define GLX_CONTEXT_RELEASE_BEHAVIOR_ARB  0x2097
#endif
#ifndef GLX_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB
#define GLX_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB 0
#endif
#ifndef GLX_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB
#define GLX_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB 0x2098
#endif

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

struct mct_context_state {
        struct mct_window *window;
        struct mct_draw_state *draw_state;
};

struct mct_vertex {
        float x, y;
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
mct_window_new(Display *display,
               int width, int height,
               bool flush_on_release)
{
        int context_attribs[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
                GLX_CONTEXT_MINOR_VERSION_ARB, 3,
                GLX_CONTEXT_PROFILE_MASK_ARB,
                GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                GLX_CONTEXT_FLAGS_ARB,
                GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
                GLX_CONTEXT_RELEASE_BEHAVIOR_ARB,
                GLX_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB,
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
        bool has_flush_ext;

        if (!check_glx_extension(display, "GLX_ARB_create_context")) {
                fprintf(stderr,
                        "GLX_ARB_create_context is not supported\n");
                return NULL;
        }

        has_flush_ext = check_glx_extension(display,
                                            "GLX_ARB_context_flush_control");

        if (flush_on_release) {
                if (has_flush_ext) {
                        context_attribs[sizeof context_attribs /
                                        sizeof context_attribs[0] - 2] =
                                GLX_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB;
                } else {
                        context_attribs[sizeof context_attribs /
                                        sizeof context_attribs[0] - 3] = None;
                }
        } else if (!has_flush_ext) {
                fprintf(stderr,
                        "Requested disabling flush on release but "
                        "GLX_ARB_context_flush_control is not "
                        "available\n");
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
        float sh;
        float blx, bly;
        int x, y;

        /* Makes a grid of triangles where each line of quads is
         * represented as a triangle strip. Each line is intended to
         * drawn separately */

        glGenBuffers(1, buffer);
        glBindBuffer(GL_ARRAY_BUFFER, *buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     sizeof (struct mct_vertex) * (width * 2 + 2) * height,
                     NULL,
                     GL_STATIC_DRAW);
        vertex = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);

        sh = 2.0f / height;

        for (y = 0; y < height; y++) {
                for (x = 0; x <= width; x++) {
                        blx = x * 2.0f / width - 1.0f;
                        bly = y * 2.0f / height - 1.0f;

                        vertex[0].x = blx;
                        vertex[0].y = bly + sh;

                        vertex[1].x = blx;
                        vertex[1].y = bly;

                        vertex += 2;
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
mct_draw_state_draw_row(struct mct_draw_state *draw_state, int y)
{
        glDrawArrays(GL_TRIANGLE_STRIP,
                     y * (GRID_WIDTH * 2 + 2),
                     GRID_WIDTH * 2 + 2);
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
destroy_contexts(struct mct_context_state *context_states,
                 int n_contexts)
{
        int i;

        for (i = n_contexts - 1; i >= 0; i--) {
                mct_window_make_current(context_states[i].window);
                mct_draw_state_free(context_states[i].draw_state);
                mct_window_free(context_states[i].window);
        }
}

static void
set_swap_interval(struct mct_window *window)
{
        PFNGLXSWAPINTERVALMESAPROC swap_interval_mesa;
        PFNGLXSWAPINTERVALMESAPROC swap_interval_sgi;

        if (check_glx_extension(window->display, "GLX_MESA_swap_control")) {
                swap_interval_mesa =
                        (void *) glXGetProcAddress((const GLubyte *)
                                                   "glXSwapIntervalMESA");
                if (swap_interval_mesa(0) == 0)
                        return;
        }

        /* Try with the SGI extension. Technically this shouldn't work
         * because the spec disallows swap interval 0 */
        if (check_glx_extension(window->display, "GLX_SGI_swap_control")) {
                swap_interval_sgi =
                        (void *) glXGetProcAddress((const GLubyte *)
                                                   "glXSwapIntervalSGI");
                if (swap_interval_sgi(0) == 0)
                        return;
        }

        fprintf(stderr,
                "note: failed to set swap interval to 0 with either "
                "GLX_MESA_swap_control or GLX_SGI_swap_control\n");
}

static bool
init_contexts(Display *display,
              struct mct_context_state *context_states,
              bool flush_on_release)
{
        int i;

        for (i = 0; i < N_WINDOWS; i++) {
                context_states[i].window =
                        mct_window_new(display, 640, 640, flush_on_release);

                if (context_states[i].window == NULL)
                        goto error;

                mct_window_make_current(context_states[i].window);

                set_swap_interval(context_states[i].window);

                context_states[i].draw_state = mct_draw_state_new();

                if (context_states[i].draw_state == NULL) {
                        mct_window_free(context_states[i].window);
                        goto error;
                }
        }

        return true;

error:
        destroy_contexts(context_states, i);
        return false;
}

static void
draw_context_window(struct mct_context_state *context_state,
                    int y)
{
        mct_window_make_current(context_state->window);
        mct_draw_state_draw_row(context_state->draw_state, y);
}

static void
draw_contexts(struct mct_context_state *context_states)
{
        int i, y;

        for (i = 0; i < N_WINDOWS; i++) {
                mct_window_make_current(context_states[i].window);
                mct_draw_state_start(context_states[i].draw_state);
        }

        for (y = 0; y < GRID_HEIGHT; y++) {
                for (i = 0; i < N_WINDOWS; i++) {
                        draw_context_window(context_states + i, y);
                }
        }

        for (i = 0; i < N_WINDOWS; i++) {
                mct_window_make_current(context_states[i].window);
                mct_draw_state_end(context_states[i].draw_state);
                mct_window_swap(context_states[i].window);
        }
}

static void
dump_release_behavior(void)
{
        GLint value;

        if (epoxy_has_gl_extension("GL_KHR_context_flush_control")) {
                glGetIntegerv(GL_CONTEXT_RELEASE_BEHAVIOR, &value);
                printf("GL_CONTEXT_RELEASE_BEHAVIOR = 0x%04x ", value);
                if (value == GL_NONE)
                        printf("(GL_NONE)\n");
                else if (value == GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH)
                        printf("(GL_CONTEXT_RELEASE_BEHAVIOR_FLUSH)\n");
                else
                        printf("(?)\n");
        } else {
                printf("GL_KHR_context_flush_control is unavailable\n");
        }
}

static void
usage(void)
{
        fprintf(stderr, "usage: multi-context-test [flush/none]\n");
        exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
        struct mct_context_state context_states[N_WINDOWS];
        Display *display;
        int frame_count = 0;
        time_t last_time = 0, now;
        bool flush_on_release = true;
        int i;

        if (argc == 2) {
                if (!strcmp(argv[1], "flush"))
                        flush_on_release = true;
                else if (!strcmp(argv[1], "none"))
                        flush_on_release = false;
                else
                        usage();
        } else if (argc != 1) {
                usage();
        }

        display = XOpenDisplay(NULL);

        if (display == NULL) {
                fprintf(stderr, "XOpenDisplay failed\n");
                return EXIT_FAILURE;
        }

        if (init_contexts(display, context_states, flush_on_release)) {
                for (i = 0; i < N_WINDOWS; i++) {
                        XMapWindow(display, context_states[i].window->win);
                        mct_window_make_current(context_states[i].window);
                        dump_release_behavior();
                }

                while (true) {
                        draw_contexts(context_states);

                        frame_count++;

                        time(&now);
                        if (now != last_time) {
                                printf("FPS = %i\n", frame_count);
                                last_time = now;
                                frame_count = 0;
                        }
                }
                destroy_contexts(context_states, N_WINDOWS);
        }

        XCloseDisplay(display);

        return EXIT_SUCCESS;
}
