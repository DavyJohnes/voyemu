#include "bounded_wait_entry.c"
#if !defined(QGFX_PASSION_ROOT_IMAGEFS)
#include "interguest_network.c"
#if !defined(QGFX_QINGGAN11_MCU)
#include "startup_extension.c"
#define QGFX_HELPER_DISPATCH()                                                                     \
    do {                                                                                           \
        qbw_start();                                                                               \
        qnet_start();                                                                              \
        qse_userdata_start();                                                                      \
        qse_supervisor_start();                                                                    \
        qse_start();                                                                               \
    } while (0)
#else
#define QGFX_HELPER_DISPATCH()                                                                     \
    do {                                                                                           \
        qbw_start();                                                                               \
        qnet_start();                                                                              \
    } while (0)
#endif
#else
#include "interguest_network.c"

#include "hab_socket_qnx.c"
#include "passion_helper_dispatch.h"
#endif
#include "core.c"
VOIDCALL(glActiveTexture, 100, (GLenum texture), (u64)texture)

void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) {
    acquire();
    u64 a[] = {(u64)program, (u64)index, 0};
    (void)call(101, a, 3, name, length(name) + 1);
    release();
}

void glBindBuffer(GLenum target, GLuint buffer) {
    acquire();
    u64 a[] = {(u64)target, (u64)buffer};
    (void)send_void(102, a, 2, 0, 0);

    if (target == 0x88ec) {
        unpack_buffer = buffer;
    } else if (target == 0x8892) {
        client_array_buffer = buffer;
    } else if (target == 0x8893) {
        client_element_buffer = buffer;
    }

    release();
}

VOIDCALL(glBindFramebuffer, 103, (GLenum target, GLuint framebuffer), (u64)target, (u64)framebuffer)
VOIDCALL(glBindRenderbuffer, 104, (GLenum target, GLuint renderbuffer), (u64)target,
         (u64)renderbuffer)
VOIDCALL(glBindTexture, 105, (GLenum target, GLuint texture), (u64)target, (u64)texture)
VOIDCALL(glBlendFunc, 106, (GLenum sfactor, GLenum dfactor), (u64)sfactor, (u64)dfactor)
VOIDCALL(glBlendFuncSeparate, 107,
         (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha),
         (u64)sfactorRGB, (u64)dfactorRGB, (u64)sfactorAlpha, (u64)dfactorAlpha)
VOIDCALL(glBlitFramebuffer, 108,
         (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1,
          GLint dstY1, GLbitfield mask, GLenum filter),
         (u64)srcX0, (u64)srcY0, (u64)srcX1, (u64)srcY1, (u64)dstX0, (u64)dstY0, (u64)dstX1,
         (u64)dstY1, (u64)mask, (u64)filter)

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    acquire();
    usize bytes = (usize)size;
    u64 blob = upload(data, bytes);

    if (data && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)size, blob, (u64)usage};
    (void)call(109, a, 4, 0, 0);
    drop_blob(blob);
    release();
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    acquire();
    usize bytes = (usize)size;
    u64 blob = upload(data, bytes);

    if (data && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)offset, (u64)size, blob};
    (void)call(110, a, 4, 0, 0);
    drop_blob(blob);
    release();
}

SCALAR(glCheckFramebufferStatus, GLenum, 111, (GLenum target), (u64)target)
VOIDCALL(glClearBufferfi, 112, (GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil),
         (u64)buffer, (u64)drawbuffer, fbits(depth), (u64)stencil)

void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat *value) {
    if (!value) {
        transport_error = 1;

        return;
    }

    acquire();
    usize bytes = buffer == 0x1800 ? 16 : 4;
    u64 a[] = {(u64)buffer, (u64)drawbuffer, 0};
    (void)send_void(113, a, 3, value, (unsigned)bytes);
    release();
}

void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint *value) {
    if (!value) {
        transport_error = 1;

        return;
    }

    acquire();
    usize bytes = buffer == 0x1800 ? 16 : 4;
    u64 a[] = {(u64)buffer, (u64)drawbuffer, 0};
    (void)send_void(114, a, 3, value, (unsigned)bytes);
    release();
}

void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint *value) {
    if (!value) {
        transport_error = 1;

        return;
    }

    acquire();
    usize bytes = buffer == 0x1800 ? 16 : 4;
    u64 a[] = {(u64)buffer, (u64)drawbuffer, 0};
    (void)send_void(115, a, 3, value, (unsigned)bytes);
    release();
}

