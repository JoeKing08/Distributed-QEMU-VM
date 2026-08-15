#ifndef WAVEVM_SHA256_H
#define WAVEVM_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define WVM_SHA256_DIGEST_BYTES 32U
#define WVM_SHA256_BLOCK_BYTES 64U

struct wvm_sha256_ctx {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[WVM_SHA256_BLOCK_BYTES];
    size_t block_bytes;
};

void wvm_sha256_init(struct wvm_sha256_ctx *ctx);
void wvm_sha256_update(struct wvm_sha256_ctx *ctx, const void *data,
                       size_t data_bytes);
void wvm_sha256_final(struct wvm_sha256_ctx *ctx,
                      uint8_t digest[WVM_SHA256_DIGEST_BYTES]);
void wvm_sha256_digest(const void *data, size_t data_bytes,
                       uint8_t digest[WVM_SHA256_DIGEST_BYTES]);

#endif /* WAVEVM_SHA256_H */
