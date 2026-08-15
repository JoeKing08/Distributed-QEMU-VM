#include <stdio.h>
#include <string.h>

#include "wavevm_sha256.h"

static int expect_digest(const char *input, const uint8_t expected[32])
{
    uint8_t actual[WVM_SHA256_DIGEST_BYTES];

    wvm_sha256_digest(input, strlen(input), actual);
    if (memcmp(actual, expected, sizeof(actual)) != 0) {
        fprintf(stderr, "sha256 test: digest mismatch for '%s'\n", input);
        return -1;
    }
    return 0;
}

int main(void)
{
    static const uint8_t empty_digest[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };
    static const uint8_t abc_digest[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    struct wvm_sha256_ctx ctx;
    uint8_t incremental[WVM_SHA256_DIGEST_BYTES];

    if (expect_digest("", empty_digest) || expect_digest("abc", abc_digest)) {
        return 1;
    }

    wvm_sha256_init(&ctx);
    wvm_sha256_update(&ctx, "a", 1);
    wvm_sha256_update(&ctx, "b", 1);
    wvm_sha256_update(&ctx, "c", 1);
    wvm_sha256_final(&ctx, incremental);
    if (memcmp(incremental, abc_digest, sizeof(incremental)) != 0) {
        fprintf(stderr, "sha256 test: incremental digest mismatch\n");
        return 1;
    }

    puts("sha256 tests: PASS");
    return 0;
}
