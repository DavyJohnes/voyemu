void glGetActiveAttrib(GLuint program, GLuint index, GLsizei cap, GLsizei *length, GLint *size,
                       GLenum *type, GLchar *name) {
    if (cap < 0 || cap > 3827) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 a[] = {program, index, cap};
    (void)call(300, a, 3, 0, 0);

    if (packet->out_len >= 12) {
        if (length) {
            copy(length, packet->data, 4);
        }

        if (size) {
            copy(size, packet->data + 4, 4);
        }

        if (type) {
            copy(type, packet->data + 8, 4);
        }

        if (name && cap && packet->out_len > 12) {
            copy(name, packet->data + 12, packet->out_len - 12);
        }
    }

    release();
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei cap, GLsizei *length, GLint *size,
                        GLenum *type, GLchar *name) {
    if (cap < 0 || cap > 3827) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 a[] = {program, index, cap};
    (void)call(301, a, 3, 0, 0);

    if (packet->out_len >= 12) {
        if (length) {
            copy(length, packet->data, 4);
        }

        if (size) {
            copy(size, packet->data + 4, 4);
        }

        if (type) {
            copy(type, packet->data + 8, 4);
        }

        if (name && cap && packet->out_len > 12) {
            copy(name, packet->data + 12, packet->out_len - 12);
        }
    }

    release();
}

static void info_log(unsigned op, GLuint object, GLsizei cap, GLsizei *length, GLchar *text) {
    if (cap < 0 || cap > MAX_BULK - 4) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 blob = download_begin((usize)cap + 4);
    u64 a[] = {object, cap, blob};
    u64 bytes = call(op, a, 3, 0, 0);

    if (bytes >= 4 && bytes <= (u64)cap + 4) {
        unsigned char *p = bulk_memory;

        if (length) {
            copy(length, p, 4);
        }

        if (text && bytes > 4) {
            copy(text, p + 4, bytes - 4);
        }
    } else {
        transport_error = 1;
    }

    bulk_free();
    drop_blob(blob);
    release();
}

void glGetProgramInfoLog(GLuint program, GLsizei cap, GLsizei *length, GLchar *text) {
    info_log(302, program, cap, length, text);
}

void glGetShaderInfoLog(GLuint shader, GLsizei cap, GLsizei *length, GLchar *text) {
    info_log(303, shader, cap, length, text);
}

void glGetProgramBinary(GLuint program, GLsizei cap, GLsizei *length, GLenum *format,
                        void *binary) {
    if (cap < 0 || cap > MAX_BULK - 8) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 blob = download_begin((usize)cap + 8);
    u64 a[] = {program, cap, blob};
    u64 bytes = call(304, a, 3, 0, 0);

    if (bytes >= 8 && bytes <= (u64)cap + 8) {
        unsigned char *p = bulk_memory;

        if (length) {
            copy(length, p, 4);
        }

        if (format) {
            copy(format, p + 4, 4);
        }

        if (binary && bytes > 8) {
            copy(binary, p + 8, bytes - 8);
        }
    } else {
        transport_error = 1;
    }

    bulk_free();
    drop_blob(blob);
    release();
}

void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum format, const void *binary,
                    GLsizei length) {
    if (count < 0 || length < 0) {
        transport_error = 1;

        return;
    }

    acquire();
    u64 objects = upload(shaders, (usize)count * 4), code = upload(binary, length);
    u64 a[] = {count, objects, format, code, (u64)length};
    (void)call(305, a, 5, 0, 0);
    drop_blob(objects);
    drop_blob(code);
    release();
}
