#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wavevm_canonical.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "canonical-record test: %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    static const uint8_t ipv4[4] = {10, 0, 0, 1};
    static const uint8_t expected[] = {
        0x00, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 0x40,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0a, 0x00,
        0x00, 0x01,
        0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x23, 0x28,
        0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02,
        0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0a, 0x00,
        0x00, 0x01,
        0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x23, 0x29,
    };
    struct wvm_canonical_builder builder;
    struct wvm_canonical_record record;
    struct wvm_canonical_field field;
    uint8_t encoded[sizeof(expected)];
    uint8_t digest_record[128];
    uint8_t expected_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t actual_digest[WVM_SHA256_DIGEST_BYTES];
    uint8_t malformed[sizeof(expected)];
    uint8_t zero_digest[WVM_SHA256_DIGEST_BYTES] = {0};
    size_t encoded_bytes;
    size_t digest_record_bytes;
    size_t field_offset;
    unsigned int fields = 0;

    if (expect(wvm_canonical_record_begin(&builder, encoded, sizeof(encoded),
                                          0x1001) == 0,
               "begin Endpoint") ||
        expect(wvm_canonical_field_append_u16(&builder, 1, 1) == 0,
               "append data transport") ||
        expect(wvm_canonical_field_append(&builder, 2, ipv4, sizeof(ipv4)) == 0,
               "append data address") ||
        expect(wvm_canonical_field_append_u16(&builder, 3, 9000) == 0,
               "append data port") ||
        expect(wvm_canonical_field_append_u16(&builder, 4, 2) == 0,
               "append control transport") ||
        expect(wvm_canonical_field_append(&builder, 5, ipv4, sizeof(ipv4)) == 0,
               "append control address") ||
        expect(wvm_canonical_field_append_u16(&builder, 6, 9001) == 0,
               "append control port") ||
        expect(wvm_canonical_record_finish(&builder, &encoded_bytes) == 0,
               "finish Endpoint") ||
        expect(encoded_bytes == sizeof(expected), "Endpoint length") ||
        expect(memcmp(encoded, expected, sizeof(expected)) == 0,
               "Endpoint bytes must match canonical fixture") ||
        expect(wvm_canonical_record_parse(encoded, encoded_bytes, &record) == 0,
               "parse Endpoint") ||
        expect(record.record_type == 0x1001, "Endpoint record type")) {
        return 1;
    }

    field_offset = 0;
    while (wvm_canonical_record_next(&record, &field_offset, &field) == 1) {
        fields++;
        if (expect(field.tag == fields, "field tag order") ||
            expect(field.flags == 0, "V1 field flags")) {
            return 1;
        }
    }
    if (expect(fields == 6, "Endpoint field count")) {
        return 1;
    }

    if (expect(wvm_canonical_record_begin(&builder, digest_record,
                                          sizeof(digest_record), 0x1014) == 0,
               "begin self-digest record") ||
        expect(wvm_canonical_field_append(&builder, 1, zero_digest,
                                          sizeof(zero_digest)) == 0,
               "append self-digest field") ||
        expect(wvm_canonical_field_append_u32(&builder, 2, 256) == 0,
               "append ordinary field") ||
        expect(wvm_canonical_record_finish(&builder, &digest_record_bytes) == 0,
               "finish self-digest record") ||
        expect(wvm_canonical_record_digest(digest_record, digest_record_bytes,
                                           1, actual_digest) == 0,
               "digest self-digest record")) {
        return 1;
    }
    wvm_sha256_digest(digest_record, digest_record_bytes, expected_digest);
    if (expect(memcmp(actual_digest, expected_digest, sizeof(actual_digest)) == 0,
               "zero self-digest preimage")) {
        return 1;
    }
    memcpy(digest_record + WVM_CANONICAL_RECORD_HEADER_BYTES +
               WVM_CANONICAL_FIELD_HEADER_BYTES,
           actual_digest, sizeof(actual_digest));
    if (expect(wvm_canonical_record_digest(digest_record, digest_record_bytes,
                                           1, expected_digest) == 0,
               "digest populated self-digest record") ||
        expect(memcmp(actual_digest, expected_digest, sizeof(actual_digest)) == 0,
               "self-digest must not change final digest")) {
        return 1;
    }

    if (expect(wvm_canonical_record_begin(&builder, encoded, sizeof(encoded),
                                          0x1001) == 0,
               "begin order check") ||
        expect(wvm_canonical_field_append_u16(&builder, 2, 1) == 0,
               "append initial ordered field") ||
        expect(wvm_canonical_field_append_u16(&builder, 1, 1) != 0,
               "reject out-of-order field")) {
        return 1;
    }

    memcpy(malformed, expected, sizeof(malformed));
    malformed[18] = 0x00;
    malformed[19] = 0x01;
    if (expect(wvm_canonical_record_parse(malformed, sizeof(malformed),
                                          &record) != 0,
               "reject duplicate field tag")) {
        return 1;
    }

    memcpy(malformed, expected, sizeof(malformed));
    malformed[12] = 0x00;
    malformed[13] = 0x01;
    if (expect(wvm_canonical_record_parse(malformed, sizeof(malformed),
                                          &record) != 0,
               "reject nonzero field flags")) {
        return 1;
    }

    memcpy(malformed, expected, sizeof(malformed));
    malformed[7]--;
    if (expect(wvm_canonical_record_parse(malformed, sizeof(malformed),
                                          &record) != 0,
               "reject mismatched body length")) {
        return 1;
    }

    puts("canonical-record tests: PASS");
    return 0;
}