VOIDCALL(glClearDepthf, 116, (GLfloat d), fbits(d))
VOIDCALL(glClearStencil, 117, (GLint s), (u64)s)
VOIDCALL(glColorMask, 118, (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha),
         (u64)red, (u64)green, (u64)blue, (u64)alpha)

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                            GLsizei height, GLint border, GLsizei imageSize, const void *data) {
    acquire();
    usize bytes = (usize)imageSize;
    u64 blob = upload(data, bytes);

    if (data && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)level,  (u64)internalformat, (u64)width,
               (u64)height, (u64)border, (u64)imageSize,      blob};
    (void)call(119, a, 8, 0, 0);
    drop_blob(blob);
    release();
}

void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                            GLsizei height, GLsizei depth, GLint border, GLsizei imageSize,
                            const void *data) {
    acquire();
    usize bytes = (usize)imageSize;
    u64 blob = upload(data, bytes);

    if (data && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)level,  (u64)internalformat, (u64)width, (u64)height,
               (u64)depth,  (u64)border, (u64)imageSize,      blob};
    (void)call(120, a, 9, 0, 0);
    drop_blob(blob);
    release();
}

void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                               GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
                               const void *data) {
    acquire();
    usize bytes = (usize)imageSize;
    u64 blob = upload(data, bytes);

    if (data && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)level,  (u64)xoffset,   (u64)yoffset, (u64)width,
               (u64)height, (u64)format, (u64)imageSize, blob};
    (void)call(121, a, 9, 0, 0);
    drop_blob(blob);
    release();
}

VOIDCALL(glCullFace, 122, (GLenum mode), (u64)mode)

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    acquire();
    usize bytes = (usize)n * 4;
    u64 blob = upload(buffers, bytes);

    if (buffers && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)n, blob};
    (void)call(123, a, 2, 0, 0);
    drop_blob(blob);
    release();
}

void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    acquire();
    usize bytes = (usize)n * 4;
    u64 blob = upload(framebuffers, bytes);

    if (framebuffers && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)n, blob};
    (void)call(124, a, 2, 0, 0);
    drop_blob(blob);
    release();
}

void glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
    acquire();
    usize bytes = (usize)n * 4;
    u64 blob = upload(renderbuffers, bytes);

    if (renderbuffers && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)n, blob};
    (void)call(125, a, 2, 0, 0);
    drop_blob(blob);
    release();
}

void glDeleteTextures(GLsizei n, const GLuint *textures) {
    acquire();
    usize bytes = (usize)n * 4;
    u64 blob = upload(textures, bytes);

    if (textures && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)n, blob};
    (void)call(126, a, 2, 0, 0);
    drop_blob(blob);
    release();
}

VOIDCALL(glDepthFunc, 127, (GLenum func), (u64)func)
VOIDCALL(glDepthMask, 128, (GLboolean flag), (u64)flag)
VOIDCALL(glDetachShader, 129, (GLuint program, GLuint shader), (u64)program, (u64)shader)
VOIDCALL(glDisable, 130, (GLenum cap), (u64)cap)

void glDisableVertexAttribArray(GLuint index) {
    acquire();
    u64 a[] = {(u64)index};
    (void)send_void(131, a, 1, 0, 0);

    if (index < 64) {
        client_arrays[index].enabled = 0;
    }

    release();
}

void glDrawBuffers(GLsizei n, const GLenum *bufs) {
    acquire();
    usize bytes = (usize)n * 4;
    u64 blob = upload(bufs, bytes);

    if (bufs && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)n, blob};
    (void)call(132, a, 2, 0, 0);
    drop_blob(blob);
    release();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
VOIDCALL(glEnable, 134, (GLenum cap), (u64)cap)

void glEnableVertexAttribArray(GLuint index) {
    acquire();
    u64 a[] = {(u64)index};
    (void)send_void(135, a, 1, 0, 0);

    if (index < 64) {
        client_arrays[index].enabled = 1;
    }

    release();
}

void glFlush(void) {
    acquire();
    (void)call(136, 0, 0, 0, 0);
    release();
}

VOIDCALL(glFramebufferRenderbuffer, 137,
         (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer),
         (u64)target, (u64)attachment, (u64)renderbuffertarget, (u64)renderbuffer)
VOIDCALL(glFramebufferTexture2D, 138,
         (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level),
         (u64)target, (u64)attachment, (u64)textarget, (u64)texture, (u64)level)

