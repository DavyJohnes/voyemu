/* Keep premultiplied alpha intact; GL RGBA becomes native Screen BGRA. */
static int screen_copy_rgba(void *target, int stride, const void *source, int width, int height) {
    if (!target || !source || width < 1 || width > 4096 || height < 1 || height > 2160 ||
        stride < width * 4 || stride > 65536 || (usize)stride * height > (usize)64 * 1024 * 1024) {
        return 0;
    }

    for (int y = 0; y < height; y++) {
        const unsigned char *src = (const unsigned char *)source +
                                   (usize)(height - 1 - y) * width * 4;
        unsigned char *dst = (unsigned char *)target + (usize)y * stride;
        int x = 0;
        typedef unsigned char Pixels __attribute__((vector_size(16)));

        for (; x + 4 <= width; x += 4) {
            Pixels input;
            __builtin_memcpy(&input, src + (usize)x * 4, 16);
            Pixels output = __builtin_shufflevector(input, input, 2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8,
                                                    11, 14, 13, 12, 15);
            __builtin_memcpy(dst + (usize)x * 4, &output, 16);
        }

        for (; x < width; x++) {
            dst[(usize)x * 4] = src[(usize)x * 4 + 2];
            dst[(usize)x * 4 + 1] = src[(usize)x * 4 + 1];
            dst[(usize)x * 4 + 2] = src[(usize)x * 4];
            dst[(usize)x * 4 + 3] = src[(usize)x * 4 + 3];
        }
    }

    return 1;
}
