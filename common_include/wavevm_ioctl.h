#ifndef WAVEVM_IOCTL_H
#define WAVEVM_IOCTL_H

#include <linux/ioctl.h>
#include "../common_include/wavevm_protocol.h"

struct wvm_ioctl_gateway {
    uint32_t gw_id;
    uint32_t ip;   // Network byte order
    uint16_t port; // Network byte order
};

// Control Plane Injection
#define IOCTL_SET_GATEWAY _IOW('G', 1, struct wvm_ioctl_gateway)

#define IOCTL_WVM_REMOTE_RUN _IOWR('G', 2, struct wvm_ioctl_remote_run)

// 路由表更新结构体
struct wvm_ioctl_route_update {
    uint32_t start_index;
    uint32_t count;
    // 柔性数组，用户态需分配足够的空间
    // 对于 CPU 表是 uint32_t，对于 MEM 表是 uint16_t，这里统一用 u32 传输方便对齐
    uint32_t entries[0]; 
};

#define IOCTL_UPDATE_CPU_ROUTE _IOW('G', 3, struct wvm_ioctl_route_update)
#define IOCTL_UPDATE_MEM_ROUTE _IOW('G', 4, struct wvm_ioctl_route_update)
#define IOCTL_WAIT_IRQ _IOR('G', 5, uint32_t) 
// 返回值是触发中断的 IRQ 号 (简化起见，返回 1 表示有中断)

#include <linux/types.h>

struct wvm_mem_range {
    uint64_t start;
    uint64_t size;
};

// 动态内存布局结构体：支持最多 32 个不连续的 RAM 槽位
struct wvm_ioctl_mem_layout {
    uint32_t count;
    struct wvm_mem_range slots[32];
};

#define IOCTL_SET_MEM_LAYOUT   _IOW('G', 10, struct wvm_ioctl_mem_layout)
#define IOCTL_RPC_SYNC_ACK     _IOW('G', 11, uint8_t)
#define IOCTL_UPDATE_EPOCH _IOW('G', 20, uint32_t)
/* 1 GiB guest-memory chunk -> raw directory vnode placement table. */
#define IOCTL_UPDATE_MEMORY_PLACEMENT _IOW('G', 21, struct wvm_ioctl_route_update)
/* Keep the kernel Logic Core's composite-ID namespace aligned with the master. */
#define IOCTL_SET_VM_ID _IOW('G', 22, uint8_t)

#endif // WAVEVM_IOCTL_H
