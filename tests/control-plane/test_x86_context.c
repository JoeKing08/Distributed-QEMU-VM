#include <stdio.h>
#include <string.h>

#include "wavevm_protocol.h"
#include "wavevm_x86_context.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "x86 context test: %s\n", message);
        return -1;
    }
    return 0;
}

static int check_round_trip(uint16_t backend, const void *source,
                            size_t source_bytes, uint64_t valid_fields,
                            uint8_t *encoded, size_t encoded_capacity)
{
    uint8_t restored[sizeof(wvm_tcg_context_t)];
    size_t encoded_bytes = 0;
    uint64_t decoded_fields = 0;
    char error[256] = {0};

    memset(restored, 0, sizeof(restored));
    return expect(wvm_x86_context_encode(
                      backend, valid_fields, source, source_bytes, encoded,
                      encoded_capacity, &encoded_bytes, error,
                      sizeof(error)) == 0,
                  "encode context") ||
                   expect(wvm_x86_context_decode(
                              backend, encoded, encoded_bytes, &decoded_fields,
                              restored, sizeof(restored), error,
                              sizeof(error)) == 0 &&
                              decoded_fields == valid_fields &&
                              memcmp(restored, source, source_bytes) == 0,
                          "decode exact context")
               ? -1
               : 0;
}

int main(void)
{
    wvm_kvm_context_t kvm_context;
    wvm_tcg_context_t tcg_context;
    uint8_t encoded[WVM_X86_CONTEXT_WIRE_HEADER_BYTES +
                    sizeof(wvm_tcg_context_t)];
    size_t encoded_bytes = 0;
    uint64_t fields = WVM_VCPU_CONTEXT_FIELD_ARCHITECTURAL_STATE |
                      WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE |
                      WVM_VCPU_CONTEXT_FIELD_TIMER_STATE |
                      WVM_VCPU_CONTEXT_FIELD_DEVICE_RESUME;
    uint64_t decoded_fields = 0;
    char error[256] = {0};

    memset(&kvm_context, 0xa5, sizeof(kvm_context));
    memset(&tcg_context, 0x5a, sizeof(tcg_context));
    kvm_context.rip = UINT64_C(0x0123456789abcdef);
    kvm_context.tsc_deadline = UINT64_C(0xfedcba9876543210);
    tcg_context.eip = UINT64_C(0x1122334455667788);
    tcg_context.lapic.timer_expiry = -123456789;

    if (check_round_trip(WVM_VCPU_BACKEND_KVM, &kvm_context,
                         sizeof(kvm_context), fields, encoded,
                         sizeof(encoded)) != 0 ||
        check_round_trip(WVM_VCPU_BACKEND_TCG, &tcg_context,
                         sizeof(tcg_context), fields, encoded,
                         sizeof(encoded)) != 0) {
        return 1;
    }

    if (expect(wvm_x86_context_encode(
                   WVM_VCPU_BACKEND_KVM,
                   WVM_VCPU_CONTEXT_FIELD_INTERRUPT_STATE, &kvm_context,
                   sizeof(kvm_context), encoded, sizeof(encoded),
                   &encoded_bytes, error, sizeof(error)) != 0,
               "reject context without architectural state") ||
        expect(wvm_x86_context_encode(
                   WVM_VCPU_BACKEND_TCG, fields, &tcg_context,
                   sizeof(tcg_context) - 1, encoded, sizeof(encoded),
                   &encoded_bytes, error, sizeof(error)) != 0,
               "reject truncated legacy container") ||
        expect(wvm_x86_context_encode(
                   WVM_VCPU_BACKEND_TCG, fields, &tcg_context,
                   sizeof(tcg_context), encoded, sizeof(encoded),
                   &encoded_bytes, error, sizeof(error)) == 0,
               "encode TCG context for negative checks")) {
        return 1;
    }

    if (expect(wvm_x86_context_decode(
                   WVM_VCPU_BACKEND_KVM, encoded, encoded_bytes,
                   &decoded_fields, &kvm_context, sizeof(kvm_context), error,
                   sizeof(error)) != 0,
               "reject backend profile mismatch")) {
        return 1;
    }
    encoded[WVM_X86_CONTEXT_WIRE_HEADER_BYTES - 1] = 1;
    if (expect(wvm_x86_context_decode(
                   WVM_VCPU_BACKEND_TCG, encoded, encoded_bytes,
                   &decoded_fields, &tcg_context, sizeof(tcg_context), error,
                   sizeof(error)) != 0,
               "reject nonzero reserved context bytes")) {
        return 1;
    }

    puts("x86 context tests: PASS");
    return 0;
}