void glGenBuffers(GLsizei n, GLuint *buffers) {
    if (n > 0 && !buffers) {
        transport_error = 1;

        return;
    }

    if (n >= 0 && (u64)n * 4 <= 3840) {
        acquire();
        u64 a[] = {(u64)n, 0};
        u64 r = call(139, a, 2, 0, 0);

        if (r == (u64)n * 4 && packet && packet->out_len == (unsigned)n * 4) {
            if (n) {
                copy(buffers, packet->data, (usize)n * 4);
            }
        } else {
            transport_error = 1;
        }

        release();

        return;
    }

    if (n < 0 || (u64)n * 4 > MAX_BULK) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 blob = download_begin((usize)n * 4);
    u64 a[] = {(u64)n, blob};
    u64 r = call(139, a, 2, 0, 0);

    if (r == (u64)n * 4) {
        download_end(buffers, (usize)n * 4);
    } else {
        bulk_free();
    }

    drop_blob(blob);
    release();
}

void glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
    if (n > 0 && !framebuffers) {
        transport_error = 1;

        return;
    }

    if (n >= 0 && (u64)n * 4 <= 3840) {
        acquire();
        u64 a[] = {(u64)n, 0};
        u64 r = call(140, a, 2, 0, 0);

        if (r == (u64)n * 4 && packet && packet->out_len == (unsigned)n * 4) {
            if (n) {
                copy(framebuffers, packet->data, (usize)n * 4);
            }
        } else {
            transport_error = 1;
        }

        release();

        return;
    }

    if (n < 0 || (u64)n * 4 > MAX_BULK) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 blob = download_begin((usize)n * 4);
    u64 a[] = {(u64)n, blob};
    u64 r = call(140, a, 2, 0, 0);

    if (r == (u64)n * 4) {
        download_end(framebuffers, (usize)n * 4);
    } else {
        bulk_free();
    }

    drop_blob(blob);
    release();
}

void glGenRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    if (n > 0 && !renderbuffers) {
        transport_error = 1;

        return;
    }

    if (n >= 0 && (u64)n * 4 <= 3840) {
        acquire();
        u64 a[] = {(u64)n, 0};
        u64 r = call(141, a, 2, 0, 0);

        if (r == (u64)n * 4 && packet && packet->out_len == (unsigned)n * 4) {
            if (n) {
                copy(renderbuffers, packet->data, (usize)n * 4);
            }
        } else {
            transport_error = 1;
        }

        release();

        return;
    }

    if (n < 0 || (u64)n * 4 > MAX_BULK) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 blob = download_begin((usize)n * 4);
    u64 a[] = {(u64)n, blob};
    u64 r = call(141, a, 2, 0, 0);

    if (r == (u64)n * 4) {
        download_end(renderbuffers, (usize)n * 4);
    } else {
        bulk_free();
    }

    drop_blob(blob);
    release();
}

void glGenTextures(GLsizei n, GLuint *textures) {
    if (n > 0 && !textures) {
        transport_error = 1;

        return;
    }

    if (n >= 0 && (u64)n * 4 <= 3840) {
        acquire();
        u64 a[] = {(u64)n, 0};
        u64 r = call(142, a, 2, 0, 0);

        if (r == (u64)n * 4 && packet && packet->out_len == (unsigned)n * 4) {
            if (n) {
                copy(textures, packet->data, (usize)n * 4);
            }
        } else {
            transport_error = 1;
        }

        release();

        return;
    }

    if (n < 0 || (u64)n * 4 > MAX_BULK) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 blob = download_begin((usize)n * 4);
    u64 a[] = {(u64)n, blob};
    u64 r = call(142, a, 2, 0, 0);

    if (r == (u64)n * 4) {
        download_end(textures, (usize)n * 4);
    } else {
        bulk_free();
    }

    drop_blob(blob);
    release();
}

VOIDCALL(glGenerateMipmap, 143, (GLenum target), (u64)target)
void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size,
                       GLenum *type, GLchar *name);
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size,
                        GLenum *type, GLchar *name);

GLint glGetAttribLocation(GLuint program, const GLchar *name) {
    acquire();
    u64 a[] = {(u64)program, 0};
    u64 r = call(146, a, 2, name, length(name) + 1);
    release();

    return (GLint)r;
}

void glGetBooleanv(GLenum pname, GLboolean *data) {
    acquire();
    u64 a[] = {(u64)pname, 0};
    (void)call(147, a, 2, 0, 0);

    if (data && packet && packet->out_len) {
        copy(data, packet->data, packet->out_len);
    }

    release();
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    acquire();
    u64 a[] = {(u64)target, (u64)pname, 0};
    (void)call(148, a, 3, 0, 0);

    if (params && packet && packet->out_len) {
        copy(params, packet->data, packet->out_len);
    }

    release();
}

void glGetFloatv(GLenum pname, GLfloat *data) {
    acquire();
    u64 a[] = {(u64)pname, 0};
    (void)call(149, a, 2, 0, 0);

    if (data && packet && packet->out_len) {
        copy(data, packet->data, packet->out_len);
    }

    release();
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname,
                                           GLint *params) {
    acquire();
    u64 a[] = {(u64)target, (u64)attachment, (u64)pname, 0};
    (void)call(150, a, 4, 0, 0);

    if (params && packet && packet->out_len) {
        copy(params, packet->data, packet->out_len);
    }

    release();
}

