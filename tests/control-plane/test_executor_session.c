#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../common_include/wavevm_executor_session.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

int main(void)
{
    const uint8_t payload[] = {0x01, 0x02, 0xa5, 0xff};
    uint8_t frame[WVM_EXECUTOR_SESSION_MAX_FRAME_BYTES];
    const uint8_t *decoded_payload = NULL;
    size_t frame_bytes = 0;
    size_t decoded_bytes = 0;
    uint16_t decoded_type = 0;
    char error[128] = {0};
    int failures = 0;

    failures += expect(
        wvm_executor_session_encode(
            WVM_EXECUTOR_SESSION_VCPU_RUN, payload, sizeof(payload), frame,
            sizeof(frame), &frame_bytes, error, sizeof(error)) == 0,
        "encode valid frame");
    failures += expect(
        wvm_executor_session_decode(
            frame, frame_bytes, &decoded_type, &decoded_payload,
            &decoded_bytes, error, sizeof(error)) == 0,
        "decode valid frame");
    failures += expect(decoded_type == WVM_EXECUTOR_SESSION_VCPU_RUN,
                       "decoded message type");
    failures += expect(decoded_bytes == sizeof(payload) &&
                           memcmp(decoded_payload, payload, sizeof(payload)) == 0,
                       "decoded payload");

    frame[0] ^= 1U;
    failures += expect(
        wvm_executor_session_decode(
            frame, frame_bytes, &decoded_type, &decoded_payload,
            &decoded_bytes, error, sizeof(error)) != 0,
        "reject invalid magic");
    frame[0] ^= 1U;
    frame[8] = 0;
    frame[9] = 0;
    frame[10] = 0;
    frame[11] = 3;
    failures += expect(
        wvm_executor_session_decode(
            frame, frame_bytes, &decoded_type, &decoded_payload,
            &decoded_bytes, error, sizeof(error)) != 0,
        "reject mismatched payload length");

    puts(failures == 0 ? "executor session: PASS" : "executor session: FAIL");
    return failures == 0 ? 0 : 1;
}
