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
#include <EGL/egl.h>
#include <EGL/eglext.h>
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

#ifndef EGL_CONTEXT_RELEASE_BEHAVIOR_KHR
#define EGL_CONTEXT_RELEASE_BEHAVIOR_KHR  0x2097
#endif
#ifndef EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR
#define EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR 0
#endif
#ifndef EGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_KHR
#define EGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_KHR 0x2098
#endif

struct mct_display {
        Display *x;
        EGLDisplay egl;
};

struct mct_window {
        struct mct_display *display;
        Window win;
        EGLContext context;
        EGLSurface surface;
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
check_egl_extension(EGLDisplay egl_display, const char *ext_name)
{
        int ext_len = strlen(ext_name);
        const char *extensions =
                eglQueryString(egl_display, EGL_EXTENSIONS);
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

static EGLConfig
choose_egl_config(EGLDisplay display)
{
        EGLint config_count = 0;
        EGLConfig config;
        EGLBoolean status;
        static const int attrib_list[] = {
                EGL_NONE
        };

        status = eglChooseConfig(display,
                                 attrib_list,
                                 &config, 1,
                                 &config_count);

        if (status != EGL_TRUE || config_count == 0) {
                fprintf(stderr, "Unable to find a usable EGL configuration\n");
                return NULL;
        }

        return config;
}

static void
mct_window_make_current(struct mct_window *window)
{
        eglMakeCurrent(window->display->egl,
                       window->surface,
                       window->surface,
                       window->context);
}

static void
mct_window_swap(struct mct_window *window)
{
        mct_window_make_current(window);
        eglSwapBuffers(window->display->egl, window->surface);
}

static struct mct_window *
mct_window_new(struct mct_display *display,
               int width, int height,
               bool flush_on_release)
{
        int context_attribs[] = {
                EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
                EGL_CONTEXT_MINOR_VERSION_KHR, 3,
                EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                EGL_CONTEXT_FLAGS_KHR,
                EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR,
                EGL_CONTEXT_RELEASE_BEHAVIOR_KHR,
                EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR,
                EGL_NONE
        };
        EGLint visualid;
        EGLConfig egl_config;
        EGLContext ctx;
        int scrnum = 0;
        XSetWindowAttributes attr;
        unsigned long mask;
        Window root;
        XVisualInfo *visinfo, visinfo_template;
        int visinfos_count;
        struct mct_window *window;
        bool has_flush_ext;

        if (!check_egl_extension(display->egl, "EGL_KHR_create_context")) {
                fprintf(stderr,
                        "EGL_KHR_create_context is not supported\n");
                return NULL;
        }

        has_flush_ext = check_egl_extension(display->egl,
                                            "EGL_KHR_context_flush_control");

        if (flush_on_release) {
                if (has_flush_ext) {
                        context_attribs[sizeof context_attribs /
                                        sizeof context_attribs[0] - 2] =
                                EGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_KHR;
                } else {
                        context_attribs[sizeof context_attribs /
                                        sizeof context_attribs[0] - 3] =
                                EGL_NONE;
                }
        } else if (!has_flush_ext) {
                fprintf(stderr,
                        "Requested disabling flush on release but "
                        "EGL_KHR_context_flush_control is not "
                        "available\n");
                return NULL;
        }

        if (!eglBindAPI(EGL_OPENGL_API)) {
                fprintf(stderr,
                        "eglBindAPI failed\n");
                return NULL;
        }

        egl_config = choose_egl_config(display->egl);

        if (egl_config == NULL)
                return NULL;

        eglGetConfigAttrib(display->egl, egl_config,
                           EGL_NATIVE_VISUAL_ID, &visualid);

        if (visualid == 0) {
                fprintf(stderr,
                         "EGL config does not have an associated visual\n");
                return NULL;
        }

        visinfo_template.visualid = visualid;

        visinfo = XGetVisualInfo(display->x,
                                 VisualIDMask, &visinfo_template,
                                 &visinfos_count);

        if (visinfo == NULL) {
                fprintf(stderr, "XGetVisualInfo failed\n");
                return NULL;
        }

        ctx = eglCreateContext(display->egl,
                               egl_config,
                               EGL_NO_CONTEXT,
                               context_attribs);

        if (ctx == EGL_NO_CONTEXT) {
                fprintf(stderr,
                        "Error: eglCreateContext failed\n");
                XFree(visinfo);
                return NULL;
        }

        window = malloc(sizeof *window);

        root = RootWindow(display->x, scrnum);

        /* window attributes */
        attr.background_pixel = 0;
        attr.border_pixel = 0;
        attr.colormap =
            XCreateColormap(display->x, root, visinfo->visual, AllocNone);
        attr.event_mask =
            StructureNotifyMask | ExposureMask | PointerMotionMask |
            KeyPressMask;
        mask = CWBorderPixel | CWColormap | CWEventMask;

        window->win = XCreateWindow(display->x, root, 0, 0, width, height,
                                    0, visinfo->depth, InputOutput,
                                    visinfo->visual, mask, &attr);

        window->surface =
                eglCreateWindowSurface(display->egl,
                                       egl_config,
                                       (EGLNativeWindowType) window->win,
                                       NULL);

        window->context = ctx;
        window->display = display;

        XFree(visinfo);

        return window;
}

static void
mct_window_free(struct mct_window *window)
{
        eglDestroyContext(window->display->egl, window->context);
        eglDestroySurface(window->display->egl, window->surface);
        XDestroyWindow(window->display->x, window->win);
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
        EGLBoolean status;

        status = eglSwapInterval(window->display->egl, 0);

        if (!status)
                fprintf(stderr, "note: failed to set swap interval to 0\n");
}

static bool
init_contexts(struct mct_display *display,
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

static struct mct_display *
open_display(void)
{
        Display *x;
        EGLDisplay egl;
        struct mct_display *display;
        EGLBoolean status;
        EGLint egl_major, egl_minor;

        x = XOpenDisplay(NULL);

        if (x == NULL) {
                fprintf(stderr, "XOpenDisplay failed\n");
                return NULL;
        }

        egl = eglGetDisplay((EGLNativeDisplayType) x);

        if (egl == EGL_NO_DISPLAY) {
                XCloseDisplay(x);
                fprintf(stderr, "eglGetDisplay failed\n");
                return NULL;
        }

        status = eglInitialize(egl, &egl_major, &egl_minor);

        if (!status) {
                XCloseDisplay(x);
                fprintf(stderr, "eglInitialize failed\n");
                return NULL;
        }

        display = malloc(sizeof *display);
        display->x = x;
        display->egl = egl;

        return display;
}

static void
close_display(struct mct_display *display)
{
        eglTerminate(display->egl);
        XCloseDisplay(display->x);
}

int
main(int argc, char **argv)
{
        struct mct_context_state context_states[N_WINDOWS];
        struct mct_display *display;
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

        display = open_display();

        if (display == NULL)
                return EXIT_FAILURE;

        if (init_contexts(display, context_states, flush_on_release)) {
                for (i = 0; i < N_WINDOWS; i++) {
                        XMapWindow(display->x, context_states[i].window->win);
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

        close_display(display);

        return EXIT_SUCCESS;
}