void glGetIntegerv(GLenum pname, GLint *data) {
    acquire();
    u64 a[] = {(u64)pname, 0};
    (void)call(151, a, 2, 0, 0);

    if (data && packet && packet->out_len) {
        copy(data, packet->data, packet->out_len);
    }

    release();
}

void glGetProgramBinary(GLuint program, GLsizei bufSize, GLsizei *length, GLenum *binaryFormat,
                        void *binary);
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    acquire();
    u64 a[] = {(u64)target, (u64)pname, 0};
    (void)call(154, a, 3, 0, 0);

    if (params && packet && packet->out_len) {
        copy(params, packet->data, packet->out_len);
    }

    release();
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params) {
    acquire();
    u64 a[] = {(u64)target, (u64)pname, 0};
    (void)call(156, a, 3, 0, 0);

    if (params && packet && packet->out_len) {
        copy(params, packet->data, packet->out_len);
    }

    release();
}

GLint glGetUniformLocation(GLuint program, const GLchar *name) {
    GLint r = uniform_get_location(program, name, 157);

    return (GLint)r;
}

void glGetUniformfv(GLuint program, GLint location, GLfloat *params) {
    acquire();
    u64 a[] = {(u64)program, (u64)location, 0};
    (void)call(158, a, 3, 0, 0);

    if (params && packet && packet->out_len) {
        copy(params, packet->data, packet->out_len);
    }

    release();
}

void glGetUniformiv(GLuint program, GLint location, GLint *params) {
    acquire();
    u64 a[] = {(u64)program, (u64)location, 0};
    (void)call(159, a, 3, 0, 0);

    if (params && packet && packet->out_len) {
        copy(params, packet->data, packet->out_len);
    }

    release();
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer);

void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params) {
    acquire();
    u64 a[] = {(u64)index, (u64)pname, 0};
    (void)call(161, a, 3, 0, 0);

    if (params && packet && packet->out_len) {
        copy(params, packet->data, packet->out_len);
    }

    release();
}

VOIDCALL(glHint, 162, (GLenum target, GLenum mode), (u64)target, (u64)mode)

void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments) {
    acquire();
    usize bytes = (usize)numAttachments * 4;
    u64 blob = upload(attachments, bytes);

    if (attachments && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)numAttachments, blob};
    (void)call(163, a, 3, 0, 0);
    drop_blob(blob);
    release();
}

SCALAR(glIsEnabled, GLboolean, 164, (GLenum cap), (u64)cap)
SCALAR(glIsFramebuffer, GLboolean, 165, (GLuint framebuffer), (u64)framebuffer)
SCALAR(glIsTexture, GLboolean, 166, (GLuint texture), (u64)texture)
VOIDCALL(glLineWidth, 167, (GLfloat width), fbits(width))

void glPixelStorei(GLenum pname, GLint param) {
    acquire();
    u64 a[] = {(u64)pname, (u64)param};
    (void)send_void(168, a, 2, 0, 0);

    if (pname == 0xcf5 && (param == 1 || param == 2 || param == 4 || param == 8)) {
        unpack_alignment = param;
    } else if (pname == 0xcf2 && param >= 0) {
        unpack_row_length = param;
    } else if (pname == 0xcf3 && param >= 0) {
        unpack_skip_rows = param;
    } else if (pname == 0xcf4 && param >= 0) {
        unpack_skip_pixels = param;
    }

    release();
}

void glProgramBinary(GLuint program, GLenum binaryFormat, const void *binary, GLsizei length) {
    acquire();
    uniform_forget(program);
    usize bytes = (usize)length;
    u64 blob = upload(binary, bytes);

    if (binary && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)program, (u64)binaryFormat, blob, (u64)length};
    (void)call(169, a, 4, 0, 0);
    drop_blob(blob);
    release();
}

VOIDCALL(glRenderbufferStorage, 170,
         (GLenum target, GLenum internalformat, GLsizei width, GLsizei height), (u64)target,
         (u64)internalformat, (u64)width, (u64)height)
VOIDCALL(glRenderbufferStorageMultisample, 171,
         (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height),
         (u64)target, (u64)samples, (u64)internalformat, (u64)width, (u64)height)
VOIDCALL(glSampleCoverage, 172, (GLfloat value, GLboolean invert), fbits(value), (u64)invert)
VOIDCALL(glScissor, 173, (GLint x, GLint y, GLsizei width, GLsizei height), (u64)x, (u64)y,
         (u64)width, (u64)height)
