#include "wavevm_canonical.h"

#include <limits.h>
#include <string.h>

static void write_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
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

static uint16_t read_be16(const uint8_t *src)
{
    return ((uint16_t)src[0] << 8) | src[1];
}

static uint32_t read_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | src[3];
}

int wvm_canonical_record_begin(struct wvm_canonical_builder *builder,
                               uint8_t *bytes, size_t capacity,
                               uint16_t record_type)
{
    if (!builder || !bytes || capacity < WVM_CANONICAL_RECORD_HEADER_BYTES ||
        record_type == 0) {
        return -1;
    }

    memset(builder, 0, sizeof(*builder));
    builder->bytes = bytes;
    builder->capacity = capacity;
    builder->bytes_used = WVM_CANONICAL_RECORD_HEADER_BYTES;
    builder->record_type = record_type;
    return 0;
}

int wvm_canonical_field_reserve(struct wvm_canonical_builder *builder,
                                uint16_t field_tag, uint32_t value_bytes,
                                uint8_t **value_out)
{
    size_t field_bytes;
    uint8_t *field;

    if (!builder || !builder->bytes || builder->finished || field_tag == 0 ||
        field_tag <= builder->last_field_tag || !value_out) {
        return -1;
    }

    field_bytes = WVM_CANONICAL_FIELD_HEADER_BYTES + (size_t)value_bytes;
    if (field_bytes < WVM_CANONICAL_FIELD_HEADER_BYTES ||
        field_bytes > builder->capacity - builder->bytes_used) {
        return -1;
    }

    field = builder->bytes + builder->bytes_used;
    write_be16(field, field_tag);
    write_be16(field + 2, 0);
    write_be32(field + 4, value_bytes);

    builder->bytes_used += field_bytes;
    builder->last_field_tag = field_tag;
    *value_out = field + WVM_CANONICAL_FIELD_HEADER_BYTES;
    return 0;
}

int wvm_canonical_field_append(struct wvm_canonical_builder *builder,
                               uint16_t field_tag, const void *value,
                               uint32_t value_bytes)
{
    uint8_t *field_value;

    if ((!value && value_bytes != 0) ||
        wvm_canonical_field_reserve(builder, field_tag, value_bytes,
                                    &field_value) != 0) {
        return -1;
    }
    if (value_bytes != 0) {
        memcpy(field_value, value, value_bytes);
    }
    return 0;
}

int wvm_canonical_field_append_u16(struct wvm_canonical_builder *builder,
                                   uint16_t field_tag, uint16_t value)
{
    uint8_t encoded[sizeof(value)];

    write_be16(encoded, value);
    return wvm_canonical_field_append(builder, field_tag, encoded,
                                      sizeof(encoded));
}

int wvm_canonical_field_append_u32(struct wvm_canonical_builder *builder,
                                   uint16_t field_tag, uint32_t value)
{
    uint8_t encoded[sizeof(value)];

    write_be32(encoded, value);
    return wvm_canonical_field_append(builder, field_tag, encoded,
                                      sizeof(encoded));
}

int wvm_canonical_field_append_u64(struct wvm_canonical_builder *builder,
                                   uint16_t field_tag, uint64_t value)
{
    uint8_t encoded[sizeof(value)];

    write_be64(encoded, value);
    return wvm_canonical_field_append(builder, field_tag, encoded,
                                      sizeof(encoded));
}

int wvm_canonical_record_finish(struct wvm_canonical_builder *builder,
                                size_t *record_bytes)
{
    size_t body_bytes;

    if (!builder || !builder->bytes || builder->finished ||
        builder->bytes_used < WVM_CANONICAL_RECORD_HEADER_BYTES) {
        return -1;
    }

    body_bytes = builder->bytes_used - WVM_CANONICAL_RECORD_HEADER_BYTES;
    if (body_bytes > UINT32_MAX) {
        return -1;
    }

    write_be16(builder->bytes, WVM_CANONICAL_SCHEMA_V1);
    write_be16(builder->bytes + 2, builder->record_type);
    write_be32(builder->bytes + 4, (uint32_t)body_bytes);
    builder->finished = 1;

    if (record_bytes) {
        *record_bytes = builder->bytes_used;
    }
    return 0;
}

int wvm_canonical_record_parse(const uint8_t *bytes, size_t record_bytes,
                               struct wvm_canonical_record *record)
{
    size_t body_bytes;
    size_t offset;
    uint16_t previous_tag = 0;

