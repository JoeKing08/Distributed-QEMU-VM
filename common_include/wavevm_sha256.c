#include "wavevm_sha256.h"

#include <string.h>

static const uint32_t round_constants[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

static uint32_t rotate_right(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

static uint32_t read_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | src[3];
}

static void write_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static void write_be64(uint8_t *dst, uint64_t value)
{
    dst[0] = (uint8_t)(value >> 56);
    dst[1] = (uint8_t)(value >> 48);
    dst[2] = (uint8_t)(value >> 40);
    dst[3] = (uint8_t)(value >> 32);
    dst[4] = (uint8_t)(value >> 24);
    dst[5] = (uint8_t)(value >> 16);
    dst[6] = (uint8_t)(value >> 8);
    dst[7] = (uint8_t)value;
}

static void transform(struct wvm_sha256_ctx *ctx,
                      const uint8_t block[WVM_SHA256_BLOCK_BYTES])
{
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t i;

    for (i = 0; i < 16; i++) {
        words[i] = read_be32(block + i * 4);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^
                      rotate_right(words[i - 15], 18) ^
                      (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^
                      rotate_right(words[i - 2], 19) ^
                      (words[i - 2] >> 10);

        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                        rotate_right(e, 25);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choose + round_constants[i] + words[i];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                        rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void wvm_sha256_init(struct wvm_sha256_ctx *ctx)
{
    if (!ctx) {
        return;
    }

    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->total_bytes = 0;
    ctx->block_bytes = 0;
}

void wvm_sha256_update(struct wvm_sha256_ctx *ctx, const void *data,
                       size_t data_bytes)
{
    const uint8_t *input = data;

    if (!ctx || (!input && data_bytes != 0)) {
        return;
    }

    ctx->total_bytes += data_bytes;
    while (data_bytes != 0) {
        size_t copy_bytes = WVM_SHA256_BLOCK_BYTES - ctx->block_bytes;

        if (copy_bytes > data_bytes) {
            copy_bytes = data_bytes;
        }
        memcpy(ctx->block + ctx->block_bytes, input, copy_bytes);
        ctx->block_bytes += copy_bytes;
        input += copy_bytes;
        data_bytes -= copy_bytes;

        if (ctx->block_bytes == WVM_SHA256_BLOCK_BYTES) {
            transform(ctx, ctx->block);
            ctx->block_bytes = 0;
        }
    }
}

void wvm_sha256_final(struct wvm_sha256_ctx *ctx,
                      uint8_t digest[WVM_SHA256_DIGEST_BYTES])
{
    uint64_t bit_length;
    uint8_t padding[WVM_SHA256_BLOCK_BYTES] = {0x80};
    uint8_t encoded_length[sizeof(bit_length)];
    uint32_t i;
    size_t padding_bytes;

    if (!ctx || !digest) {
        return;
    }

    bit_length = ctx->total_bytes << 3;
    padding_bytes = ctx->block_bytes < 56 ? 56 - ctx->block_bytes
                                          : 120 - ctx->block_bytes;
    wvm_sha256_update(ctx, padding, padding_bytes);
    write_be64(encoded_length, bit_length);
    wvm_sha256_update(ctx, encoded_length, sizeof(encoded_length));

    for (i = 0; i < 8; i++) {
        write_be32(digest + i * 4, ctx->state[i]);
    }
}

void wvm_sha256_digest(const void *data, size_t data_bytes,
                       uint8_t digest[WVM_SHA256_DIGEST_BYTES])
{
    struct wvm_sha256_ctx ctx;

    wvm_sha256_init(&ctx);
    wvm_sha256_update(&ctx, data, data_bytes);
    wvm_sha256_final(&ctx, digest);
}
