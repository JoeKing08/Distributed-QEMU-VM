#ifndef WAVEVM_X86_CONTEXT_H
#define WAVEVM_X86_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include "wavevm_vcpu_handoff.h"

/*
 * This schema carries the current QEMU 5.2 x86 context containers as a
 * capability-bound profile blob. The fixed prefix prevents a legacy IPC union
 * from being mistaken for a typed context. It is only accepted between
 * participants that negotiated the same backend and context schema.
 */
#define WVM_X86_CONTEXT_MAGIC 0x57565843U /* "WVXC" */
#define WVM_X86_CONTEXT_SCHEMA_VERSION WVM_VCPU_CONTEXT_SCHEMA_X86
#define WVM_X86_CONTEXT_WIRE_HEADER_BYTES 24U

size_t wvm_x86_context_legacy_bytes(uint16_t backend);

int wvm_x86_context_validate(uint16_t expected_backend,
                             const uint8_t *input, size_t input_bytes,
                             uint64_t *valid_fields_out, char *error,
                             size_t error_len);

int wvm_x86_context_encode(uint16_t backend, uint64_t valid_fields,
                           const void *legacy_context,
                           size_t legacy_context_bytes, uint8_t *output,
                           size_t output_capacity, size_t *output_bytes,
                           char *error, size_t error_len);

int wvm_x86_context_decode(uint16_t expected_backend,
                           const uint8_t *input, size_t input_bytes,
                           uint64_t *valid_fields_out, void *legacy_context,
                           size_t legacy_context_capacity, char *error,
                           size_t error_len);

#endif /* WAVEVM_X86_CONTEXT_H */