    if (!bytes || !record ||
        record_bytes < WVM_CANONICAL_RECORD_HEADER_BYTES ||
        read_be16(bytes) != WVM_CANONICAL_SCHEMA_V1 ||
        read_be16(bytes + 2) == 0) {
        return -1;
    }

    body_bytes = read_be32(bytes + 4);
    if (body_bytes != record_bytes - WVM_CANONICAL_RECORD_HEADER_BYTES) {
        return -1;
    }

    offset = 0;
    while (offset < body_bytes) {
        const uint8_t *field;
        uint16_t field_tag;
        uint16_t field_flags;
        uint32_t value_bytes;

        if (body_bytes - offset < WVM_CANONICAL_FIELD_HEADER_BYTES) {
            return -1;
        }
        field = bytes + WVM_CANONICAL_RECORD_HEADER_BYTES + offset;
        field_tag = read_be16(field);
        field_flags = read_be16(field + 2);
        value_bytes = read_be32(field + 4);
        if (field_tag == 0 || field_tag <= previous_tag || field_flags != 0 ||
            (size_t)value_bytes > body_bytes - offset -
                                      WVM_CANONICAL_FIELD_HEADER_BYTES) {
            return -1;
        }

        previous_tag = field_tag;
        offset += WVM_CANONICAL_FIELD_HEADER_BYTES + (size_t)value_bytes;
    }

    record->schema_version = read_be16(bytes);
    record->record_type = read_be16(bytes + 2);
    record->body = bytes + WVM_CANONICAL_RECORD_HEADER_BYTES;
    record->body_bytes = body_bytes;
    return 0;
}

int wvm_canonical_record_next(const struct wvm_canonical_record *record,
                              size_t *field_offset,
                              struct wvm_canonical_field *field)
{
    const uint8_t *encoded;
    uint32_t value_bytes;

    if (!record || !field_offset || !field || *field_offset > record->body_bytes) {
        return -1;
    }
    if (*field_offset == record->body_bytes) {
        return 0;
    }
    if (record->body_bytes - *field_offset <
        WVM_CANONICAL_FIELD_HEADER_BYTES) {
        return -1;
    }

    encoded = record->body + *field_offset;
    value_bytes = read_be32(encoded + 4);
    if ((size_t)value_bytes > record->body_bytes - *field_offset -
                                  WVM_CANONICAL_FIELD_HEADER_BYTES) {
        return -1;
    }

    field->tag = read_be16(encoded);
    field->flags = read_be16(encoded + 2);
    field->value_bytes = value_bytes;
    field->value = encoded + WVM_CANONICAL_FIELD_HEADER_BYTES;
    *field_offset += WVM_CANONICAL_FIELD_HEADER_BYTES + (size_t)value_bytes;
    return 1;
}

int wvm_canonical_record_digest(const uint8_t *bytes, size_t record_bytes,
                                uint16_t self_digest_tag,
                                uint8_t digest[WVM_SHA256_DIGEST_BYTES])
{
    static const uint8_t zero_digest[WVM_SHA256_DIGEST_BYTES];
    struct wvm_canonical_record record;
    struct wvm_canonical_field field;
    struct wvm_sha256_ctx ctx;
    size_t field_offset = 0;
    size_t hashed_bytes = 0;
    int found_self_digest = 0;
    int next;

    if (!digest ||
        wvm_canonical_record_parse(bytes, record_bytes, &record) != 0) {
        return -1;
    }
    if (self_digest_tag == 0) {
        wvm_sha256_digest(bytes, record_bytes, digest);
        return 0;
    }

    wvm_sha256_init(&ctx);
    while (1) {
        next = wvm_canonical_record_next(&record, &field_offset, &field);
        if (next < 0) {
            return -1;
        }
        if (next == 0) {
            break;
        }
        if (field.tag != self_digest_tag) {
            continue;
        }
        if (field.value_bytes != WVM_SHA256_DIGEST_BYTES) {
            return -1;
        }

        wvm_sha256_update(&ctx, bytes + hashed_bytes,
                          (size_t)(field.value - bytes) - hashed_bytes);
        wvm_sha256_update(&ctx, zero_digest, sizeof(zero_digest));
        hashed_bytes = (size_t)(field.value - bytes) + field.value_bytes;
        found_self_digest = 1;
        break;
    }
    if (!found_self_digest) {
        return -1;
    }

    wvm_sha256_update(&ctx, bytes + hashed_bytes, record_bytes - hashed_bytes);
    wvm_sha256_final(&ctx, digest);
    return 0;
}
