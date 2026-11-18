#include "e2fs_helper_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(const char *data, size_t len, size_t chunk, const char *hex) {
    Sha256 ctx;
    unsigned char digest[32];
    char out[65];

    sha256_init(&ctx);

    for (size_t at = 0; at < len; at += chunk) {
        sha256_update(&ctx, data + at, len - at < chunk ? len - at : chunk);
    }

    sha256_final(&ctx, digest);

    for (size_t i = 0; i < 32; i++) {
        snprintf(out + 2 * i, 3, "%02x", digest[i]);
    }

    if (strcmp(out, hex) != 0) {
        fprintf(stderr, "%zu bytes in chunks of %zu: %s != %s\n", len, chunk, out, hex);
        failures++;
    }
}

int main(void) {
    static const char two[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    char *million = malloc(1000000);

    if (!million) {
        return 1;
    }

    memset(million, 'a', 1000000);
    expect("", 0, 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect("abc", 3, 1, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect(two, sizeof two - 1, 64,
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    expect(two, sizeof two - 1, 7,
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    expect(million, 1000000, 65536,
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    expect(million, 1000000, 1000000,
           "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    free(million);

    if (!failures) {
        puts("e2fsprogs helper SHA-256 vectors passed");
    }

    return failures != 0;
}
