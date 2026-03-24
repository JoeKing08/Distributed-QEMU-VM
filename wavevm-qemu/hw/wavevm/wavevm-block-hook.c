#include "qemu/osdep.h"
#include "qemu/iov.h"

/*
 * virtio-blk hook entry point. Returns 0 when the request is handled by the
 * WaveVM IPC path. Returns -1 to let virtio-blk use its normal local path.
 */
int wavevm_blk_interceptor(uint64_t sector, QEMUIOVector *qiov, int is_write);

extern int wvm_send_ipc_block_io(uint64_t lba, void *buf, uint32_t len, int is_write)
    __attribute__((weak));

static int wvm_block_io_enabled = -1;

int wavevm_blk_interceptor(uint64_t sector, QEMUIOVector *qiov, int is_write)
{
    /* Gate on WVM_BLOCK_IO=1 env var; when unset, fall through to QEMU local block layer */
    if (wvm_block_io_enabled < 0) {
        const char *env = getenv("WVM_BLOCK_IO");
        wvm_block_io_enabled = (env && strcmp(env, "1") == 0) ? 1 : 0;
    }
    if (!wvm_block_io_enabled) {
        return -1;
    }

    size_t total_len = qiov->size;
    uint8_t *linear_buf;
    int ret;

    if (total_len == 0 || total_len > UINT32_MAX) {
        return -1;
    }

    linear_buf = g_malloc(total_len);
    if (!linear_buf) {
        return -1;
    }

    if (is_write) {
        qemu_iovec_to_buf(qiov, 0, linear_buf, total_len);
    }

    if (!wvm_send_ipc_block_io) {
        g_free(linear_buf);
        return -1;
    }

    ret = wvm_send_ipc_block_io(sector, linear_buf, (uint32_t)total_len, is_write);
    if (!is_write && ret == 0) {
        qemu_iovec_from_buf(qiov, 0, linear_buf, total_len);
    }

    g_free(linear_buf);
    return ret;
}

