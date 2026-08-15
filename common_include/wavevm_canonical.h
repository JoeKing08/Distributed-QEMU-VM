#ifndef WAVEVM_CANONICAL_H
#define WAVEVM_CANONICAL_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_sha256.h"

#define WVM_CANONICAL_SCHEMA_V1 1U
#define WVM_CANONICAL_RECORD_HEADER_BYTES 8U
#define WVM_CANONICAL_FIELD_HEADER_BYTES 8U

/*
 * A bounded encoder for the common WVM-TLV container.  Record-specific
 * schemas own which tags and values are valid; this layer enforces the
 * byte-order, framing, and monotonic-tag invariants common to every V1 record.
 */
struct wvm_canonical_builder {
    uint8_t *bytes;
    size_t capacity;
    size_t bytes_used;
    uint16_t record_type;
    uint16_t last_field_tag;
    int finished;
};

struct wvm_canonical_record {
    uint16_t schema_version;
    uint16_t record_type;
    const uint8_t *body;
    size_t body_bytes;
};

struct wvm_canonical_field {
    uint16_t tag;
    uint16_t flags;
    const uint8_t *value;
    uint32_t value_bytes;
};

int wvm_canonical_record_begin(struct wvm_canonical_builder *builder,
                               uint8_t *bytes, size_t capacity,
                               uint16_t record_type);

int wvm_canonical_field_append(struct wvm_canonical_builder *builder,
                               uint16_t field_tag, const void *value,
                               uint32_t value_bytes);

/*
 * Reserve one field value for a nested canonical record or list.  The caller
 * must fill exactly value_bytes at *value_out before finishing the record.
 */
int wvm_canonical_field_reserve(struct wvm_canonical_builder *builder,
                                uint16_t field_tag, uint32_t value_bytes,
                                uint8_t **value_out);

int wvm_canonical_field_append_u16(struct wvm_canonical_builder *builder,
                                   uint16_t field_tag, uint16_t value);

int wvm_canonical_field_append_u32(struct wvm_canonical_builder *builder,
                                   uint16_t field_tag, uint32_t value);

int wvm_canonical_field_append_u64(struct wvm_canonical_builder *builder,
                                   uint16_t field_tag, uint64_t value);

int wvm_canonical_record_finish(struct wvm_canonical_builder *builder,
                                size_t *record_bytes);

/*
 * Parse and structurally validate one complete V1 record.  Record-specific
 * callers must still validate the known tag set, widths, enum values, and
 * cross-field constraints after this function succeeds.
 */
int wvm_canonical_record_parse(const uint8_t *bytes, size_t record_bytes,
                               struct wvm_canonical_record *record);

/*
 * Iterate fields after wvm_canonical_record_parse().  Set *field_offset to
 * zero for the first field.  It returns 1 for one field, 0 at end, and -1 for
 * malformed input.
 */
int wvm_canonical_record_next(const struct wvm_canonical_record *record,
                              size_t *field_offset,
                              struct wvm_canonical_field *field);

/*
 * Calculate the SHA-256 digest of one canonical record.  A nonzero
 * self_digest_tag must name an exactly 32-byte field; that field is supplied
 * as zero bytes to the hash, without modifying the source record.
 */
int wvm_canonical_record_digest(const uint8_t *bytes, size_t record_bytes,
                                uint16_t self_digest_tag,
                                uint8_t digest[WVM_SHA256_DIGEST_BYTES]);

#endif /* WAVEVM_CANONICAL_H */
