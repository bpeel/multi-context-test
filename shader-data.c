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

#include <stdarg.h>
#include <epoxy/gl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "shader-data.h"

char *
shader_data_load_shader_source(const char *filename)
{
        FILE *file;
        char *source;
        long int size;
        int res;

        file = fopen(filename, "r");
        if (file == NULL) {
                fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                return NULL;
        }

        res = fseek(file, 0L, SEEK_END);
        if (res) {
                fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                fclose(file);
                return NULL;
        }

        size = ftell(file);
        if (size == -1) {
                fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                fclose(file);
                return NULL;
        }

        res = fseek(file, 0L, SEEK_SET);
        if (res) {
                fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                fclose(file);
                return NULL;
        }

        source = malloc(size + 1);
        source[size] = '\0';

        res = fread(source, 1, size, file);
        if (res != size) {
                if (ferror(file))
                        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                else
                        fprintf(stderr, "%s: unexpected EOF\n", filename);
                fclose(file);
                free(source);
                return NULL;
        }

        fclose(file);

        return source;
}

GLuint
shader_data_load_shader(GLenum type,
                        const char *filename)
{
        GLuint shader;
        GLint length, compile_status;
        GLsizei actual_length;
        char *source;
        GLchar *info_log;

        source = shader_data_load_shader_source(filename);
        if (source == NULL)
                return 0;

        length = strlen(source);

        shader = glCreateShader(type);
        glShaderSource(shader, 1, (const GLchar **) &source, &length);

        free(source);

        glCompileShader(shader);

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
                info_log = malloc(length);
                glGetShaderInfoLog(shader, length, &actual_length, info_log);
                if (*info_log) {
                        fprintf(stderr,
                                "Info log for %s:\n%s\n",
                                filename,
                                info_log);
                }
                free(info_log);
        }

        glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);

        if (!compile_status) {
                fprintf(stderr, "%s: compilation failed\n", filename);
                glDeleteShader(shader);
                return 0;
        }

        return shader;
}

GLuint
shader_data_load_program(GLenum shader_type,
                         ...)
{
        GLint length, link_status;
        GLsizei actual_length;
        GLchar *info_log;
        GLuint program;
        GLuint shader;
        va_list ap;

        va_start(ap, shader_type);

        program = glCreateProgram();

        while (shader_type != GL_NONE) {
                shader = shader_data_load_shader(shader_type,
                                                 va_arg(ap, const char *));
                if (shader == 0) {
                        glDeleteProgram(program);
                        va_end(ap);
                        return 0;
                }

                glAttachShader(program, shader);
                glDeleteShader(shader);

                shader_type = va_arg(ap, GLenum);
        }

        va_end(ap);

        glLinkProgram(program);

        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
                info_log = malloc(length);
                glGetProgramInfoLog(program, length, &actual_length, info_log);
                if (*info_log) {
                        fprintf(stderr,
                                "Link info log:\n%s\n",
                                info_log);
                }
                free(info_log);
        }

        glGetProgramiv(program, GL_LINK_STATUS, &link_status);

        if (!link_status) {
                fprintf(stderr, "program link failed\n");
                glDeleteProgram(program);
                return 0;
        }

        return program;
}