void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryformat, const void *binary,
                    GLsizei length);
VOIDCALL(glStencilFunc, 175, (GLenum func, GLint ref, GLuint mask), (u64)func, (u64)ref, (u64)mask)
VOIDCALL(glStencilMask, 176, (GLuint mask), (u64)mask)
VOIDCALL(glStencilOp, 177, (GLenum fail, GLenum zfail, GLenum zpass), (u64)fail, (u64)zfail,
         (u64)zpass)

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const void *pixels) {
    acquire();
    usize bytes = pixel_extent(width, height, format, type);

    if (pixels && bytes && bytes <= 3840) {
        u64 a[] = {(u64)target, (u64)level,  (u64)internalformat, (u64)width, (u64)height,
                   (u64)border, (u64)format, (u64)type,           0};
        (void)call(178, a, 9, pixels, (unsigned)bytes);
        release();

        return;
    }

    u64 blob = texture_upload(pixels, bytes);

    if (pixels && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)level,  (u64)internalformat, (u64)width, (u64)height,
               (u64)border, (u64)format, (u64)type,           blob};
    (void)call(178, a, 9, 0, 0);
    release();
}

VOIDCALL(glTexParameterf, 179, (GLenum target, GLenum pname, GLfloat param), (u64)target,
         (u64)pname, fbits(param))
VOIDCALL(glTexParameteri, 180, (GLenum target, GLenum pname, GLint param), (u64)target, (u64)pname,
         (u64)param)
VOIDCALL(glTexStorage2D, 181,
         (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height),
         (u64)target, (u64)levels, (u64)internalformat, (u64)width, (u64)height)
VOIDCALL(glTexStorage2DMultisample, 182,
         (GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height,
          GLboolean fixedsamplelocations),
         (u64)target, (u64)samples, (u64)internalformat, (u64)width, (u64)height,
         (u64)fixedsamplelocations)
VOIDCALL(glTexStorage3D, 183,
         (GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height,
          GLsizei depth),
         (u64)target, (u64)levels, (u64)internalformat, (u64)width, (u64)height, (u64)depth)

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                     GLsizei height, GLenum format, GLenum type, const void *pixels) {
    acquire();
    usize bytes = pixel_extent(width, height, format, type);

    if (pixels && bytes && bytes <= 3840) {
        u64 a[] = {(u64)target,  (u64)level, (u64)xoffset,
                   (u64)yoffset, (u64)width, (u64)height,
                   (u64)format,  (u64)type,  0};
        (void)call(184, a, 9, pixels, (unsigned)bytes);
        release();

        return;
    }

    u64 blob = texture_upload(pixels, bytes);

    if (pixels && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)target, (u64)level,  (u64)xoffset, (u64)yoffset, (u64)width,
               (u64)height, (u64)format, (u64)type,    blob};
    (void)call(184, a, 9, 0, 0);
    release();
}

VOIDCALL(glUniform1f, 185, (GLint location, GLfloat v0), (u64)location, fbits(v0))

void glUniform1fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count > 0 && !value) {
        transport_error = 1;

        return;
    }

    if (count >= 0 && ((usize)count * 4) <= 3840 && (((usize)count * 4) == 0 || value)) {
        acquire();
        u64 a[] = {(u64)location, (u64)count, 0};
        (void)send_void(186, a, 3, value, (unsigned)((usize)count * 4));
        release();

        return;
    }

    acquire();
    usize bytes = (usize)count * 4;
    u64 blob = upload(value, bytes);

    if (value && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)location, (u64)count, blob};
    (void)call(186, a, 3, 0, 0);
    drop_blob(blob);
    release();
}

VOIDCALL(glUniform1i, 187, (GLint location, GLint v0), (u64)location, (u64)v0)
VOIDCALL(glUniform2f, 188, (GLint location, GLfloat v0, GLfloat v1), (u64)location, fbits(v0),
         fbits(v1))

/* The wire carries vec2 uniforms only in the scalar form, but Qt sets every vec2 (the scene graph's
textureScale among them) through the vector form: untranslated, such a call reaches the OEM driver
instead of the host, and the host program keeps the uniform's zero default. */
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count <= 0) {
        return;
    }

    if (!value) {
        transport_error = 1;

        return;
    }

    if (location > 0 && (u64)location + (u64)count - 1 > 0x7fffffff) {
        transport_error = 1;

        return;
    }

    for (GLsizei i = 0; i < count; ++i) {
        glUniform2f(location + i, value[(usize)i * 2], value[(usize)i * 2 + 1]);
    }
}

VOIDCALL(glUniform3f, 189, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2), (u64)location,
         fbits(v0), fbits(v1), fbits(v2))

