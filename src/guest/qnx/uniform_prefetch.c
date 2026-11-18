extern char *getenv(const char *);
#include "uniform_names.inc"
typedef struct {
    u32 program, stamp;
    GLint locations[UNIFORM_COUNT];
} UniformProgram;
static UniformProgram uniform_programs[64];
static u32 uniform_stamp;
static u64 uniform_hits, uniform_missing_table, uniform_unknown_name, uniform_evictions;

static int uniform_enabled(void) {
    const char *v = getenv("QGFX_UNIFORM_PREFETCH");

    return v && v[0] == '1' && !v[1];
}

static void uniform_clear(void) {
    for (unsigned i = 0; i < 64; i++) {
        uniform_programs[i].stamp = 0;
    }
}

static void uniform_forget(u32 program) {
    for (unsigned i = 0; i < 64; i++) {
        if (uniform_programs[i].program == program) {
            uniform_programs[i].stamp = 0;
        }
    }
}

static u32 uniform_tick(void) {
    if (++uniform_stamp == 0) {
        uniform_clear();
        uniform_stamp = 1;
    }

    return uniform_stamp;
}

static int uniform_same(const char *a, const char *b) {
    for (unsigned i = 0; i <= 128; i++) {
        if (a[i] != b[i]) {
            return 0;
        }

        if (!a[i]) {
            return 1;
        }
    }

    return 0;
}

/* Caller owns api_lock; no cache entry is installed until the complete reply validates. */
static void uniform_program_info(GLuint program, GLint *value) {
    u64 args[] = {program, UNIFORM_SCHEMA};

    (void)call(358, args, 2, 0, 0);
    uniform_forget(program);

    if (!value || !packet || packet->state != 2 || packet->seq != sequence || packet->fault ||
        packet->out_len < 16) {
        transport_error = 1;

        return;
    }

    GLint linked;
    u32 count;
    u64 schema;
    copy(&linked, packet->data, 4);
    copy(&count, packet->data + 4, 4);
    copy(&schema, packet->data + 8, 8);

    if (schema != UNIFORM_SCHEMA || (linked != 0 && linked != 1) ||
        count != (linked ? UNIFORM_COUNT : 0) || packet->out_len != 16 + count * 4) {
        transport_error = 1;

        return;
    }

    *value = linked;

    if (!linked) {
        return;
    }

    unsigned slot = 0;

    for (unsigned i = 0; i < 64; i++) {
        if (!uniform_programs[i].stamp) {
            slot = i;
            break;
        }

        if (uniform_programs[i].stamp < uniform_programs[slot].stamp) {
            slot = i;
        }
    }

    if (uniform_programs[slot].stamp) {
        uniform_evictions++;
    }

    uniform_programs[slot].program = program;
    uniform_programs[slot].stamp = uniform_tick();
    copy(uniform_programs[slot].locations, packet->data + 16, UNIFORM_COUNT * 4);
}

static GLint uniform_get_location(GLuint program, const GLchar *name, unsigned opcode) {
    acquire();

    if (!name) {
        transport_error = 1;
        release();

        return -1;
    }

    if (uniform_enabled()) {
        for (unsigned i = 0; i < 64; i++) {
            if (uniform_programs[i].stamp && uniform_programs[i].program == program) {
                for (unsigned n = 0; n < UNIFORM_COUNT; n++) {
                    if (uniform_same(name, uniform_names[n])) {
                        uniform_programs[i].stamp = uniform_tick();
                        GLint location = uniform_programs[i].locations[n];
                        uniform_hits++;
                        release();

                        return location;
                    }
                }

                uniform_unknown_name++;
                goto uniform_fallback;
            }
        }
    }

    if (uniform_enabled()) {
        uniform_missing_table++;
    }

uniform_fallback:;
    u64 args[] = {program, 0};
    u64 result = call(opcode, args, 2, name, (unsigned)length(name) + 1);
    release();

    return (GLint)result;
}
