/* Preserve guest client-array pointers locally; copy only the draw extent. */
static unsigned element_bytes(unsigned type) {
    switch (type) {
    case 0x1400:
    case 0x1401:
        return 1;
    case 0x1402:
    case 0x1403:
    case 0x140b:
        return 2;
    case 0x1404:
    case 0x1405:
    case 0x1406:
    case 0x140c:
        return 4;
    default:
        return 0;
    }
}

void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer) {
    if (index >= 64 || size < 1 || size > 4 || stride < 0 || !element_bytes(type)) {
        transport_error = 1;

        return;
    }

    acquire();
    ClientArray *a = &client_arrays[index];
    a->pointer = pointer;
    a->size = size;
    a->type = type;
    a->normalized = normalized;
    a->stride = stride;
    a->buffer = client_array_buffer;

    if (client_array_buffer || !pointer) {
        u64 args[] = {index, size, type, normalized, stride, (u64)pointer};
        (void)call(195, args, 6, 0, 0);
    }

    release();
}

void glGetVertexAttribPointerv(GLuint index, GLenum pname, void **pointer) {
    if (index >= 64 || pname != 0x8645 || !pointer) {
        transport_error = 1;

        return;
    }

    *pointer = (void *)client_arrays[index].pointer;
}

static int prepare_client_arrays(usize vertices) {
    if (!vertices) {
        return 1;
    }

    for (unsigned index = 0; index < 64; index++) {
        ClientArray *a = &client_arrays[index];

        if (!a->enabled || a->buffer || !a->pointer) {
            continue;
        }

        usize element = (usize)a->size * element_bytes(a->type),
              stride = a->stride ? a->stride : element;

        if (!element || vertices > MAX_BULK / stride ||
            (vertices - 1) * stride > MAX_BULK - element) {
            transport_error = 1;

            return 0;
        }

        usize bytes = (vertices - 1) * stride + element;

        if (bytes <= 3840) {
            u64 args[] = {index, a->size, a->type, a->normalized, a->stride, 0, vertices};

            if (!send_void(350, args, 7, a->pointer, (unsigned)bytes)) {
                transport_error = 1;

                return 0;
            }

            continue;
        }

        u64 blob = upload(a->pointer, bytes);

        if (!blob) {
            return 0;
        }

        u64 args[] = {index, a->size, a->type, a->normalized, a->stride, blob};
        u64 ok = call(350, args, 6, 0, 0);
        drop_blob(blob);

        if (!ok) {
            transport_error = 1;

            return 0;
        }
    }

    return 1;
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    unsigned bytes = element_bytes(type);

    if (count < 0 || (type != 0x1401 && type != 0x1403 && type != 0x1405) ||
        (usize)count * bytes > MAX_BULK) {
        transport_error = 1;

        return;
    }

    acquire();
    usize vertices = 0;
    u64 blob = 0;

    if (count && client_element_buffer) {
        u64 args[] = {count, type, (u64)indices};
        u64 n = call(352, args, 3, 0, 0);

        if (!n) {
            transport_error = 1;
            release();

            return;
        }

        vertices = n;
    } else if (count) {
        if (!indices) {
            transport_error = 1;
            release();

            return;
        }

        const unsigned char *p = indices;
        u32 maximum = 0;

        for (int i = 0; i < count; i++) {
            u32 value = 0;
            copy(&value, p + (u64)i * bytes, bytes);

            if (value > maximum) {
                maximum = value;
            }
        }

        vertices = (usize)maximum + 1;
        blob = upload(indices, (usize)count * bytes);

        if (!blob) {
            release();

            return;
        }
    }

    if (prepare_client_arrays(vertices)) {
        u64 args[] = {mode, count, type, blob ? blob : (u64)indices};
        (void)call(blob ? 351 : 133, args, 4, 0, 0);
    }

    drop_blob(blob);
    release();
}