void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count > 0 && !value) {
        transport_error = 1;

        return;
    }

    if (count >= 0 && ((usize)count * 12) <= 3840 && (((usize)count * 12) == 0 || value)) {
        acquire();
        u64 a[] = {(u64)location, (u64)count, 0};
        (void)send_void(190, a, 3, value, (unsigned)((usize)count * 12));
        release();

        return;
    }

    acquire();
    usize bytes = (usize)count * 12;
    u64 blob = upload(value, bytes);

    if (value && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)location, (u64)count, blob};
    (void)call(190, a, 3, 0, 0);
    drop_blob(blob);
    release();
}

VOIDCALL(glUniform4f, 191, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3),
         (u64)location, fbits(v0), fbits(v1), fbits(v2), fbits(v3))

void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) {
    if (count > 0 && !value) {
        transport_error = 1;

        return;
    }

    if (count >= 0 && ((usize)count * 16) <= 3840 && (((usize)count * 16) == 0 || value)) {
        acquire();
        u64 a[] = {(u64)location, (u64)count, 0};
        (void)send_void(192, a, 3, value, (unsigned)((usize)count * 16));
        release();

        return;
    }

    acquire();
    usize bytes = (usize)count * 16;
    u64 blob = upload(value, bytes);

    if (value && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)location, (u64)count, blob};
    (void)call(192, a, 3, 0, 0);
    drop_blob(blob);
    release();
}

void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if (count > 0 && !value) {
        transport_error = 1;

        return;
    }

    if (count >= 0 && ((usize)count * 36) <= 3840 && (((usize)count * 36) == 0 || value)) {
        acquire();
        u64 a[] = {(u64)location, (u64)count, (u64)transpose, 0};
        (void)send_void(193, a, 4, value, (unsigned)((usize)count * 36));
        release();

        return;
    }

    acquire();
    usize bytes = (usize)count * 36;
    u64 blob = upload(value, bytes);

    if (value && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)location, (u64)count, (u64)transpose, blob};
    (void)call(193, a, 4, 0, 0);
    drop_blob(blob);
    release();
}

void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
    if (count > 0 && !value) {
        transport_error = 1;

        return;
    }

    if (count >= 0 && ((usize)count * 64) <= 3840 && (((usize)count * 64) == 0 || value)) {
        acquire();
        u64 a[] = {(u64)location, (u64)count, (u64)transpose, 0};
        (void)send_void(194, a, 4, value, (unsigned)((usize)count * 64));
        release();

        return;
    }

    acquire();
    usize bytes = (usize)count * 64;
    u64 blob = upload(value, bytes);

    if (value && !blob) {
        transport_error = 1;
        release();

        return;
    }

    u64 a[] = {(u64)location, (u64)count, (u64)transpose, blob};
    (void)call(194, a, 4, 0, 0);
    drop_blob(blob);
    release();
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer);
#include "client_arrays.c"
#include "special.c"

#include "egl_window.c"
#include "screen_export.c"

#include "screen_capture.c"
#include "screen_capture_entry.c"
typedef void (*Proc)(void);

