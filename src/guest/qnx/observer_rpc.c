/* Caller owns api_lock. Metadata failures must not become EGL/GLES errors. */
static u64 observation_call(u32 op, const u64 *args, unsigned count, const void *data,
                            unsigned bytes) {
    if (op != 356 && op != 357 && op != 359) {
        return 0;
    }

    int before = transport_error;
    u64 result = call_raw(op, args, count, data, bytes);
    transport_error = before;

    return result;
}