Proc eglGetProcAddress(const char *name) {
    struct Entry {
        const char *name;
        Proc function;
    };
    static const struct Entry entries[] = {
            {"eglBindAPI", (void (*)(void))eglBindAPI},
            {"eglChooseConfig", (void (*)(void))eglChooseConfig},
            {"eglCopyBuffers", (void (*)(void))eglCopyBuffers},
            {"eglCreateContext", (void (*)(void))eglCreateContext},
            {"eglCreateWindowSurface", (void (*)(void))eglCreateWindowSurface},
            {"eglDestroyContext", (void (*)(void))eglDestroyContext},
            {"eglDestroySurface", (void (*)(void))eglDestroySurface},
            {"eglGetConfigAttrib", (void (*)(void))eglGetConfigAttrib},
            {"eglGetConfigs", (void (*)(void))eglGetConfigs},
            {"eglGetCurrentContext", (void (*)(void))eglGetCurrentContext},
            {"eglGetCurrentDisplay", (void (*)(void))eglGetCurrentDisplay},
            {"eglGetDisplay", (void (*)(void))eglGetDisplay},
            {"eglGetError", (void (*)(void))eglGetError},
            {"eglGetProcAddress", (void (*)(void))eglGetProcAddress},
            {"eglInitialize", (void (*)(void))eglInitialize},
            {"eglMakeCurrent", (void (*)(void))eglMakeCurrent},
            {"eglQueryAPI", (void (*)(void))eglQueryAPI},
            {"eglQueryContext", (void (*)(void))eglQueryContext},
            {"eglQueryString", (void (*)(void))eglQueryString},
            {"eglQuerySurface", (void (*)(void))eglQuerySurface},
            {"eglSurfaceAttrib", (void (*)(void))eglSurfaceAttrib},
            {"eglSwapBuffers", (void (*)(void))eglSwapBuffers},
            {"eglSwapInterval", (void (*)(void))eglSwapInterval},
            {"eglTerminate", (void (*)(void))eglTerminate},
            {"glActiveTexture", (void (*)(void))glActiveTexture},
            {"glAttachShader", (void (*)(void))glAttachShader},
            {"glBindAttribLocation", (void (*)(void))glBindAttribLocation},
            {"glBindBuffer", (void (*)(void))glBindBuffer},
            {"glBindFramebuffer", (void (*)(void))glBindFramebuffer},
            {"glBindRenderbuffer", (void (*)(void))glBindRenderbuffer},
            {"glBindTexture", (void (*)(void))glBindTexture},
            {"glBlendFunc", (void (*)(void))glBlendFunc},
            {"glBlendFuncSeparate", (void (*)(void))glBlendFuncSeparate},
            {"glBlitFramebuffer", (void (*)(void))glBlitFramebuffer},
            {"glBufferData", (void (*)(void))glBufferData},
            {"glBufferSubData", (void (*)(void))glBufferSubData},
            {"glCheckFramebufferStatus", (void (*)(void))glCheckFramebufferStatus},
            {"glClear", (void (*)(void))glClear},
            {"glClearBufferfi", (void (*)(void))glClearBufferfi},
            {"glClearBufferfv", (void (*)(void))glClearBufferfv},
            {"glClearBufferiv", (void (*)(void))glClearBufferiv},
            {"glClearBufferuiv", (void (*)(void))glClearBufferuiv},
            {"glClearColor", (void (*)(void))glClearColor},
            {"glClearDepthf", (void (*)(void))glClearDepthf},
            {"glClearStencil", (void (*)(void))glClearStencil},
            {"glColorMask", (void (*)(void))glColorMask},
            {"glCompileShader", (void (*)(void))glCompileShader},
            {"glCompressedTexImage2D", (void (*)(void))glCompressedTexImage2D},
            {"glCompressedTexImage3D", (void (*)(void))glCompressedTexImage3D},
            {"glCompressedTexSubImage2D", (void (*)(void))glCompressedTexSubImage2D},
            {"glCreateProgram", (void (*)(void))glCreateProgram},
            {"glCreateShader", (void (*)(void))glCreateShader},
            {"glCullFace", (void (*)(void))glCullFace},
            {"glDeleteBuffers", (void (*)(void))glDeleteBuffers},
            {"glDeleteFramebuffers", (void (*)(void))glDeleteFramebuffers},
            {"glDeleteProgram", (void (*)(void))glDeleteProgram},
            {"glDeleteRenderbuffers", (void (*)(void))glDeleteRenderbuffers},
            {"glDeleteShader", (void (*)(void))glDeleteShader},
            {"glDeleteTextures", (void (*)(void))glDeleteTextures},
            {"glDepthFunc", (void (*)(void))glDepthFunc},
            {"glDepthMask", (void (*)(void))glDepthMask},
            {"glDetachShader", (void (*)(void))glDetachShader},
            {"glDisable", (void (*)(void))glDisable},
            {"glDisableVertexAttribArray", (void (*)(void))glDisableVertexAttribArray},
            {"glDrawArrays", (void (*)(void))glDrawArrays},
            {"glDrawBuffers", (void (*)(void))glDrawBuffers},
            {"glDrawElements", (void (*)(void))glDrawElements},
            {"glEnable", (void (*)(void))glEnable},
            {"glEnableVertexAttribArray", (void (*)(void))glEnableVertexAttribArray},
            {"glFlush", (void (*)(void))glFlush},
            {"glFramebufferRenderbuffer", (void (*)(void))glFramebufferRenderbuffer},
            {"glFramebufferTexture2D", (void (*)(void))glFramebufferTexture2D},
            {"glGenBuffers", (void (*)(void))glGenBuffers},
            {"glGenFramebuffers", (void (*)(void))glGenFramebuffers},
            {"glGenRenderbuffers", (void (*)(void))glGenRenderbuffers},
            {"glGenTextures", (void (*)(void))glGenTextures},
            {"glGenerateMipmap", (void (*)(void))glGenerateMipmap},
            {"glGetActiveAttrib", (void (*)(void))glGetActiveAttrib},
            {"glGetActiveUniform", (void (*)(void))glGetActiveUniform},
            {"glGetAttribLocation", (void (*)(void))glGetAttribLocation},
            {"glGetBooleanv", (void (*)(void))glGetBooleanv},
            {"glGetBufferParameteriv", (void (*)(void))glGetBufferParameteriv},
            {"glGetError", (void (*)(void))glGetError},
            {"glGetFloatv", (void (*)(void))glGetFloatv},
            {"glGetFramebufferAttachmentParameteriv",
             (void (*)(void))glGetFramebufferAttachmentParameteriv},
            {"glGetIntegerv", (void (*)(void))glGetIntegerv},
            {"glGetProgramBinary", (void (*)(void))glGetProgramBinary},
            {"glGetProgramInfoLog", (void (*)(void))glGetProgramInfoLog},
            {"glGetProgramiv", (void (*)(void))glGetProgramiv},
            {"glGetRenderbufferParameteriv", (void (*)(void))glGetRenderbufferParameteriv},
            {"glGetShaderInfoLog", (void (*)(void))glGetShaderInfoLog},
            {"glGetShaderiv", (void (*)(void))glGetShaderiv},
            {"glGetString", (void (*)(void))glGetString},
            {"glGetTexParameteriv", (void (*)(void))glGetTexParameteriv},
            {"glGetUniformLocation", (void (*)(void))glGetUniformLocation},
            {"glGetUniformfv", (void (*)(void))glGetUniformfv},
            {"glGetUniformiv", (void (*)(void))glGetUniformiv},
            {"glGetVertexAttribPointerv", (void (*)(void))glGetVertexAttribPointerv},
            {"glGetVertexAttribiv", (void (*)(void))glGetVertexAttribiv},
            {"glHint", (void (*)(void))glHint},
            {"glInvalidateFramebuffer", (void (*)(void))glInvalidateFramebuffer},
            {"glIsEnabled", (void (*)(void))glIsEnabled},
            {"glIsFramebuffer", (void (*)(void))glIsFramebuffer},
            {"glIsTexture", (void (*)(void))glIsTexture},
            {"glLineWidth", (void (*)(void))glLineWidth},
            {"glLinkProgram", (void (*)(void))glLinkProgram},
            {"glPixelStorei", (void (*)(void))glPixelStorei},
            {"glProgramBinary", (void (*)(void))glProgramBinary},
            {"glReadPixels", (void (*)(void))glReadPixels},
            {"glRenderbufferStorage", (void (*)(void))glRenderbufferStorage},
            {"glRenderbufferStorageMultisample", (void (*)(void))glRenderbufferStorageMultisample},
            {"glSampleCoverage", (void (*)(void))glSampleCoverage},
            {"glScissor", (void (*)(void))glScissor},
            {"glShaderBinary", (void (*)(void))glShaderBinary},
            {"glShaderSource", (void (*)(void))glShaderSource},
            {"glStencilFunc", (void (*)(void))glStencilFunc},
            {"glStencilMask", (void (*)(void))glStencilMask},
            {"glStencilOp", (void (*)(void))glStencilOp},
            {"glTexImage2D", (void (*)(void))glTexImage2D},
            {"glTexParameterf", (void (*)(void))glTexParameterf},
            {"glTexParameteri", (void (*)(void))glTexParameteri},
            {"glTexStorage2D", (void (*)(void))glTexStorage2D},
            {"glTexStorage2DMultisample", (void (*)(void))glTexStorage2DMultisample},
            {"glTexStorage3D", (void (*)(void))glTexStorage3D},
            {"glTexSubImage2D", (void (*)(void))glTexSubImage2D},
            {"glUniform1f", (void (*)(void))glUniform1f},
            {"glUniform1fv", (void (*)(void))glUniform1fv},
            {"glUniform1i", (void (*)(void))glUniform1i},
            {"glUniform2f", (void (*)(void))glUniform2f},
            {"glUniform2fv", (void (*)(void))glUniform2fv},
            {"glUniform3f", (void (*)(void))glUniform3f},
            {"glUniform3fv", (void (*)(void))glUniform3fv},
            {"glUniform4f", (void (*)(void))glUniform4f},
            {"glUniform4fv", (void (*)(void))glUniform4fv},
            {"glUniformMatrix3fv", (void (*)(void))glUniformMatrix3fv},
            {"glUniformMatrix4fv", (void (*)(void))glUniformMatrix4fv},
            {"glUseProgram", (void (*)(void))glUseProgram},
            {"glVertexAttribPointer", (void (*)(void))glVertexAttribPointer},
            {"glViewport", (void (*)(void))glViewport}};

    for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const char *a = name, *b = entries[i].name;

        while (*a && *a == *b) {
            a++;
            b++;
        }

        if (!*a && !*b) {
            return entries[i].function;
        }
    }

    return 0;
}
