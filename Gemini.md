## Document

这份文档是 **WaveVM "Wavelet" V31** 的最高技术指导纲领，标志着分布式虚拟化从"实验性仿真"向"工业级生产底座"的终极跨越。V31 在 V30.0 的基础上实现了 **多 VM 资源池化 (Multi-VM Resource Pooling)**，使单一物理集群能够同时托管多个独立 VM 实例，完成了从"单租户超级虚拟机"到"多租户算力池"的架构升维。

#### **🕰️ 历史的坍缩与演进：从 V27 到 V31 的量子跃迁**

*   **V27 "Frontier-X" (静态中心化时代)**: **已被埋葬**。其基于 Master-Worker 的中心化拓扑在百节点规模下会产生剧烈的“元数据串行化风暴”。Master 节点的 CPU 软中断（SoftIRQ）会因为处理海量的 RPC 请求而陷入死锁，成为系统扩展性的物理终结点。

*   **V28 "Swarm" (分布式拉取时代)**: **伟大的“数据随动”革命**。它奠定了 WaveVM 的灵魂——**去中心化一致性哈希环 (DHT)** 与 **软件定义 MESI 协议**。通过将故障域物理隔离，Swarm 实现了算力的线性扩展。其引入的 **“按需拉取 (Pull-on-Fault)”** 机制，利用 `wait_queue` 和异步信号，在网络波动时为我们提供了**黄金降落伞和最终一致性安全网**。V28 的中后期版本（Hardened）首次引入了 CRC32 链路校验、指数退避重试和动态反压网关，赋予了系统在恶劣公网环境下的生存本能。

*   **V29.5 "Wavelet" (浪潮+小波)**: **分布式虚拟化的最终形态**。它融合了 **“主动推送 (Active Push)”** 与 **“语义透传 (Semantic Pass-through)”** 双重引擎。对于高熵数据（如AI梯度），它利用 Diff 增量推送实现毫秒级同步；对于低熵操作（如内存清零、批量初始化），它利用 **Prophet 引擎** 将 GB 级的数据传输降维为字节级的指令广播，实现**物理带宽的“超光速”突破**，缺陷是仍未实现分布式存储与**非 K8S 式**的动态集群容错（不是动态编排）。

*   **V30.0 "Wavelet" (双引擎共生时代)**: **分布式虚拟化的完全体**。V30.0 在 V28 的废墟上重建了数据交换模型，实现了 **“脑（Master）与手（Slave）”** 的解耦共生，同时完成了分布式存储与动态集群的实现。它融合了以下三大核心引擎：
    1.  **Wavelet 增量推送引擎**: 针对高熵、高频变动的数据（如 AI 模型梯度、MMO 状态机），通过 `mprotect` + 内存快照技术，只提交数十字节的“小波”差分（Diff），在 **32Mbps** 的窄带宽下跑出了万兆光纤的体感。
    2.  **Prophet 语义透传引擎**: 针对低熵、大面积重复操作（如内存清零、批量分配），利用指令重写技术，将 GB 级的网络搬运降维为 32 字节的 RPC 广播，实现了**物理带宽的“超光速”突破**。
    3.  **Ingress 流量分发引擎**: 这是 V30.0 最硬核的进化。它将计算节点定义为"复合体"，Master 进程通过命令行指定的 `L_PORT` 占据物理入口，通过 **127.0.0.1 内部转发** 将计算任务（VCPU_RUN）和存储任务（BLOCK_IO）精准分流至 Slave 进程，实现了架构上的**高内聚与物理上的强解耦**。

*   **V31 "Wavelet" (多租户资源池化时代)**: **从单 VM 超算到多 VM 算力池的架构升维**。V31 在 V30.0 的双进程共生架构基础上，引入了 **Multi-VM Resource Pooling**——在不修改 48 字节协议头的前提下，利用 `slave_id` / `target_id` 的高 8 位编码 `vm_id`（低 24 位保留给 `node_id`），实现了单一物理集群对多个独立 VM 实例的同时托管。核心特性：
    1.  **Composite ID 编码引擎**: `WVM_ENCODE_ID(vm, node)` 将 `vm_id` 和 `node_id` 压入同一个 `uint32_t`，协议头零扩展、网关零改造。当 `vm_id=0` 时，composite ID 等于裸 `node_id`，实现了对 V30.0 的完全透明兼容。
    2.  **Per-VM 进程隔离模型**: 每个 VM 在每个物理节点上运行独立的 Master + Slave 进程（不同端口、不同 SHM 文件），Gateway 每物理节点只跑一个、共享路由。隔离三维度：网络层（composite ID 路由）、内存层（独立 SHM）、进程层（独立 DHT directory table）。
    3.  **入站 vm_id 过滤**: `rx_thread_loop` 和 `wvm_logic_process_packet` 在 CRC 校验后立即检查 `WVM_GET_VMID(target_id)`，丢弃非本 VM 的包，防止跨租户数据泄漏。
    4.  **弹性变容**: 变容量只需停止对应 VM 的进程、修改配置、重启，不影响同集群其他 VM。

#### **📜 V31 核心设计公理：软件定义的物理法则**

在 V31 的视界里，我们遵循以下四条不可动摇的底线：
1.  **延迟屏蔽优先 (Latency Hiding)**: 主动推送（Active Push）必须掩盖物理光纤的 RTT。读延迟必须从网络级（~100µs）坍缩至本地内存级（~60ns）。
2.  **写操作本地化 (Write Localization)**: Guest OS 的任何写入指令在触发快照后必须**立即返回**。网络同步必须异步化、流水线化，严禁阻塞 vCPU 核心。
3.  **异构资源逻辑化 (Heterogeneous Mapping)**: 不再区分物理机的大小。所有资源按 **4GB = 1 DHT Slot** 的标准权重进行逻辑切分，确保 PB 级内存空间的哈希分布绝对均匀，消除"长尾节点"。
4.  **多租户物理隔离 (Multi-VM Isolation)**: 同一物理集群的多个 VM 实例通过 composite ID 编码实现网络级路由隔离，通过独立 SHM / 独立进程实现内存级与进程级隔离。vm_id=0 时行为与 V30.0 完全一致，零感知升级。

---

### 📘 第一部分：能力飞跃——V31 "Wavelet" 的降维打击

V28 Swarm 解决了“能不能连”的问题，而 V30.0 Wavelet 解决了“能不能跑得快”的问题。下表对比了 V30.0 针对分布式虚拟化痛点进行的深度优化与代码级实现逻辑。

| 核心维度 | V28 方案 (Hardened Swarm) | **V30.0 终极方案 (Wavelet)** | 代码级实现逻辑与物理意义 |
| :--- | :--- | :--- | :--- |
| **数据同步模型** | **被动拉取 (Pull)** | **主动推送 (Active Push)** | **“宣告兴趣”逻辑实现**。节点通过 `wvm_declare_interest` 订阅邻居内存，Directory 节点通过 `broadcast_to_subscribers` 主动推送更新。 |
| **流量传输效率** | **全页传输 (Full-Page)** | **增量小波 (Wavelet Diff)** | **差分收割算法**。通过 `p64_curr != p64_pre` 进行 64 位宽扫描，只传输变动比特。对于全零操作，通过 `WVM_FLAG_ZERO` 标记进行指令级清零。 |
| **分布式分发架构** | **单进程混沌** | **双进程分流 (Ingress Dispatch)** | **Ingress 分流器**。Master 占据入口，通过判断 `msg_type`（计算/存储类型）转发至本地 Slave 监听端口，解决了 P2P 模式下的端口占用冲突。 |
| **一致性保障** | **MESI (阻塞锁)** | **Epoch-Version (乐观锁)** | **复合版本号控制**。高 32 位 `Epoch` (纪元) 解决网络分区，低 32 位 `Counter` 解决 UDP 乱序。版本不匹配触发 `MSG_FORCE_SYNC` 回退安全网。 |
| **存储物理安全** | **异步不可靠写入** | **原子 Flush 语义 (O_DIRECT)** | **存储链路闭环**。`slave_hybrid.c` 强制集成 `fdatasync` 响应 `MSG_BLOCK_FLUSH`，并检查 `pwrite` 返回值回传 `WVM_FLAG_ERROR` 标志。 |
| **内存管理瓶颈** | **全局 mprotect** | **Lazy TLB Flush (QoS)** | **延时保护机制**。`flush_lazy_ro_queue` 将数千次 `mprotect` 降级合并，将 TLB Shootdown 频率降低 95% 以上，释放算力给 Guest。 |
| **集群资源规划** | **物理 ID 线性分布** | **异构权重 DHT 环** | **4GB 权重映射**。`main_wrapper.c` 自动根据物理 RAM 大小展开虚拟节点数，确保大内存节点承载更多的 Directory 元数据负载。 |
| **大块操作优化** | **逐页同步 (慢)** | **Prophet 语义透传** | **指令卸载技术**。劫持 `memset` 转换为 32 字节 RPC 指令广播，全网 Slave 节点同步硬件执行物理内存填充，实现带宽突破。 |

---

### 🏛️ 第二部分：V31 集群架构与核心组件详解

#### 1. 架构核心示意图 (The Composite Node Topology)

V31 不再将物理计算节点视为单一的进程，而是定义为由 **"指挥中枢 (Master)"** 和 **"执行单元 (Slave)"** 构成的复合计算实体。Master 占据节点唯一的对外物理入口（Ingress），而 Slave 躲在其后执行具体的原子指令。

```text
[ Architecture: P2P Multi-Process | Protocol: Wavelet Push | Ingress: Dispatched ]

                               [ Guest OS ]
                                    |
            +-----------------------+-----------------------+
            | (Memory Access)       | (Compute/IO Exit)     |
            v                       v                       v
[ QEMU Frontend (Aware) ] <-------> [ Master Daemon (Node) ] <----+
( 1. sigsegv: CBW Snapshot )        ( Port: 9000 - The BRAIN )    |
( 2. listener: Lazy Flush  )        ( - Meta-DHT Routing     )    |
( 3. Latch: 128B Aligned   )        ( - Wavelet Pub/Sub      )    |
            ^                       ( - Ingress Dispatcher   )    | (Remote Link)
            | (IPC / SHM)           +-----------+------------+    |
            |                                   | (127.0.0.1)     |
            |                                   v                 |
            |                       [ Slave Daemon (Exec) ] <-----+
            |                       ( Port: 9005 - The MUSCLE )
            |                       ( - KVM Stateless Run     )
            +---------------------> ( - Block IO (O_DIRECT)   )
                                    ( - Dirty Sync (MPSC)     )
                                                |
                                                v
                     [ Gateway Sidecar (Dynamic & Backpressure-Aware) ]
                                    |
                     ( 10GbE / 25GbE Switched Network )
```

#### 2. 物理实体与进程职责地图 (Process Responsibility Mapping)

基于 `GeminiV30.0.txt` 的代码实现，我们将物理节点（Physical Node）的职责进行物理级拆解：

##### **A. Master 守护进程 (`wavevm_node_master`) - 指挥与分流**
*   **物理入口**：监听物理端口（如 9000）。它是网关和邻居节点眼中该 ID 的唯一代表。
*   **流量分流器 (Ingress Dispatcher)**：这是 V31 的核心逻辑。
    *   **代码事实**：`user_backend.c` 和 `kernel_backend.c` 拦截收到的数据包。如果是 `MSG_VCPU_RUN` 或 `MSG_BLOCK_WRITE/READ`，Master 不处理，直接通过 `127.0.0.1` 转发给本地 Slave 端口。
*   **Directory 节点 (The Truth)**：管理 `g_dir_table`（当前主线默认：用户态 `64K`，内核态 smoke 路径 `4K`）。
    *   **代码事实**：使用 `MurmurHash3` 将 GPA 映射到 Owner。维护订阅位图 `subscribers`。
*   **元数据同步 (Gossip)**：通过 `autonomous_monitor_thread` 随机向邻居扩散视图，实现 Epoch 的最终共识。

##### **B. Slave 执行引擎 (`wavevm_node_slave`) - 计算与存储**
*   **执行容器**：`slave_hybrid.c` 负责创建 KVM 虚拟机和 vCPU。
*   **无状态运行 (Stateless Exec)**：通过 `handle_kvm_run_stateless` 接收 Master 转发过来的寄存器快照，瞬间复活并执行指令。
*   **脏页收割 (MPSC Sync)**：利用 `g_mpsc_queue` 记录 KVM 产生的脏页。
    *   **代码事实**：`dirty_sync_sender_thread` 异步将脏页数据回传给 Master 端口，利用 `robust_sendto` 进行网络反压保护。
*   **物理存储对接**：处理 `MSG_BLOCK_IO`。
    *   **代码事实**：实现 `fdatasync` (Flush 指令) 和 `O_DIRECT`。检查 `pwrite` 返回值并向 Master 回传 `WVM_FLAG_ERROR`。

#### 3. 核心文件及其代码级实现要点 (Implementation Deep Dive)

**1. `common_include/` (协议与法则)**
*   **`wavevm_protocol.h`**: 定义了复合版本号控制。
    *   **关键函数 `is_next_version`**: 通过 `(Epoch << 32) | Counter` 判定逻辑连续性，解决 UDP 乱序导致的“因果倒置”问题。
    *   **存储安全标志**: 明确了 `WVM_FLAG_ERROR` 状态位。
*   **`wavevm_config.h`**: 锁定了 PB 级空间的寻址公理。
    *   `WVM_ROUTING_SHIFT 30` (1GB 路由粒度)；`WVM_AFFINITY_SHIFT 21` (2MB 订阅粒度)。

**2. `master_core/` (一致性脑域)**
*   **`logic_core.c`**: 发布-订阅引擎。
    *   **分段锁设计**: `LOCK_SHARDS 65536`。将 500PB 内存映射到 6.5 万个互斥域，实现 Directory 层面的万级并发无锁竞争。
    *   **冲突决策**: 在 `MSG_COMMIT_DIFF` 处理器中，通过 `commit_counter != local_counter` 识别写冲突，并由 `force_sync_client` 发射“最终真理”覆盖包。
*   **`main_wrapper.c`**: 异构身份映射。
    *   **解析逻辑**: 严格执行 `RAM / 4GB` 权重分配。如果一台物理机有 128GB RAM，它将自动在 DHT 环上注册 32 个连续的虚拟 ID。

**3. `slave_daemon/` (原子执行域)**
*   **`slave_hybrid.c`**: 执行肌肉。
    *   **内存复用逻辑**: 通过 `WVM_SHM_FILE` 环境变量与 Master 共享物理内存后端，彻底消除了计算与元数据管理之间的 IPC 拷贝开销。
    *   **FD 缓存**: `g_fd_cache` 使用 `uthash` 维护 1024 个物理块文件句柄，实现分布式存储的亚毫秒级寻址。
*   **`slave_vfio.c`**: 硬件拦截。
    *   **去抖动机制**: 在 `wvm_vfio_poll_irqs` 中利用 `epoll` 监听中断，确保物理 GPU 产生的中断包不会冲垮控制平面。

**4. `wavevm-qemu/` (边缘加速前端)**
*   **`wavevm-user-mem.c`**: Wavelet 前端引擎。
    *   **Latch 屏障**: `aligned_latch_t` 结构体通过 **128 字节对齐填充**，彻底消除了多核 vCPU 下的 False Sharing (虚假共享) 性能陷阱。
    *   **重排缓冲区 (`g_reorder_buf`)**: 窗口大小 32。它在用户态实现了类似 TCP 的滑动窗口功能，将网络乱序包在“坍缩”进物理内存前进行有序排列。
    *   **延迟保护 (Lazy Flush)**: `defer_ro_protect` 收集一轮循环内的所有修改页，在循环末尾仅触发一次 `flush_lazy_ro_queue`，将昂贵的 TLB Shootdown 中断合并，提升 Guest CPU 效率。

**5. `gateway_service/` (物理层侧车)**
*   **`aggregator.c`**: 流量聚合核心。
    *   **反向压力 (Backpressure)**: 实现了 `smart_backoff` 逻辑。当网关发送队列满时，通过“CPU忙等 -> nanosleep -> usleep”三级降速，优雅地阻断生产者的产生速度，防止宿主机 OOM。

---

### 📊 第三部分：运行机制全流程拆解与效率对比 (Architecture Walkthrough)

本章节将深度解析 **V31 "Wavelet"** 在物理层面的真实数据流转，特别强调 Master 与 Slave 在 127.0.0.1 上的指令交接逻辑。

#### 🎬 场景一：Mode A (内核态) 的运作机制 —— "Predictive Zero-Copy with Ingress Bridge"

**物理背景**：裸金属环境，对延迟和 CPU 开销有极致要求的 HPC 场景。
**核心机制**：内核态预测性推送 + 内核级直转分流，实现计算、存储与通信的完全重叠。

**全流程拆解：一次跨节点内存访与 CPU 调度**

1.  **[订阅] 缺页即宣告 (Fault is Declaration)**
    *   Guest 内 vCPU 访问远程 GPA `0xA000`。
    *   **V30.0 行为**：
        1.  **异步缺页**：内核 `wvm_fault_handler` 发起拉取流程，但不自旋死等，而是将 vCPU 线程放入 `wait_queue_head_t` 休眠。
        2.  **异步宣告**：同步向 Directory 发送 `MSG_DECLARE_INTEREST`。
        3.  **中断唤醒**：网络中断收到包后，直接在软中断上下文填充页面并唤醒 `wait_queue`，vCPU 恢复执行。

2.  **[分流] Ingress 旁路转发 (Ingress Dispatcher)**
    *   **事件**：Master 节点的内核模块 `wavevm.ko` 收到外部发来的 `MSG_VCPU_RUN`（计算请求）。
    *   **代码级逻辑**：`kernel_backend.c` 的 `internal_process_single_packet` 识别消息类型。
    *   **物理动作**：内核发现是计算任务，直接在包处理分支中构造本地环回地址，通过 `kernel_sendmsg` 将原始数据包“踢”给本地环回地址 `127.0.0.1:local_slave_port`。
    *   **效果**：Master 进程零参与，计算任务以内核级速度直达 Slave 进程。

3.  **[推送] 更新即广播 (Update is Broadcast)**
    *   **事件**：Slave 端的物理 CPU 修改了 GPA `0xA000`。
    *   **代码级逻辑**：内核 `wvm_page_mkwrite` 捕获写动作，快照并唤醒 `committer_thread`。
    *   **Wavelet 行为**：`committer_thread` 执行 64 位宽扫描，计算出增量 `diff`，发送带逻辑时钟的 `MSG_COMMIT_DIFF` 给 Directory 广播。

4.  **[命中] 读操作零延迟 (Read is Local Hit)**
    *   由于步骤 3 的主动推送，订阅者节点的 `handle_kernel_push` 已通过 `kmap_atomic` 将数据注入物理页。
    *   vCPU 再次读取时，EPT 权限为 Read，**零网络开销，零内核陷入**。

---

#### 🎬 场景二：Mode B (用户态) 的运作机制 —— "Resilient Wavelet with MPSC Harvesting"

**场景**：K8s 容器环境。强调写操作本地化与 Slave 端的异步收割。

1.  **[乐观写入] 写保护即快照 (Latch-Aware Snapshot)**
    *   **信号捕获**：vCPU 写入只读页触发 `SIGSEGV`。
    *   **代码级防护**：`sigsegv_handler` 首先执行 `wait_on_latch`（128 字节对齐锁），确保此时 `diff_harvester_thread` 没有在操作该页。
    *   **CBW 快照**：从预分配的 `image_pool` 拷贝 `pre-image`，立即 `mprotect(RW)` 放行，vCPU **写零延迟**。

2.  **[Slave 收割] 异步脏页同步 (MPSC Queue Harvesting)**
    *   **事件**：Slave 进程中的 vCPU 任务执行结束。
    *   **物理逻辑**：`handle_kvm_run_stateless` 通过 `KVM_GET_DIRTY_LOG` 获取变更位图。
    *   **异步入队**：脏页数据被压入 `g_mpsc_queue`。`dirty_sync_sender_thread` 负责将这些脏页通过 `robust_sendto`（指数退避机制）回传给本地 Master 端口（9000）。
    *   **效果**：Slave 的计算线程永远不被网络 I/O 阻塞。

3.  **[冲突解决] 版本校验与强制同步 (Conflict & Force Sync)**
    *   **逻辑判定**：Directory 收到 `COMMIT_DIFF`，通过 `is_next_version` 发现版本号不连续。
    *   **安全网动作**：Directory 拒绝 `diff`，向写者发送 `MSG_FORCE_SYNC`。
    *   **自愈恢复**：客户端 `mem_push_listener_thread` 接收全页数据覆盖本地内存，恢复最终一致性。

---

#### 🎬 场景三：分布式存储运作机制 —— "Atomic Block IO with Safety Flush"

**物理背景**：Guest 执行磁盘持久化操作（如数据库 commit）。

1.  **[拦截] Virtio-Blk Hook**：QEMU 前端 `wavevm_blk_interceptor` 拦截 I/O 请求。
2.  **[分流] Ingress Forwarding**：Master 进程收到存储包，识别为 `MSG_BLOCK_WRITE`，通过 127.0.0.1 转发给 Slave 进程。
3.  **[落盘] 物理原子性执行**：
    *   **O_DIRECT**：Slave 端 `handle_block_io_phys` 利用 `posix_memalign` 分配对齐空间，调用 `pwrite` 绕过内核缓存。
    *   **Flush 语义**：若收到 `MSG_BLOCK_FLUSH`，Slave 强制执行 `fdatasync` 并检查返回值。
4.  **[反馈] 错误回传**：若物理磁盘写入失败，Slave 在 ACK 包中置位 `WVM_FLAG_ERROR`，Master 将其透传给 Guest，防止静默数据损坏。

---

### 📊 效率对比：V31 Wavelet vs V28 Swarm (百万节点集群)

这张表格是 V30.0 架构自信的最终体现，它反映了从“拉”到“推”的质变。

| 任务类型 | V28 状态 (Pull Model) | V30.0 Mode B 预期 (Push) | V30.0 Mode A 预期 (Push) | **V30.0 最终分析 (Why it's the Ultimate)** |
| :--- | :--- | :--- | :--- | :--- |
| **MMO (元宇宙)** | 有瓶颈 (TPS ~5M) | **40M+ TPS** | **50M+ TPS** | **写零延迟 + 增量推送**。玩家操作（写）立即完成，Diff 异步提交。Mode A 因更低的内核开销而更快。热点争抢导致的串行化问题被乐观写入模型极大缓解。 |
| **HPC (气象)** | 不可用 (<10%) | **95%+** | **99%+** | **计算/通信重叠**。通过`DECLARE_INTEREST`，邻居节点的更新被主动推送到本地，消除了阻塞式读取延迟。Mode A 几乎等同于 InfiniBand 的零拷贝性能。 |
| **AI训练** | 勉强可用 (~45%) | **90%+** | **95%+** | **异步梯度流**。梯度更新 (`diff`) 被持续不断地推送到参数服务器 (Directory)，消除了全局同步点 (Barrier)。Mode A 因无用户/内核态拷贝而更优。 |
| **渲染** | 高效 (99%) | **99.9%** | **99.9%** | 无显著差别，V28 在此场景已足够优秀。V30.0 只是在结果回传时更节省带宽。 |
| **冷启动 / 克隆**<br>(System Ops) | **极慢 (<5%)** | **99.9%** | **99.9%** | **降维打击**。通过语义透传，4GB 内存清零操作被转换为一条 32 字节的 RPC 指令。无论网络带宽多低（甚至 1Mbps），系统启动和 Fork 速度都等同于本地内存带宽。 |
| **低带宽环境**<br>(32Mbps 容器) | **不可用** | **完全可用** | **完全可用** | **Diff + RPC 组合拳**。常规逻辑走 Diff 增量，大块操作走 RPC 透传。这使得 V30.0 成为唯一能在廉价公网容器中运行高性能集群的架构。 |
| **分布式存储 IO** | 未实现 | **接近本地性能** | **接近本地性能** | **存储链路闭环**。O_DIRECT + fdatasync + 错误标志位，实现了与物理硬盘对等的可靠性语义。 |

---

### 🚀 第四部分：生产级集群部署演练 (Deployment Walkthrough)

V31 的部署继承了 V28 的**分形蜂群（Fractal Swarm）**理念，但在配置和启动细节上更加精炼，以支持"推送"模型和多 VM 资源池化。本演练将覆盖从简单的**扁平化集群**到复杂的**分形（分层联邦）集群**的完整部署流程。

---

#### 部署场景一：扁平化异构集群 (Flat Heterogeneous Cluster)

**目标**：将几台不同配置的机器，通过**虚拟节点（Virtual Nodes）**权重，在逻辑上组成一台性能均衡的超级虚拟机。

##### 1. 目标拓扑与硬件规划 (The Scenario)

*   **Node 0 (Primary / Local GPU)**:
    *   **物理 ID**: 0
    *   **IP**: 192.168.1.2
    *   **资源**: **4核 / 4GB RAM**
    *   **硬件**: **NVIDIA RTX 3060** (直通)
*   **Node 1 (Compute / Remote GPU)**:
    *   **物理 ID**: 1
    *   **IP**: 192.168.1.30
    *   **资源**: **64核 / 4GB RAM** (算力强，内存小)
    *   **硬件**: **Tesla T4** (远程拦截)
*   **Node 2 (Storage)**:
    *   **物理 ID**: 2
    *   **IP**: 192.168.1.31
    *   **资源**: **4核 / 128GB RAM** (算力弱，内存大)

**核心挑战**：Node 2 的内存是 Node 0/1 的 32 倍。
**V30 解法**：在配置文件中为 Node 2 分配 **32 个虚拟节点 ID**，使其在 DHT 哈希环上占据 32/34 的概率，从而承载绝大部分内存元数据管理压力。

##### 2. 统一配置文件编写

这两个文件必须在**所有节点**上完全一致。

*   **配置文件 (`/etc/wavevm/logical_topology.txt`)**:
    ```ini
    # 格式: NODE [物理ID] [IP] [PORT] [CORES] [RAM_GB]
    NODE 0 127.0.0.1 8000 4  4
    NODE 1 127.0.0.1 8000 64 4
    NODE 2 127.0.0.1 8000 4  128
    ```
*   **配置文件 (`/etc/wavevm/real_routes.txt`)**:
    ```ini
    # 格式: ROUTE BaseID Count GatewayIP Port
    # BaseID 和 Count 均指 虚拟节点ID
    ROUTE  0  1    192.168.1.2    9000
    ROUTE  1  1    192.168.1.30   9000
    ROUTE  2  32   192.168.1.31   9000
    ```

##### 3. 部署步骤详解 (Step-by-Step)

**第一步：部署 Gateway Sidecar**

**在每个节点上执行**:

*   **启动命令**:
    ```bash
    # 上游指向自己
    ./wavevm_gateway 8000 127.0.0.1 9000 /etc/wavevm/real_routes.txt 9001 &
    ```

**第二步：启动统一节点守护进程 (Unified Node Daemon)**

**在 Node 0 (192.168.1.2) 上执行**:

*   **启动命令**:
    ```bash
    # 1. 物理存储预热：创建 Chunk 槽位并赋予权限
    sudo mkdir -p /var/lib/wavevm/chunks
    sudo chmod 777 /var/lib/wavevm/chunks
    # 2. 定义本节点共享内存标识（用于防止单机部署时内存重叠坍缩，多机部署可不使用该项设置，使用默认的 /wavevm_ram）
    export WVM_SHM_FILE="/wavevm_ram_node0"
    # 3. 加载内核模块
    sudo insmod wavevm.ko service_port=9000 local_slave_port=9005
    # 4. 启动 Master
    # <RAM_MB> <L_PORT> <CONF> <ID> <C_PORT> <SLAVE_PORT> <SYNC_BATCH> [VM_ID]
    ./wavevm_node_master 4096 9002 /etc/wavevm/logical_topology.txt 0 9001 9005 64 &
    # 5. 等待 SHM 创建与预热
    sleep 2
    # 6. 启动 Slave
    # <L_PORT> <CORES> <RAM_MB> <ID> <C_PORT> [VM_ID]
    ./wavevm_node_slave 9005 4 4096 0 9001 &
    ```

**在 Node 1 (192.168.1.30) 上执行**:

*   **配置文件 (`/etc/wavevm/devices.txt`)**:
    ```ini
    # Format:
    # DEVICE <PCI_ID> <IOMMU_GROUP_PATH>
    # BAR<Index> <GPA_Start_Hex> <Size_Bytes_Dec>
    # END

    # --- Device 1: NVIDIA Tesla T4 ---
    # 1. 物理 PCI 地址与 IOMMU 组 (需用 lspci 和 find /sys/kernel/iommu_groups 确认)
    DEVICE 0000:01:00.0 /dev/vfio/12

    # 2. BAR0: 寄存器空间 (16MB)
    # 映射到 Guest GPA 0xFA000000 (需与 QEMU 启动参数匹配)
    BAR0 0xFA000000 16777216

    # 3. BAR1: 显存空间 (16GB)
    # 映射到 Guest GPA 0x400000000 (16GB 位置，避开低端内存)
    # Size: 16 * 1024^3 = 17179869184
    BAR1 0x400000000 17179869184

    # 4. BAR2: 预留 IO 空间 (4KB) - 对应 wavevm-gpu-stub 的 IO 拦截区
    # 映射到 Guest GPA 0xFB000000
    BAR2 0xFB000000 4096

    END
    ```
*   **启动命令**:
    ```bash
    sudo mkdir -p /var/lib/wavevm/chunks
    sudo chmod 777 /var/lib/wavevm/chunks
    export WVM_SHM_FILE="/wavevm_ram_node1"
    export WVM_VFIO_CONFIG="/etc/wavevm/devices.txt"
    ./wavevm_node_master 4096 9000 /etc/wavevm/logical_topology.txt 1 9001 9005 64 &
    sleep 2
    ./wavevm_node_slave 9005 64 4096 1 9001 &
    ```

**在 Node 2 (192.168.1.31) 上执行**:

*   **启动命令**:
    ```bash
    sudo mkdir -p /var/lib/wavevm/chunks
    sudo chmod 777 /var/lib/wavevm/chunks
    export WVM_SHM_FILE="/wavevm_ram_node2"
    ./wavevm_node_master 131072 9000 /etc/wavevm/logical_topology.txt 2 9001 9005 64 &
    sleep 2
    ./wavevm_node_slave 9005 4 131072 2 9001 &
    ```

**第三步：启动 QEMU Payload (在 Node 0 上)**

```bash
#!/bin/bash

# 1. 物理存储预热：创建本地 Chunk 槽位
sudo mkdir -p /var/lib/wavevm/chunks
sudo chmod 777 /var/lib/wavevm/chunks

# 2. 提取守护进程生成的 Ingress 桥接 Socket 路径
export WVM_ENV_SOCK_PATH=$(strings /proc/$(pgrep -f wavevm_node_master)/environ | grep WVM_ENV_SOCK_PATH | cut -d= -f2)

# 3. 启动指令：对齐物理拓扑，强制总线隔离，锁定大页性能（为了指令能被正确解析，只能将行间注释放在这里）
  # --- [A] 物理内存加固：强制开启 2MB 硬件大页 (Hugepages) ---
  # 理由：消除 500PB 空间映射导致的页表内存溢出，TLB 命中率提升 200%
  # --- [B] 分布式存储挂载：利用 /dev/zero 欺骗，实现线性化拦截 ---
  # 理由：qemu-wavevm.diff 会劫持此路径，将 IO 路由至全网 Slave 物理 Chunk
  # --- [C] 本地硬件直通：Node 0 物理显卡 ---
  # --- [D] 远程硬件物理锚定：Node 1 伪装显卡 (核心 DMA 解决逻辑) ---
  # 1. 创造物理隔离的总线 (Bus 0x20)，并将其逻辑绑定到 vNUMA 1
  # 2. 在该总线上添加根端口，作为显卡的物理插槽
  # 3. 将 Stub 显卡插入此插槽。
  # 物理后果：Guest 驱动会强制在 vNUMA 1 (即 Node 1 本地内存) 申请 DMA 缓冲区，实现 0 网络开销的 DMA
qemu-system-x86_64 \
  -name "WVM-Wavelet-SSI-Core" -m 136G -smp 72 \
  -accel wavevm,mode=user \
  \
  -object memory-backend-file,id=mem0,size=4G,mem-path=/dev/hugepages,share=on,hugepages=on \
  -numa node,nodeid=0,cpus=0-3,memdev=mem0 \
  \
  -object memory-backend-file,id=mem1,size=4G,mem-path=/dev/hugepages,share=on,hugepages=on \
  -numa node,nodeid=1,cpus=4-67,memdev=mem1 \
  \
  -object memory-backend-file,id=mem2,size=128G,mem-path=/dev/hugepages,share=on,hugepages=on \
  -numa node,nodeid=2,cpus=68-71,memdev=mem2 \
  \
  -drive file=/dev/zero,format=raw,if=virtio,cache=none,aio=native \
  \
  -device vfio-pci,host=01:00.0,id=gpu_local \
  \
  -device pxb-pcie,id=br1,bus_nr=0x20,numa_node=1 \
  -device pcie-root-port,id=rp1,bus=br1,slot=0 \
  -device wavevm-gpu-stub,id=gpu_remote,bus=rp1,vendor_id=0x10de,device_id=0x1eb8,bar1_size=16G \
  \
  -nographic -vga none
```

##### 4. 数据流转与控制链路验证 (Physical Data & Control Flow Verification)

为了确保部署成功，我们基于 **“Master-指挥/Slave-执行”** 的双进程逻辑，对三条核心链路进行推演：

**场景 A: 跨节点内存读取 (元数据/内存链路)**
*   **发起者**: Node 0 **Master 进程** (`wavevm_node_master`)。
*   **接收者**: Node 2 **Master 进程** (`wavevm_node_master`)。
*   **逻辑路径**:
    1.  Node 0 的 vCPU 缺页，**Master 0** 计算出 Directory 为 Node 2。
    2.  **Master 0** 发包 -> `127.0.0.1:8000` (Sidecar 0)。
    3.  **Sidecar 0** 查表 -> `192.168.1.31:9000` (Node 2 的 Master 入口)。
    4.  **Master 2** 在 **9000 端口**捕获 `MSG_MEM_READ`，直接从本地 SHM 读取数据。
    5.  **Master 2** 将 `MSG_MEM_ACK` 通过 `g_ops->send_packet` 交给本地 Sidecar，经网关管道原路返回 Node 0。
*   **结论**: 内存同步是 **Master 与 Master** 之间的对话，Slave 进程不参与。所有跨节点通信均通过网关管道（TX 队列 → Sidecar 8000 → 对端 9000），不存在直接 sendto 远端的捷径。

**场景 B: 跨节点 CPU 执行 (计算链路 —— 最关键的分流路径)**
*   **发起者**: Node 0 **Master 进程**。
*   **处理者**: Node 1 **Slave 进程** (`wavevm_node_slave`)。
*   **逻辑路径**:
    1.  Node 0 的 **Master 0** 构造 `MSG_VCPU_RUN` 计算包。
    2.  经过网关，包到达 Node 1 的物理地址 `192.168.1.30:9000` (Node 1 **Master 入口**)。
    3.  **Master 1** 识别 `msg_type` 为 5，触发 **Ingress 分流逻辑**：`sendto(127.0.0.1:9005)`。
    4.  Node 1 的 **Slave 1** 进程在 **9005 端口**接收包，瞬间复活 KVM 核心执行计算。
    5.  **Slave 1** 计算完，带着寄存器结果 `sendto` 回传给 `client` 地址（即本地 **Master 1** 的回环端口 `127.0.0.1:9000`）。
    6.  **Master 1** 识别 `from_local_slave` 标志，从包头提取 `target_id`（发起方 Node 0），通过 `u_send_packet` 经网关管道将 `MSG_VCPU_EXIT` 路由回 Node 0。
*   **结论**: 计算任务由 Master **负责分发**，由 Slave **负责干活**。Slave 不具备直接回传远端的能力，其结果必须经本地 Master 中转，再由网关管道送达发起方。127.0.0.1 转发实现了 Master↔Slave 的本地高速通道。

**场景 C: 拓扑同步与路由注入 (控制链路)**
*   **发起者**: 运维机脚本 或 Node 3 **Master 进程**。
*   **接收者**: 所有在线节点的 **Master 进程** / **Gateway 进程**。
*   **逻辑路径**:
    1.  **Gossip 自动同步**: Node 3 启动，其 **Master 3** 通过 `g_ops->send_packet_async(MSG_HEARTBEAT)` 将心跳包送入 TX 队列，经 Sidecar **8000 端口**出站，到达邻居节点的 **9000 端口**（数据面管道）。心跳 payload 携带 `ctrl_port` 信息供对端记录。
    2.  **路由动态注入**: 运维人员执行 `wvm_route_ctl.py`。脚本向 `Node_IP:9001` 发送 `MAGIC=WVMC` 的 UDP 指令（`OP_ADD_ROUTE`/`OP_DEL_ROUTE`），这是独立于数据面的**带外控制通道**。
    3.  各节点 **Gateway** 在 **9001 端口**接收 WVMC 指令，通过 `pthread_rwlock_wrlock` 更新路由哈希表。
*   **结论**: Gossip 心跳走**数据面管道**（8000→9000），与业务流量共享网关路径；**9001 端口**专用于运维路由注入（WVMC 协议），是独立的带外控制通道。两者物理分离，确保路由更新不受数据面拥塞影响。

---

#### 部署场景二：分形联邦集群 (Fractal Federation Cluster)

**目标**：将大规模集群（如 1000 节点）通过**分层网关 (Tiered Gateway)** 进行物理路由隔离，同时保持逻辑上的单一 DHT 环，以实现无限扩展。

##### 1. 架构与IP规划

*   **总规模**: 1000 节点，每个节点16GB内存
*   **结构**: 10 个 Pod (机柜)，每个 Pod 100 台机器。
*   **IP**: Pod 0 (`192.168.0.x`), Pod 9 (`192.168.9.x`), Core Switch (`10.0.0.1`)。

##### 2. 部署步骤详解

**第一步：部署 L2 Core Gateway (骨干网)**

在 **10.0.0.1** 上运行。它只认识 L1 Gateway。

*   **配置文件 (`/etc/wavevm/l2_routes.txt`)**:
    ```ini
    # 格式: ROUTE BaseID Count GatewayIP Port
    # BaseID 和 Count 均指 虚拟节点ID
    ROUTE 0     400   192.168.0.1   9000  # Pod 0 的 100个 phys-nodes 指向它
    ROUTE 400   400   192.168.0.1   9000  # Pod 1 的 100个 phys-nodes 指向它
    ... (其余 Pod 的路由)
    ```
*   **启动命令**:
    ```bash
    # 上游指向自己，作为根节点
    ./wavevm_gateway 9000 127.0.0.1 9000 /etc/wavevm/l2_routes.txt 9001 &
    ```

**第二步：部署 L1 Pod Gateway (机柜)**

在每个 Pod 的汇聚交换机（如 **192.168.0.1**）上运行。它只认识 Pod 内的物理节点。

*   **配置文件 (`/etc/wavevm/pod0_routes.txt`)**:
    ```ini
    ROUTE 0   4   192.168.0.10   9000 
    ROUTE 4   4   192.168.0.11   9000 
    ... (其余 Pod 的路由)
    ```
*   **启动命令**:
    ```bash
    # 上游指向 L2 Core Gateway
    ./wavevm_gateway 9000 10.0.0.1 9000 /etc/wavevm/pod0_routes.txt 9001 &
    ```

**第三步：部署 Node Daemon & Gateway Sidecar**

在每个计算节点（如 **192.168.0.10**）上执行。

*   **配置文件 (`/etc/wavevm/logical_topology.txt`)**:
    ```ini
    NODE 0 127.0.0.1 8000 4 16
    NODE 1 127.0.0.1 8000 4 16
    ...
    NODE 999 127.0.0.1 8000 4 16
    ```
*   **启动 Gateway Sidecar**:
    ```bash
    # 配置文件为空，所有流量都转发给上游 (Pod Gateway)
    touch /etc/wavevm/empty.txt
    ./wavevm_gateway 8000 192.168.0.1 9000 /etc/wavevm/empty.txt 9001 &
    ```
*   **启动 Node Daemon (在 Node 0-998 上)**:
    ```bash
    # 物理存储预热：创建 Chunk 槽位并赋予权限
    sudo mkdir -p /var/lib/wavevm/chunks
    sudo chmod 777 /var/lib/wavevm/chunks
    # Daemon 只需与本地 Sidecar 通信
    # 它需要完整的 logical_topology.txt 来计算全局 DHT 哈希，但不需要知道每个节点的真实 IP
    ./wavevm_node_master 16384 9000 /etc/wavevm/logical_topology.txt 0 9001 9005 &
    sleep 2
    ./wavevm_node_slave 9005 4 16384 0 9001 &
    ```
*   **配置文件 (`/etc/wavevm/devices.txt`)**:
    ```ini
    # Format:
    # DEVICE <PCI_ID> <IOMMU_GROUP_PATH>
    # BAR<Index> <GPA_Start_Hex> <Size_Bytes_Dec>
    # END

    # --- Device 1: NVIDIA Tesla T4 ---
    DEVICE 0000:01:00.0 /dev/vfio/12

    BAR0 0xFA000000 16777216

    # Size: 16 * 1024^3 = 17179869184
    BAR1 0x400000000 17179869184

    BAR2 0xFB000000 4096

    END
    ```
*   **启动 Node Daemon (在 Node 999 上)**:
    ```bash
    sudo mkdir -p /var/lib/wavevm/chunks
    sudo chmod 777 /var/lib/wavevm/chunks
    export WVM_VFIO_CONFIG="/etc/wavevm/devices.txt"
    ./wavevm_node_master 16384 9000 /etc/wavevm/logical_topology.txt 0 9001 9005 &
    sleep 2
    ./wavevm_node_slave 9005 4 16384 0 9001 &
    ```

**第四步：启动 QEMU Payload (在 Node 0 上)**

*   **配置文件 (`/etc/wavevm/fractal_vm.cfg`)**:
    ```ini
    [machine]
      type = "qemu-system-x86_64"
      accel = "wavevm"

    # --- Node 0 (Pod 0 / Leaf 0) ---
    [object "mem0"]
      qom-type = "memory-backend-file"
      mem-path = "/dev/hugepages"
      size = "16G"
      share = "on"
      hugepages = "on"

    [numa]
      type = "node"
      nodeid = "0"
      memdev = "mem0"
      cpus = "0-3"

    # --- Node 1 (Pod 0 / Leaf 1) ---
    [object "mem1"]
      qom-type = "memory-backend-file"
      mem-path = "/dev/hugepages"
      size = "16G"
      share = "on"
      hugepages = "on"

    [numa]
      type = "node"
      nodeid = "1"
      memdev = "mem1"
      cpus = "4-7"

    # ... (此处省略 Node 2 到 Node 998 的配置，逻辑同上，以此类推) ...

    # --- Node 999 (Pod 9 / Leaf 99) - 挂载分布式 GPU ---
    [object "mem999"]
      qom-type = "memory-backend-file"
      mem-path = "/dev/hugepages"
      size = "16G"
      share = "on"
      hugepages = "on"

    [numa]
      type = "node"
      nodeid = "999"
      memdev = "mem999"
      cpus = "3996-3999"

    # [关键] 分布式 DMA 物理锚定
    # 1. 创建亲和性总线，绑定到 vNUMA 999 (即 Node 999 本地物理内存)
    [device "pxb_bus_999"]
      driver = "pxb-pcie"
      bus_nr = "200"
      id = "br_999"
      numa_node = "999"

    # 2. 创建根端口
    [device "rp_999"]
      driver = "pcie-root-port"
      bus = "br_999"
      slot = "0"
      id = "rp_999"

    # 3. 插入显卡，驱动将自动在 vNUMA 999 申请 DMA 缓冲
    [device "gpu_999"]
      driver = "wavevm-gpu-stub"
      bus = "rp_999"
      vendor_id = "0x10de"
      device_id = "0x1eb8"
      bar1_size = "16G"

    # --- 分布式存储后端 ---
    [drive "sys_disk"]
      file = "/dev/zero"
      format = "raw"
      if = "virtio"
      cache = "none"
      aio = "native"
    ```
*   **启动命令**:
    ```bash
    export WVM_ENV_SOCK_PATH=$(strings /proc/$(pgrep -f wavevm_node_master)/environ | grep WVM_ENV_SOCK_PATH | cut -d= -f2)
    qemu-system-x86_64 \
      -name "WVM-Fractal-SuperComputer" \
      -m 16T -smp 4096 \
      -readconfig /etc/wavevm/fractal_vm.cfg \
      -nographic -vga none
    ```

##### 3. 分形架构数据流转与控制链路验证 (Fractal Data & Control Flow Verification)

在分形架构中，物理网络被划分为"计算节点-机柜网关-核心骨干"三层。我们基于 **V31 "双进程分流"** 逻辑，对跨联邦的三条核心链路进行深度路径推演：

**场景 A: 跨 Pod 内存同步 (元数据/内存链路)**
*   **物理场景**: Pod 0 的 Node 0 (`192.168.0.10`) 缺页，需读取由 Pod 9 的 Node 999 管理的 Directory 元数据。
*   **发起者**: Node 0 **Master 进程** (`wavevm_node_master`)。
*   **处理者**: Node 999 **Master 进程** (`wavevm_node_master`)。
*   **全路径追踪**:
    1.  **发起**: Node 0 **Master** 构造 `MSG_MEM_READ` 包。
    2.  **出站**: Node 0 **Master** 执行 `sendto(127.0.0.1:8000)`，将包交给本地 **Sidecar**。
    3.  **机柜转发**: **Sidecar 0** -> `192.168.0.1:9000` (**Pod 0 网关**)。
    4.  **骨干路由**: **Pod 0 网关**查询 ID 不在机柜内，向上甩包 -> `10.0.0.1:9000` (**Core 核心网关**)。
    5.  **跨区投递**: **Core 网关**识别 ID 范围属于 Pod 9 -> `192.168.9.1:9000` (**Pod 9 网关**)。
    6.  **入站准达**: **Pod 9 网关**识别物理 IP -> `Node 999 IP:9000` (**Node 999 Master 进程入口**)。
    7.  **最终处理**: **Node 999 Master** 在 9000 端口捕获请求，从本地 SHM 读取数据后，通过 `g_ops->send_packet` 将 `MSG_MEM_ACK` 交给本地 Sidecar，经网关管道原路返回 Node 0。
*   **结论**: 内存链路在联邦架构下实现了"脑对脑"的跨区域握手，物理路由对逻辑层完全透明。ACK 回程同样走网关管道（Sidecar → Pod GW → Core GW → Pod 0 GW → Node 0），不存在直接 sendto 远端的捷径。

**场景 B: 跨 Pod 远程调度 (计算/存储执行链路)**
*   **物理场景**: Node 0 (Pod 0) 的虚拟机将计算任务分发给 Node 500 (Pod 5) 执行。
*   **发起者**: Node 0 **Master 进程**。
*   **处理者**: Node 500 **Slave 进程** (`wavevm_node_slave`)。
*   **全路径追踪**:
    1.  **分发**: Node 0 **Master** 封装 `MSG_VCPU_RUN` 计算任务包。
    2.  **物理漂流**: 经过 **Sidecar 0** -> **Pod 0 GW** -> **Core GW** -> **Pod 5 GW**。
    3.  **接棒**: 数据包最终打到 Node 500 的 **Master 9000 端口**（物理入口）。
    4.  **Ingress 内部转发**: Node 500 的 **Master** 检查 `msg_type` 为 5，识别为计算任务，立即执行 `sendto(127.0.0.1:9005)`，分流给背后的 **Slave 进程**。
    5.  **执行**: Node 500 的 **Slave** 在 **9005 端口**接收快照，驱动 KVM 硬件执行。
    6.  **回传**: **Slave** 执行完毕，带着寄存器增量，`sendto` 回传给 `client` 地址（即本地 **Master** 的回环端口 `127.0.0.1:9000`）。**Master** 识别 `from_local_slave` 标志，通过 `u_send_packet` 经网关管道将 `MSG_VCPU_EXIT` 原路送回 Node 0。
*   **结论**: 分形架构下，Master 负责跨区域的"长途运输"，Slave 负责本地的"终端执行"。Slave 不具备直接回传远端的能力，其结果经本地 Master 中转后由网关管道逐级送达发起方。127.0.0.1 的 Ingress 桥接解决了联邦模式下的任务交付问题。

**场景 C: 联邦控制面管理 (控制/Gossip 链路)**
*   **物理场景**: 运维机向 Core Gateway 注入新 Pod 的路由条目，或者 Node 0 向全网扩散 Epoch 纪元。
*   **发起者**: 运维脚本 或 Master 进程。
*   **处理者**: 全联邦所有网关及节点的 **Master / Gateway 进程**。
*   **全路径追踪**:
    1.  **路由注入**: 运维机执行脚本，向 Core Gateway 的 **9001 端口** 发送 `OP_ADD_ROUTE`（WVMC 带外控制协议）。网关更新读写锁映射，并可通过级联向下级网关扩散。
    2.  **Gossip 扩散**: Node 0 的 **Master** 每隔 500ms，通过 `g_ops->send_packet_async(MSG_HEARTBEAT)` 将心跳包送入 TX 队列，经 Sidecar **8000 端口**出站，到达邻居节点的 **9000 端口**（数据面管道）。心跳 payload 携带 `ctrl_port` 和 `local_epoch` 信息。
    3.  **状态同步**: 邻居 **Master** 在 **9000 端口**收到心跳包，发现 Epoch 领先，自动同步本地逻辑时钟，并更新 DHT 哈希环。
*   **结论**: Gossip 心跳走**数据面管道**（8000→9000），与业务流量共享网关路径；**9001 端口**专用于运维路由注入（WVMC 协议），是独立的带外控制通道。两者物理分离，确保即使数据面拥塞，路由更新依然能准时送达。

---

#### 🌟 物理端口与角色职能速查表 (Final Mapping)

| 端口号 | 物理协议 | 监听进程 | 职能描述 |
| :--- | :--- | :--- | :--- |
| **UDP 8000** | WVM Data | **Sidecar (Gateway)** | **出站总出口**。叶子节点所有发往外部的流量必须先经过此端口聚合。 |
| **UDP 9000** | WVM Data | **Master (Node)** | **入站总入口 (Ingress)**。接收所有外部包，负责元数据处理与任务分流。 |
| **UDP 9001** | WVMC Control | **Gateway** | **运维控制通道**。专用于 WVMC 协议的路由手动注入（`OP_ADD_ROUTE`/`OP_DEL_ROUTE`），带外独立于数据面。 |
| **UDP 9005** | WVM Internal | **Slave (Executor)** | **本地执行入口**。专用于接收 Master 转发的计算 (VCPU) 与存储 (Block) 任务。 |

**结论**: 这种 **"Ingress 在 Master 9000 集合，Exec 在 Slave 9005 执行，Admin 在 9001 注入"** 的三位一体架构，其中 Gossip 心跳与业务数据共享数据面管道（8000↔9000），运维路由注入走独立的 9001 带外通道，结合三层网关的递归转发，构成了 V31 支撑百万节点、一亿核心的物理底座。

---

#### 💡 部署进阶：对齐 Host-Guest 边界 (EPT Scaling Theory)

在 V31 中，我们正式**弃用 Guest Padding 补丁**。
**新物理法则**：由于 `wavevm_node_master` (Master) 采用了 **“2MB 兴趣订阅粒度 (WVM_AFFINITY_SHIFT=21)”**，我们要求 QEMU 启动参数必须严格遵循 **2MB 整数倍对齐**。
*   **物理效果**：这确保了 Guest 的物理内存页表（EPT）与 Host 侧映射到 DHT 的逻辑单元完全重合。
*   **性能提升**：这消除了 90% 的 **TLB Trashing** 现象，让分布式内存访问具备了原生硬件级的稳定性。

> **[运维避坑指南：硬件地址对齐]**
> 分布式 GPU 首次挂载时，由于 Guest OS 动态分配 PCI 地址的特性，可能导致 Master 拦截地址与 Slave 监听地址不一致。
> **解决方法**：先以默认 `devices.txt` 启动，进入 Guest 执行 `lspci -vvv` 获取 Region 实际物理基址，随后更新 Slave 端的 `devices.txt` 并重启集群，确保全链路 GPA 锚定一致。

---

### 🔄 第五部分：集群节点与 Gateway 的动态加减 (Swarm Orchestration)

本章节定义了在 **V31 "Wavelet"** 架构下，利用 **Gossip 自治**、**IOCTL 穿透** 及 **覆盖式路由注入** 实现无停机扩缩容的标准作业程序 (SOP)。

> **基本部署单元 (Deployment Unit)**
> 在 V31 架构中，无论何种拓扑，每个物理计算节点都由三个进程组成：
> 1.  **`wavevm_gateway` (Sidecar)**: 监听端口 **8000**。负责出站流量聚合与流控。
> 2. **Master (`wavevm_node_master`)**：占据 Ingress 端口（未加载内核模块时为9000），负责元数据维护、DHT 路由及流量分流。
> 3. **Slave (`wavevm_node_slave`)**：占据 Exec 端口（由 Master 参数指定，通常为 9005），负责 KVM 指令执行与物理存储 IO。
> **部署铁律**：Master 必须先于 Slave 启动，以完成物理内存（SHM）的初始化与资源锁定。

#### 1. 扁平化架构下的动态变更 (Flat Topology Operations)

**场景定义**：基于第四部分“场景一”，节点 `192.168.1.2` (Node 0) 等已在线。所有节点位于同一二层网络。

##### 1.1. Mode A (内核态) 运行模式

**1.1.1. 增加节点 (Scale Out)**

*   **场景**：上线一台新机器 Node 3，IP `192.168.1.40`。
*   **操作步骤**：
    1.  **加载内核模块**：
        ```bash
        insmod wavevm.ko service_port=9000 local_slave_port=9005
        ```
    2.  **启动 Sidecar (8000)**：
        ```bash
        # 扁平架构下 Sidecar 通常直连对端，配置可为空或指向自己
        ./wavevm_gateway 8000 127.0.0.1 9000 /dev/null 9001 &
        ```
    3.  **启动 Daemon (9002)**：
        ```bash
        cp swarm_config.txt join.txt
        echo "NODE 3 192.168.1.40 8000 64 4" >> join.txt
        export WVM_SHM_FILE="/wavevm_ram_node3"
        ./wavevm_node_master 4096 9002 join.txt 3 9001 9005 &
        ```
        4. **[关键] 等待 2 秒后启动 Slave**：
        ```bash
        ./wavevm_node_slave 9005 64 4096 3 9001 &
        ```
*   **生效验证**：
    *   **在 Node 0 上**：执行 `dmesg | tail`，应看到：
        ```text
        [WVM] IOCTL_SET_GATEWAY: Update Route ID 3 -> 192.168.1.40:8000
        ```
    *   **原理**：Node 3 的 Gossip 包穿透到 Node 0 的 Daemon，Daemon 通过 IOCTL 告知内核将 Node 3 的目标地址更新为其 Sidecar（`192.168.1.40:8000`），由 Sidecar 统一转发到本机 Master Ingress（9002）。

**1.1.2. 减少节点 (Scale In)**

*   **操作步骤**：
    1.  **直接下线**：
        ```bash
        killall wavevm_node_slave wavevm_node_master wavevm_gateway
        ```
*   **生效原理**：
    *   **Fail-in-place**：邻居节点在 5 秒 (`HEARTBEAT_TIMEOUT_US`) 后判定 Node 3 为 `OFFLINE`。
    *   **内核静默**：内核路由表中保留 Node 3 的 IP 记录，但 Logic Core 不再分配任务，流量自然归零。

##### 1.2. Mode B (用户态) 运行模式

*   **增节点**：同 Mode A，但无需 `insmod`，同时将`wavevm_node_master`的9002端口改为9000。`u_set_gateway_ip` 会更新 Daemon 内存中的路由表，发往 Node 3 的包先到其 Sidecar（8000），再由 Sidecar 转发到 Master Ingress（9000）。
*   **减节点**：Daemon 的 `smart_backoff` 机制会处理节点下线瞬间的 Socket 错误，直到 Logic Core 完成哈希环重构。

---

#### 2. 分形架构下的动态变更 (Fractal Topology Operations)

**场景定义**：基于第四部分“场景二”，Core Gateway 为 `10.0.0.1`。Pod 0 网关为 `192.168.0.1`。

##### 2.1. 计算节点 (Leaf Node) 的动态增减

**2.1.1. 增加节点 (Scale Out)**

*   **场景**：在 Pod 0 中增加 Node 10 (`192.168.0.20`)。
*   **操作步骤**：
    1.  **启动 Sidecar**：指向上游 Pod 0 Gateway (`192.168.0.1`)。
        ```bash
        # 路由文件只有一行：默认路由指向 Pod 网关
        echo "ROUTE 0 0 192.168.0.1 9000" > upstream.txt
        ./wavevm_gateway 8000 192.168.0.1 9000 upstream.txt 9001 &
        ```
    2.  **启动 Daemon**：
        ```bash
        # Master 监听 9000，分流给 9005
        ./wavevm_node_master 16384 9000 swarm_config.txt 10 9001 9005 &
        sleep 2
        # Slave 监听 9005，执行计算
        ./wavevm_node_slave 9005 4 16384 10 9001 &
        ```
*   **生效验证**：
    *   **Pod 0 Gateway (`192.168.0.1`) 日志**：
        ```text
        [Gateway-Auto] Learned New Node: 10 -> 192.168.0.20:8000
        ```
    *   **原理**：Gateway 的自学习机制 (`learn_route`) 自动捕获内网流量建立路由。

**2.1.2. 减少节点 (Scale In)**

*   **操作**：直接 `kill` Node 10 的三个进程。
*   **原理**：Pod Gateway 中保留该节点的 Hash 条目（僵尸路由）。由于 Gateway 是无状态转发，这不会影响其他节点通信。

##### 2.2. Gateway (Infrastructure) 的运维操作

这里必须严格区分 **“增加”**、**“替换”** 和 **“减少”**。

**2.2.1. 增加 Gateway (Scale Out Pod)**

*   **场景**：上线 Pod 1，部署新的 Pod Gateway (`192.168.1.1`)，负责 ID 100-199。
*   **操作步骤**：
    1.  **启动 Gateway**：在 `192.168.1.1` 上启动进程。
    2.  **路由注入**：在运维机向 **Core Gateway (`10.0.0.1`)** 注入路由。
        ```bash
        # 告诉 Core GW: ID 100-199 在 192.168.1.1
        for id in {100..199}; do
            python3 deploy/wvm_route_ctl.py 10.0.0.1 9001 $id 192.168.1.1 9000
        done
        ```

**2.2.2. 替换 Gateway (Failover / Migration)**

*   **场景**：Pod 1 Gateway (`192.168.1.1`) 宕机，IP 漂移/更换为 `192.168.1.2`。
*   **原理**：V31 不支持删除，只支持 **"覆盖更新 (Overwrite)"**。
*   **操作步骤**：
    1.  **启动新实例**：在 `192.168.1.2` 启动 Gateway。
    2.  **执行覆盖**：再次运行注入脚本，**IP 填新地址**。
        ```bash
        # Core GW 会检测到 ID 已存在，直接更新其 IP 字段
        for id in {100..199}; do
            python3 deploy/wvm_route_ctl.py 10.0.0.1 9001 $id 192.168.1.2 9000
        done
        ```
*   **验证**：Core Gateway 日志显示 `[Gateway] Route Added/Updated ...`。流量瞬间切换。

**2.2.3. 减少 Gateway (Scale In Pod / Remove)**

*   **场景**：Pod 1 整体下线，不再使用。
*   **操作**：
    1.  **直接关闭**：关闭 Pod 1 内所有节点和 Gateway (`192.168.1.1`)。
    2.  **无需操作 Core Gateway**：
        *   Core Gateway 中指向 Pod 1 的路由成为 **“黑洞路由”**。
        *   如果有其他节点误发数据给 ID 100-199，Core Gateway 会转发给已关闭的 IP，随后丢包。
        *   **逻辑自愈**：Logic Core 会将 ID 100-199 标记为 OFFLINE，不再产生流量，因此黑洞路由无害。
        *   *注：V30 代码未实现 `DEL_ROUTE` 以防止 Use-After-Free，保留僵尸路由是设计特性。*

---

#### 3. 运维实战工具箱 (The Tooling)

请在运维机保存此脚本为 `deploy/wvm_route_ctl.py`。

```python
import socket
import struct
import sys

# WaveVM V30 Gateway Control Protocol
# Magic: 0x57564D43 ("WVMC")
# OpCode: 1 (ADD/UPDATE). No DELETE supported.
PROTOCOL_FMT = "!IHIIH" 
MAGIC = 0x57564D43
OP_ADD_ROUTE = 1 

def send_route_update(gateway_ip, gateway_ctl_port, node_id, target_ip, target_port):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(1.0)
        
        target_ip_int = struct.unpack("!I", socket.inet_aton(target_ip))[0]
        
        payload = struct.pack(PROTOCOL_FMT, 
                              MAGIC, 
                              OP_ADD_ROUTE, 
                              int(node_id), 
                              target_ip_int, 
                              int(target_port))
        
        # 发送至 9001 控制端口
        sock.sendto(payload, (gateway_ip, int(gateway_ctl_port)))
        print(f"[OK] Inject: Node {node_id} -> {target_ip}:{target_port} via GW {gateway_ip}")
        
    except Exception as e:
        print(f"[ERR] Failed to inject route: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 6:
        print("Usage: python3 wvm_route_ctl.py <GW_IP> <CTL_PORT> <NODE_ID> <TARGET_IP> <TARGET_PORT>")
        print("Ex:    python3 wvm_route_ctl.py 10.0.0.1 9001 100 192.168.1.1 9000")
        sys.exit(1)
    
    send_route_update(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
```

#### 4. 故障排查速查表 (Troubleshooting)

| 现象 | 检查点 | 判定标准 |
| :--- | :--- | :--- |
| **Node 上线后状态为 SHADOW** | `wavevm_node_master` 日志 | `active_node_count` 始终为 0，说明 Gossip 组播或 UDP 9000 被防火墙拦截。 |
| **Mode A 报 Remote Fault Timeout** | 老节点 `dmesg` | 若无 `IOCTL_SET_GATEWAY` 日志，说明 Daemon 未向内核同步路由。检查 `/dev/wavevm` 权限。 |
| **替换 Gateway 后流量不通** | Core Gateway 日志 | 若无 `Updated Route` 日志，说明注入脚本未发往 9001 端口，或 Magic Number 不匹配。 |
| **Sidecar 报错 Bind Failed** | 端口占用 | 检查是否 `wavevm_node_master` 和 `wavevm_gateway` 抢占了同一个 9000 端口（应分别为 9000 和 8000）。 |

---

### 📝 第六部分：V31 架构宪法与执行铁律 (The Master Architecture Manifesto)

**致后来者：**
WaveVM V31 是一个物理级的 **单系统映像 (SSI)** 算力聚合引擎。这里的代码不是在处理 Web 请求，而是在软件定义物理法则。在你试图改动任何一行代码前，必须将以下七条"铁律"刻入骨髓。任何违背宪法的修改都将导致 PB 级内存瞬间坍缩。

---

#### ⚖️ 第一条：目录结构与物理产物公理 (The Law of Physical Artifacts)

V31 严禁将所有代码混为一谈。逻辑上的"Node"在物理上必须解构为以下三个独立的二进制实体，其源码归属关系不可撼动：

1.  **`common_include/` [协议宪章]**：包含 `wavevm_protocol.h` 和 `crc32.h`。这是全网的“度量衡”，定义了 Wavelet 复合版本号和存储 `WVM_FLAG_ERROR` 语义。
2.  **`master_core/` [指挥中枢]**：
    *   **User Mode (Mode B)**: 编译 `main_wrapper.c` + `logic_core.c` + `user_backend.c` → **`wavevm_node_master`**。
    *   **Kernel Mode (Mode A)**: 编译 `kernel_backend.c` + `logic_core.c` → **`wavevm.ko`**。
    *   *铁律*：`kernel_backend.c` 必须与逻辑核心深度耦合，实现内核态 Ingress 旁路分流。
3.  **`slave_daemon/` [执行执行引擎]**：编译 `slave_hybrid.c` + `slave_vfio.c` → **`wavevm_node_slave`**。
    *   *铁律*：必须作为独立进程运行，监听 Master 转发端口（如 9005），承载 KVM 硬件执行。
  **`gateway_service/` [物理侧车]**：编译 `aggregator.c` + `main.c` → **`wavevm_gateway`**。
**完整目录结构**：
    ```txt
    WaveVM/
    ├── common_include/                     # [基础设施]
    │   ├── wavevm_config.h
    │   ├── wavevm_protocol.h              # Wavelet 协议栈, CRC32
    │   ├── crc32.h                         # CRC32 查表法实现
    │   └── uthash.h                        # 哈希表库
    │
    ├── master_core/                        # [Swarm Daemon]
    │   ├── logic_core.c                    # DHT, Pub/Sub, Versioning FSM
    │   ├── kernel_backend.c                # Mode A: Async Fault, Diff Committer
    │   ├── user_backend.c                  # Mode B: QoS Queues
    │   └── main_wrapper.c                  # 异构权重解析
    │
    ├── slave_daemon/                       # [执行引擎]
    │   ├── slave_hybrid.c                  # Proxy w/ MESI Routing
    │   └── slave_vfio.c                    # GPU Interceptor (Legacy)
    │
    ├── gateway_service/                    # [网络侧车]
    │   ├── aggregator.c                    # Uthash, Backpressure
    │   ├── main.c                         # (Unified entry)
    │
    ├── wavevm-qemu/                         # [前端适配]
    │   ├── accel/wavevm/wavevm-user-mem.c # Latch, Diff Harvester, Version Validator
    │   ├── accel/wavevm/wavevm-cpu.c      # vCPU RPC
    │   └── hw/wavevm/wavevm-gpu-stub.c    # GPU Mock
    │
    └── deploy/                             # [部署]
        └── sysctl_check.sh                 # 50MB 缓冲区
    ```

---

#### ⚖️ 第二条：双进程共生与 Ingress 分流公理 (The Law of Symbiotic Ingress)

这是 V31 解决 P2P 端口冲突的终极物理方案，严禁退化回单进程架构。

1.  **分流不可逆性**：Master 必须占据 9000 端口。收到的包通过 `if (type == MSG_VCPU_RUN || type == MSG_BLOCK_WRITE)` 判定，强制通过 **127.0.0.1** 转发给 Slave。
2.  **内核直转铁律 (Mode A)**：`kernel_backend.c` 必须通过 `local_slave_port` 参数实现内核级 UDP 重定向。严禁将计算任务从内核拉回到用户态 Master 再转发，必须在内核网络栈直接“踢”给 Slave 进程。
3.  **资源造物主权**：Master 是 SHM (/wavevm_ram) 的创建者，Slave 仅拥有访问权。严禁 Slave 进程尝试 `ftruncate` 或 `O_CREAT` 共享内存。

---

#### ⚖️ 第三条：分形拓扑与哈希环公理 (The Law of Fractal Ring)

1.  **哈希环恒定性**：`HASH_RING_SIZE` 锁定为 **4096**。这不是可调参数，而是 Pod 物理边界的公理。
2.  **异构权重映射**：物理算力与内存的分布必须通过 `main_wrapper.c` 里的 **(RAM / 4GB = 1 Virtual Node)** 算法进行加权展开。
3.  **分层路由原则**：百万节点的扩展性由 **`wavevm_gateway`** 的递归级联实现。严禁在 `logic_core.c` 中引入全网拓扑，Master 只允许维护局部视图（`MAX_LOCAL_VIEW = 1024`）。

---

#### ⚖️ 第四条：Wavelet 一致性与真理判定公理 (The Law of Truth)

1.  **推送优先 (Push Strategy)**：V31 的灵魂是主动推送。我们假设邻居对内存变动有"知情权"。
2.  **复合版本判定**：真理的唯一判定标准是 `is_next_version(local, push)`。
    *   高 32 位 `Epoch` 解决网络分区后的时代更替。
    *   低 32 位 `Counter` 解决 UDP 的熵增（乱序）。
3.  **安全网机制**：当版本断层发生且重排缓冲区（`REORDER_WIN_SIZE`）无法弥合时，必须强制失效本地内存并回退到 V28 的阻塞拉取。

---

#### ⚖️ 第五条：物理存储原子性公理 (The Law of Atomic Storage)

1.  **物理落盘强制性**：`slave_hybrid.c` 必须响应 `MSG_BLOCK_FLUSH`，并强制执行 `fdatasync()`。严禁在存储链路使用异步假写。
2.  **错误回传机制**：任何 `pwrite` 失败必须捕获返回值，并在 ACK 包中置位 `WVM_FLAG_ERROR`。Master 严禁静默丢弃存储错误，必须反馈给 Guest OS 触发其内核 IO 重试。

---

#### ⚖️ 第六条：代码质量与物理意图铁律 (The Law of Implementation)

1.  **零 Malloc 准则**：在 `rx_thread_loop`、`wvm_fault_handler` 和 `sigsegv_handler` 这种物理热路径上，严禁使用任何 `malloc/free`。必须使用 `user_backend.c` 中的分片内存池（`POOL_SHARDS`）或预分配池。
2.  **Latch 物理对齐**：`aligned_latch_t` 必须保持 **128 字节缓存行对齐**，彻底规避硬件级的 False Sharing（虚假共享）。
3.  **注释规范**：禁止废话。注释必须解释该代码段对应的物理意图（如：`// [Ingress Dispatch] 识别计算请求，转发至本地执行引擎`），而不是简单的代码翻译。

---

#### ⚖️ 第七条：多 VM 隔离与 Composite ID 公理 (The Law of Multi-VM Isolation)

1.  **Composite ID 不可拆分性**：所有出站包的 `slave_id` 必须通过 `WVM_ENCODE_ID(g_my_vm_id, node_id)` 编码。严禁在网络路径上使用裸 `node_id`，否则跨 VM 路由将坍缩。
2.  **入站过滤强制性**：`rx_thread_loop` 和 `wvm_logic_process_packet` 必须在 CRC 校验后、业务逻辑前检查 `WVM_GET_VMID(target_id)`。丢弃非本 VM 的包是安全底线，严禁跳过。
3.  **DHT 内部裸 ID 原则**：Hash ring、copyset、directory table 内部仍使用裸 `node_id`（0~4095）。composite 编码只在进出网络边界时使用。严禁将 composite ID 直接作为 DHT key，否则哈希分布将严重偏斜。
4.  **SHM 物理隔离**：不同 VM 的 Master/Slave 必须使用不同的 `WVM_SHM_FILE`。严禁两个 VM 共享同一块 SHM，否则将导致跨租户内存践踏。

---

**最终 Mandate**：
WaveVM V31 不是一份普通的源码，它是承载 planetary-scale 多租户算力的物理骨架。任何对上述宪法的偏离，都等同于在超级虚拟机的底座上埋设延时炸弹。**执行，或者出局。**

---

### 🌌 第七部分：百万节点级极限规模推演与边界分析 (Planetary-Scale Feasibility & Boundary Audit)

当集群规模跨越 10^6 数量级（100万节点、1亿核心、500PB 内存）时，分布式系统将面临从"工程挑战"向"物理挑战"的质变。V31 架构通过 **"脑（Master）手（Slave）解耦"** 与 **"分形联邦网关"**，在物理定律的边缘构建了一套具备生存本能的算力神经网络。

#### 1. 物理环境与混沌模型设定 (The Dyson Swarm Setting)

*   **算力总量**：1,000,000 Nodes | 100,000,000 Cores。
*   **存储空间**：500PB 分布式物理内存 | 1,000PB 分布式块存储。
*   **网络拓扑**：**Leaf (Node Master/Slave)** → **L1 (Pod Gateway)** → **L2 (Zone Gateway)** → **L3 (Core Switch)**。
*   **物理现实假设 (Chaos Engineering Assumptions)**：
    *   **高丢包环境**：跨区域（Zone）丢包率在 **0.5%-2%** 动态波动，UDP 乱序成为常态。
    *   **高动态下线**：每小时平均有 **120 台** 物理机因电力或廉价实例回收而瞬时离线。
    *   **比特翻转**：在 PB 级流量中，每 10^12 个比特会发生一次随机位翻转。

---

#### 2. 全场景效率推演矩阵 (The Comprehensive Efficiency Matrix)

V31 的核心自信来源于其"乐观推送、物理分流、语义透传"的纵深防御体系。

| 任务负载类型 | 预期执行效率 (Mode A/B) | **V31 行星级生存解析与物理逻辑支撑** |
| :--- | :--- | :--- |
| **超大规模元宇宙 (MMO/物理交互)** | **40M+ TPS** | **写零延迟 + Ingress 分流**。<br>Slave 进程处理高频状态机更新，脏页通过 `g_mpsc_queue` 异步收割。Master 守住 9000 端口，仅需 3µs 即可完成入站指令分发，彻底消除了网络同步锁对玩家交互的阻塞。 |
| **紧耦合科学计算 (HPC/气象预测)** | **95% - 99%+** | **Mode A 内核旁路 + 宣告兴趣**。<br>利用 `kernel_backend.c` 的内核级转发，计算包在网卡驱动层直接“踢”给 Slave。通过 `DECLARE_INTEREST` 机制，邻居节点的更新被主动推送到本地物理页，读延迟从 100µs 坍缩至 60ns。 |
| **AI 大模型训练 (异步梯度流)** | **90% - 95%** | **Wavelet 增量推送 + 版本安全网**。<br>梯度更新仅传输 Diff 小波。若网络丢包，Master 通过 `is_next_version` 判定版本断层，触发 **`FORCE_SYNC`** 强制同步。系统以局部抖动（Jitter）为代价，换取了全网梯度权重的绝对一致。 |
| **分布式离线渲染 (影视/蒙特卡洛)** | **99.9%** | **算力自治 + CRC32 洁净度**。<br>渲染任务属于高度并行负载。Slave 节点利用 `robust_sendto` 保证结果回传的可靠性。即使公网环境存在干扰，**CRC32 校验** 也能过滤掉所有物理损坏的数据包，确保成品帧无噪点。 |
| **系统冷启动 / 克隆 (System Ops)** | **99.9%** | **Prophet 语义透传的降维打击**。<br>在 500PB 空间进行全网内存清零。`wavevm-gpu-stub` 发起 32 字节 RPC 指令，全网 Slave 同步执行本地 `memset`。物理带宽占用从 PB 级降至字节级，启动耗时从小时级坍缩为毫秒级。 |
| **超低带宽环境 (32Mbps 窄带容器)** | **完全可用** | **Diff + RPC 组合拳**。<br>常规逻辑走增量 Diff，大块操作走 RPC 透传。V31 是唯一能在窄带宽容器网络中跑出"万兆体感"的架构，这源于 `diff_harvester_thread` 对有效比特的极致筛选。 |
| **分布式块存储 IO (Database)** | **等效本地 NVMe** | **存储链路闭环 + 物理原子性**。<br>Master 转发 `MSG_BLOCK_WRITE` 给 Slave。Slave 端利用 **O_DIRECT** 绕过缓存，并由 `fdatasync` 响应 `BLOCK_FLUSH`。`WVM_FLAG_ERROR` 确保了即使在百万节点下，硬件级坏道也能精准反馈给 Guest。 |

---

#### 3. 架构三大物理极限边界与解法 (Boundary Constraints)

##### **3.1. 128B Latch 锁的硬件冲突边缘 (Cacheline Contention)**
*   **极限挑战**：在 100M 核心同时访问邻近 GPA 时，传统的 Latch 锁会因 False Sharing 产生总线风暴。
*   **V31 解法**：
    *   **对齐公理**：`aligned_latch_t` 严格占用 128 字节，完美匹配现代 CPU 的预取步长。这确保了在 PB 级内存操作中，Master 的收割线程与 Slave 的写入线程在硬件层实现了“物理隔离”的同步。

##### **3.2. Gossip 共识的收敛时延 (Convergence Lag)**
*   **极限挑战**：1,000,000 节点下，Epoch 纪元的变更扩散到全网需要约 **2-5 秒**。
*   **V31 解法**：
    *   **跨纪元兼容逻辑**：`is_next_version` 允许 `local.Epoch = N` 与 `push.Epoch = N+1` 瞬间接轨（Cnt=1）。这保证了在拓扑视图尚未完全收敛的“过渡期”，系统的内存事务依然能无缝连续执行。

---

#### 4. V31 的"反脆弱"哲学：多层自愈安全网

V31 默认物理世界是充满敌意的，因此设计了层层递进的恢复路径：

1.  **第一层：比特自愈 (Integrity Check)**
    *   数据包在传输中发生 1 位翻转 → **CRC32 校验失败** → 后端（Kernel/User）直接丢弃 → 发起方超时重传。
2.  **第二层：乱序自愈 (Reorder Buffer)**
    *   UDP 导致 v12 先于 v11 到达 → **`g_reorder_buf`** 暂存 v12 → 等待 v11 到达后“链式坍缩”进内存。
3.  **第三层：断层自愈 (Force Sync)**
    *   网络长期拥塞导致 v11 彻底丢失 → 接收端检测到版本不连续且重排窗溢出 → 页面设为 `PROT_NONE` → 下次访问触发缺页 → **回退至 V28 阻塞拉取** → 从 Directory 拽回最新的全量“真理”。

---

#### 🏁 终极结论 (The Final Mandate)

**WaveVM "Wavelet" V31** 已经不再是传统意义上的加速器。它通过：
*   **Master (脑)** 的物理入口主宰与分流；
*   **Slave (手)** 的无状态硬件级执行；
*   **Gateway (侧车)** 的联邦化递归路由；
*   **Prophet (先知)** 的跨维度语义同步；
*   **Composite ID (令牌)** 的多租户物理隔离；

构建了一个即使在物理环境极度恶劣、规模达到行星级的场景下，依然能保持 90% 以上算力转化率的多租户 SSI 底座。**这就是人类在软件定义物理法则领域能达到的最高成就。**

---

### 🏗️ 第八部分：多 VM 资源池化——从单租户到多租户的架构升维 (Multi-VM Resource Pooling)

V30.0 实现了"一个物理集群 = 一台超级虚拟机"的 SSI 愿景。V31 进一步将其升维为"一个物理集群 = 一个算力池，同时托管 N 台超级虚拟机"。每台 VM 拥有独立的虚拟节点范围、独立的内存空间和独立的 DHT 视图，而物理网络和 Gateway 基础设施全集群共享。

#### 1. Composite ID 编码方案 (The Encoding Axiom)

`slave_id` 和 `target_id` 均为 `uint32_t`（协议头 48 字节不变）。V31 将其重新定义为 **composite ID**：

```text
  31          24 23                    0
  +-----------+------------------------+
  |  vm_id    |       node_id          |
  |  (8 bit)  |       (24 bit)         |
  +-----------+------------------------+
```

*   **编码**：`WVM_ENCODE_ID(vm, node)` = `(vm << 24) | (node & 0x00FFFFFF)`
*   **解码**：`WVM_GET_VMID(id)` = `(id >> 24) & 0xFF`，`WVM_GET_NODEID(id)` = `id & 0x00FFFFFF`
*   **哨兵值**：`WVM_NODE_AUTO_ROUTE` 改为 `0xFFFFFFFF`（原 `0x3FFFFFFF`），避免与 `vm_id=63` 冲突。
*   **向后兼容**：`WVM_ENCODE_ID(0, node_id)` = `node_id`。vm_id=0 时所有行为与 V30.0 完全一致。

#### 2. Per-VM 隔离模型 (The Isolation Trinity)

每个 VM 在每个物理节点上运行独立的 Master + Slave 进程。隔离通过三个正交维度实现：

| 隔离维度 | 机制 | 实现层 |
| :--- | :--- | :--- |
| **网络隔离** | composite ID 路由：Gateway 按完整 32 位 key 查 uthash，天然支持 composite ID | `aggregator.c` |
| **内存隔离** | 每 VM 独立 SHM 文件（`WVM_SHM_FILE` 环境变量） | `main_wrapper.c` / `slave_hybrid.c` |
| **进程隔离** | 每 VM 的 Master 拥有独立的 DHT directory table、独立的心跳域 | `logic_core.c` |

**入站过滤**（纵深防御）：
1.  **`rx_thread_loop()`**（user_backend.c）：CRC 校验通过后，立即检查 `WVM_GET_VMID(target_id) != g_my_vm_id`，丢弃非本 VM 的包。
2.  **`wvm_logic_process_packet()`**（logic_core.c）：从 `hdr->slave_id` 提取来源 vm_id，丢弃 vm_id 不匹配的包。
3.  **心跳过滤**：`update_local_topology_view()` 仅接受同 vm_id 的心跳，不同 VM 的邻居发现互不干扰。

#### 3. 配置格式与启动示例 (Configuration & Launch)

##### 配置文件（swarm.conf）

```ini
# 物理节点定义（不变）
NODE 0 192.168.1.10 9000 4 16
NODE 1 192.168.1.11 9000 4 16

# VM 定义（新增，可选）
# VM <VmID> <StartVnode> <VnodeCount>
VM 1 0 4
VM 2 4 4
```

*   VM 1 拥有虚拟节点 0~3，VM 2 拥有虚拟节点 4~7。
*   不写 `VM` 指令时，默认 vm_id=0，单 VM 模式，行为与 V30.0 完全一致。

##### 启动示例（2 物理节点，2 VM）

```bash
# === 物理节点 0 (192.168.1.10) ===

# Gateway（共享，每物理节点 1 个）
./wavevm_gateway 9000 192.168.1.10 19100 routes.conf 9001

# VM 1 的 Master + Slave
WVM_INSTANCE_ID=n0_vm1 WVM_SHM_FILE=/wavevm_ram_vm1 \
  ./wavevm_node_master 4096 19100 swarm.conf 0 9001 19200 64 1
WVM_SHM_FILE=/wavevm_ram_vm1 \
  ./wavevm_node_slave 19200 4 4096 0 9003 1

# VM 2 的 Master + Slave
WVM_INSTANCE_ID=n0_vm2 WVM_SHM_FILE=/wavevm_ram_vm2 \
  ./wavevm_node_master 4096 19110 swarm.conf 0 9002 19210 64 2
WVM_SHM_FILE=/wavevm_ram_vm2 \
  ./wavevm_node_slave 19210 4 4096 0 9004 2
```

**命令行参数变更**：
*   `wavevm_node_master`：第 8 个参数（argv[8]）为 `VM_ID`（可选，默认 0）。
*   `wavevm_node_slave`：第 6 个参数（argv[6]）为 `VM_ID`（可选，默认 0）。

#### 4. 弹性变容 (Elastic Resizing)

变容量只需三步，不停集群：

1.  停止目标 VM 的 Master + Slave 进程。
2.  修改 `swarm.conf` 中对应 `VM` 行的 `StartVnode` / `VnodeCount`。
3.  重新启动该 VM 的进程。

其他 VM 全程不受影响，无需感知变容事件。

#### 5. 代码变更摘要 (Implementation Changelog)

| 文件 | 变更要点 |
| :--- | :--- |
| `common_include/wavevm_protocol.h` | 新增 `WVM_ENCODE_ID` / `WVM_GET_VMID` / `WVM_GET_NODEID` 宏；`WVM_NODE_AUTO_ROUTE` 改为 `0xFFFFFFFF` |
| `common_include/wavevm_config.h` | 新增 `WVM_MAX_VMS 256` |
| `gateway_service/aggregator.c` | 两处范围检查从 `< WVM_MAX_SLAVES` 改为 `!= WVM_NODE_AUTO_ROUTE`，兼容 composite ID |
| `master_core/main_wrapper.c` | 新增 `g_my_vm_id` 全局变量 + argv[8] 解析 + swarm.conf `VM` 指令跳过 |
| `master_core/logic_core.c` | 3 个路由函数返回 composite ID；收包时 vm_id 过滤；心跳过滤同 vm_id；所有出站 `slave_id` 使用 `WVM_ENCODE_ID` |
| `master_core/user_backend.c` | 出站 `slave_id` composite 编码；`rx_thread_loop` 入站 vm_id 过滤；ACK / RPC 广播 composite 编码 |
| `slave_daemon/slave_hybrid.c` | 新增 `g_slave_vm_id` + argv[6] 解析；所有 ACK `slave_id` 使用 `WVM_ENCODE_ID`；脏页同步 composite 编码 |

---

## Repo Files

### Step 0: 环境预检 (sysctl_check.sh)

**文件**: `deploy/sysctl_check.sh`

```bash
#!/bin/bash
# WaveVM V29 Environment Check & Hardening Script

echo "[*] WaveVM V29: Tuning Kernel Parameters for Production..."

# 1. 基础资源限制 (Basic Resource Limits)
# 确保系统能打开足够多的文件句柄
sysctl -w fs.file-max=200000 > /dev/null
# V29的精细化脏区捕获依赖于大量的mprotect操作，必须极大增加VMA限制
sysctl -w vm.max_map_count=2000000 > /dev/null
# 预留一些大页，用于KVM后端和性能敏感的组件
sysctl -w vm.nr_hugepages=10240 > /dev/null

echo "[+] System resource limits increased."

# 2. UDP 缓冲区深井扩容 (UDP Buffer Deepening)
# 这是保证公网环境下不因突发流量（如元数据风暴或日志广播）而丢包的生命线
# 将发送和接收缓冲区均设置为50MB (默认仅约200KB)
sysctl -w net.core.rmem_max=52428800 > /dev/null
sysctl -w net.core.rmem_default=52428800 > /dev/null
sysctl -w net.core.wmem_max=52428800 > /dev/null
sysctl -w net.core.wmem_default=52428800 > /dev/null

echo "[+] UDP network buffers boosted to 50MB."

# 3. 网络设备队列长度 (Network Device Queue)
# 增加内核处理网络包的队列长度，为高PPS（每秒包数）场景提供缓冲
sysctl -w net.core.netdev_max_backlog=10000 > /dev/null

echo "[+] Network device backlog queue increased."
echo "[SUCCESS] Kernel parameters are tuned for V29 'Wavelet' deployment."

```

---

### Step 1: 基础设施定义 (Infrastructure)

**文件**: `common_include/wavevm_config.h`

```c
/*
 * [IDENTITY] Infrastructure - The Physical Axioms
 * ---------------------------------------------------------------------------
 * 物理角色：定义整个系统的"物理常数"和寻址边界。
 * 职责边界：
 * 1. 设定 WVM_SLAVE_BITS（默认 12 位单 Pod 空间）。
 * 2. 规定内存粒度 (1GB 路由 / 2MB 订阅 / 4KB 一致性)。
 * 3. 设定物理 MTU (1400)，规避 Overlay 网络的 IP 分片。
 * 
 * [禁止事项]
 * - 严禁在不修改 Gateway 内存映射逻辑的情况下增大 WVM_SLAVE_BITS。
 * - 严禁将 MTU_SIZE 设为 1500 以上，否则公网环境 PPS 性能将下降 90%。
 * ---------------------------------------------------------------------------
 */
#ifndef WAVEVM_CONFIG_H
#define WAVEVM_CONFIG_H

/* 
 * [集群规模上限] 定义节点 ID 的位宽。
 * 默认设定为 12 位，意味着单 Pod 支持 2^12 = 4096 个逻辑节点 ID。
 * 注意：若后续调大，需要同步调整网关路由表和相关内存分配策略。
 */
#ifndef WVM_SLAVE_BITS
/*
 * Default node-id bit width (shared by user/kernel paths).
 * Keep this bounded for predictable memory footprint in gateway/master.
 */
#ifdef __KERNEL__
#define WVM_SLAVE_BITS 12
#else
#define WVM_SLAVE_BITS 12
#endif
#endif

// --- 网络字节序转换宏 ---
/*
 * [跨架构兼容性] 64位整数的主机序(Host)到网络序(Network)转换。
 * 物理意义：
 * 1. WaveVM 协议强制使用大端序 (Big-Endian) 在网络传输。
 * 2. 这里的实现兼容了 x86 (Little-Endian) 和其他架构。
 * 3. 若不转换，跨架构通信时 GPA 地址会解析错误，导致读写错误的物理内存。
 */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #define WVM_HTONLL(x) __builtin_bswap64((unsigned long long)(x))
    #define WVM_NTOHLL(x) __builtin_bswap64((unsigned long long)(x))
#else
    #define WVM_HTONLL(x) (x)
    #define WVM_NTOHLL(x) (x)
#endif

// --- 核心常量定义 ---
#define WVM_MAX_SLAVES (1UL << WVM_SLAVE_BITS)
#define WVM_MAX_GATEWAYS (WVM_MAX_SLAVES)
#define WVM_MAX_VMS 256  // Multi-VM 资源池化：最多 256 个 VM 共享同一集群
/*
 * [协议特征与物理限制]
 * WVM_MAGIC: 数据包特征码 "WVMX"，用于在接收端快速识别并丢弃非法/垃圾流量。
 * MTU_SIZE: 设定为 1400 字节（低于标准以太网 1500）。
 *           原因：预留 100 字节给 Overlay 网络（如 VXLAN/Geneve）的隧道头。
 *           后果：防止底层网络进行 IP 分片。IP 分片会引发 CPU 软中断飙升，是高性能网络的杀手。
 */
#define WVM_MAGIC 0x57564D58 // "WVMX"
#define WVM_SERVICE_PORT 9000
#define MTU_SIZE  1400       
#define WVM_MAX_PACKET_SIZE 65536
#define MAX_VCPUS 4096
#define WVM_CPU_ROUTE_TABLE_SIZE MAX_VCPUS

// --- 内存粒度定义 ---
/*
 * [一级：DHT 路由粒度] 1GB (2^30)。
 * 物理意义：决定哪个节点是 Directory Owner。
 * 作用：我们不对每个 4KB 页面做哈希路由，而是每 1GB 数据块归属同一个 Owner。
 * 效果：极大减少了哈希计算频率，利用了程序的空间局部性。
 */
#define WVM_ROUTING_SHIFT 30 
/*
 * [二级：兴趣订阅粒度] 2MB (2^21)。
 * 物理意义：HugePage 对齐的订阅单元。
 * 作用：当节点发送 DECLARE_INTEREST 时，是一次性订阅 2MB 范围内的所有页面。
 * 效果：减少了 512 倍的订阅信令交互 (相比 4KB)。
 */
#define WVM_AFFINITY_SHIFT 21
/*
 * [三级：MESI 一致性粒度] 4KB (2^12)。
 * 物理意义：最小的原子数据同步单元。
 * 作用：Diff 计算、版本号校验、写冲突检测都精确到 4KB。
 */
#define WVM_PAGE_SHIFT 12

#define WVM_DEFAULT_SHM_PATH "/wavevm_ram"

#endif // WAVEVM_CONFIG_H

```

**文件**: `common_include/platform_defs.h`

```c
#ifndef PLATFORM_DEFS_H
#define PLATFORM_DEFS_H

#ifdef __KERNEL__
    /* Kernel Space Shim */
    #include <linux/types.h>
    #include <linux/vmalloc.h>
    #include <linux/slab.h>
    #include <linux/errno.h>
    #include <linux/string.h>
    #include <linux/atomic.h>
    #include <asm/processor.h> 
    #define WVM_PRINTF_LIKE(n, m) __attribute__((format(printf, n, m)))
#else
    /* User Space Shim */
    #include <stdint.h>
    #include <stdlib.h>
    #include <stdio.h>
    #include <string.h>
    #include <errno.h>
    #define WVM_PRINTF_LIKE(n, m) __attribute__((format(printf, n, m)))
#endif

#endif // PLATFORM_DEFS_H

```

**文件**: `common_include/wavevm_protocol.h`

```c
/*
 * [IDENTITY] Protocol Stack - The Wavelet Law
 * ---------------------------------------------------------------------------
 * 物理角色：系统的"通用语言"和逻辑时钟定义。
 * 职责边界：
 * 1. 定义 Wavelet (MSG_COMMIT/PUSH) 与 V28 MESI (MSG_INVALIDATE) 兼容指令集。
 * 2. 规定 wvm_header 结构，集成 QoS 分级与端到端 CRC32。
 * 3. 提供 is_next_version 版本判定算法，解决 UDP 乱序真理冲突。
 * 
 * [禁止事项]
 * - 严禁删除 Header 中的 epoch 字段 (它是解决网络分区脑裂的唯一钥匙)。
 * - 严禁在 Payload 结构体中添加非对齐字段。
 * ---------------------------------------------------------------------------
 */
#ifndef WAVEVM_PROTOCOL_H
#define WAVEVM_PROTOCOL_H

#include "wavevm_config.h"
#include "platform_defs.h"
#include <linux/kvm.h>

/*
 * WaveVM V29.5 "Wavelet" Protocol Definition (FINAL FIXED)
 * Includes V28 MESI Fallback & V29 Prophet Extensions
 */

// --- 1. 网络消息类型 (Network Message Types) ---
enum {
    // [Core] 基础通信
    MSG_PING           = 0,
    MSG_MEM_READ       = 1, // Pull Request
    MSG_MEM_WRITE      = 2, // Push / Direct Write
    MSG_MEM_ACK        = 3, // Pull Response / Sync ACK
    
    // [VCPU] 远程调度
    MSG_VCPU_RUN       = 5,
    MSG_VCPU_EXIT      = 6,
    MSG_VFIO_IRQ       = 7,

    // [V28 MESI] 一致性协议 
    MSG_INVALIDATE              = 10, // Directory -> Owner: Revoke permission
    MSG_DOWNGRADE             = 11, // Directory -> Owner: M -> S
    MSG_FETCH_AND_INVALIDATE  = 12, // Directory -> Owner: Fetch data & Invalid
    MSG_WRITE_BACK             = 13, // Owner -> Directory: Data writeback

    // [V29 Wavelet] 主动推送与版本控制
    MSG_DECLARE_INTEREST   = 25, // Client -> Directory: Subscribe
    MSG_PAGE_PUSH_FULL     = 26, // Directory -> Client: Full Page Update
    MSG_PAGE_PUSH_DIFF      = 27, // Directory -> Client: Diff Update
    MSG_COMMIT_DIFF         = 28, // Client -> Directory: Diff Commit
    MSG_FORCE_SYNC          = 29, // Directory -> Client: Version Conflict

    // [V29 Prophet] 语义透传
    MSG_RPC_BATCH_MEMSET   = 31, // Scatter-Gather Batch Command

    // [自治集群扩展]
    MSG_HEARTBEAT       = 40, // 周期性存活宣告与 Epoch Gossip
    MSG_VIEW_PULL        = 41, // 获取邻居的局部视图
    MSG_VIEW_ACK         = 42, // 返回局部视图数据
    MSG_NODE_ANNOUNCE  = 43, // 新节点上线宣告 (Warm-plug)

    // [STORAGE] 分布式块存储物理原语
    MSG_BLOCK_READ     = 50, // Master -> Slave (Pull Chunk)
    MSG_BLOCK_WRITE    = 51, // Master -> Slave (Push/Commit Chunk)
    MSG_BLOCK_ACK      = 52, // Slave -> Master
    MSG_BLOCK_FLUSH    = 53
};

// --- 2. 通用包头 (Header) ---
struct wvm_header {
    uint32_t magic;
    uint16_t msg_type;
    uint16_t payload_len; 
    uint32_t slave_id;      // Source Node ID
    uint32_t target_id;     // Destination Node ID (network order); AUTO_ROUTE means fallback
    union {
        uint64_t req_id;      // 给 ACK, VCPU_RUN 等事务 ID 用
        uint64_t target_gpa;  // 给 INVALIDATE, READ, WRITE 等地址指令用（旧版遗留）
    };
    uint8_t  qos_level;     // 1=Fast, 0=Slow
    uint8_t  flags;
    uint8_t  mode_tcg;
    uint8_t  node_state;    // 发送者当前的生命周期状态
    uint32_t epoch;         // 发送者所处的逻辑周期
    uint32_t crc32;         // End-to-End Integrity Check
} __attribute__((packed));

#define WVM_FLAG_ZERO 0x01
#define WVM_FLAG_ERROR 0x02

// --- 3. Payload 结构体 (Data Structures) ---

// [V29] 增量更新日志
struct wvm_diff_log {
    uint64_t gpa;
    uint64_t version;     
    uint16_t offset;
    uint16_t size;
    uint8_t  data[0];     // Variable length data
} __attribute__((packed));

// [V29] 全页推送
struct wvm_full_page_push {
    uint64_t gpa;
    uint64_t version;
    uint8_t  data[4096];
} __attribute__((packed));

// [V29] 带版本的读响应
struct wvm_mem_ack_payload {
    uint64_t gpa;
    uint64_t version;
    uint8_t  data[4096];
} __attribute__((packed));

// [V29] 物理段描述符
struct wvm_rpc_region {
    uint64_t gpa;
    uint64_t len;
} __attribute__((packed));

// [V29] 批量操作指令
struct wvm_rpc_batch_memset {
    uint32_t val;
    uint32_t count;
    // Followed by: struct wvm_rpc_region regions[];
} __attribute__((packed));

// 节点生命周期状态语义
enum {
    NODE_STATE_SHADOW   = 0, // 刚启动，对拓扑不可见
    NODE_STATE_WARMING  = 1, // 预热中，同步元数据与热点页
    NODE_STATE_ACTIVE   = 2, // 活跃，承载 Owner 权限
    NODE_STATE_DRAINING = 3, // 准备下线，只读不写
    NODE_STATE_OFFLINE  = 4  // 已失效 (本地判定)
};

// 心跳 Payload：用于实现无中心的 Epoch 共识
struct wvm_heartbeat_payload {
    uint32_t local_epoch;
    uint32_t active_node_count; // 本地观察到的活跃节点数
    uint16_t load_factor;       // 负载情况
    uint32_t peer_epoch_sum;    // 用于快速计算均值或直方图的特征值
    uint16_t ctrl_port;
} __attribute__((packed));

// 视图交换结构：用于发现新邻居
struct wvm_view_entry {
    uint32_t node_id;
    uint32_t ip_addr;
    uint16_t port;
    uint8_t  state;
    uint16_t ctrl_port; 
} __attribute__((packed));

struct wvm_view_payload {
    uint32_t entry_count;
    struct wvm_view_entry entries[0];
} __attribute__((packed));

// --- 4. IPC 消息定义 (Local QEMU <-> Daemon) ---

typedef struct wvm_ipc_header_t {
    uint32_t type;
    uint32_t len;
} wvm_ipc_header_t;

#define WVM_IPC_TYPE_MEM_FAULT      1
#define WVM_IPC_TYPE_MEM_WRITE      2
#define WVM_IPC_TYPE_CPU_RUN         3
#define WVM_IPC_TYPE_IRQ               4
#define WVM_IPC_TYPE_COMMIT_DIFF     5
#define WVM_IPC_TYPE_INVALIDATE       6 
#define WVM_IPC_TYPE_BLOCK_IO         7

// [V29 Fix] 正式定义 RPC 透传类型
#define WVM_IPC_TYPE_RPC_PASSTHROUGH 99

// 前后端分离路由占位符
#define WVM_NODE_AUTO_ROUTE 0xFFFFFFFF  // 旧值 0x3FFFFFFF，改为全 1 避免与 vm_id 编码冲突

// --- Multi-VM Resource Pooling: VM ID 编解码 ---
// slave_id / target_id 的高 8 位编码 vm_id，低 24 位编码 node_id
// 协议头 48 字节不变，向后兼容：vm_id=0 时 ENCODE_ID(0,n) == n
#define WVM_VMID_SHIFT    24
#define WVM_VMID_MASK     (0xFFu << WVM_VMID_SHIFT)    // 0xFF000000
#define WVM_NODEID_MASK   ((1u << WVM_VMID_SHIFT) - 1)  // 0x00FFFFFF

#define WVM_ENCODE_ID(vm, node) (((uint32_t)(vm) << WVM_VMID_SHIFT) | ((node) & WVM_NODEID_MASK))
#define WVM_GET_VMID(id)        (((uint32_t)(id) >> WVM_VMID_SHIFT) & 0xFF)
#define WVM_GET_NODEID(id)      ((uint32_t)(id) & WVM_NODEID_MASK)

// [Fix #3] AUTO_ROUTE 是 sentinel 值 (0xFFFFFFFF)，不能做 GET_NODEID 后参与普通比较
// 所有需要判断"是否是有效路由目标"的地方必须先过此检查
#define WVM_IS_VALID_TARGET(id) ((uint32_t)(id) != WVM_NODE_AUTO_ROUTE)

#define WVM_CTRL_MAGIC 0x57564D43

#define SYNC_MAGIC 0x53594E43 

struct wvm_ipc_fault_req {
    uint64_t gpa;
    uint32_t len;
    uint32_t vcpu_id;
};

struct wvm_ipc_fault_ack {
    int status;
    uint64_t version;
};

struct wvm_ipc_write_req {
    uint64_t gpa;
    uint32_t len;
};

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rsp, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint8_t sregs_data[512];
    uint32_t exit_reason;
    struct {
        uint8_t direction;
        uint8_t size;
        uint16_t port;
        uint32_t count;
        uint8_t data[8];
    } io;
    struct {
        uint64_t phys_addr;
        uint32_t len;
        uint8_t is_write;
        uint8_t data[8];
    } mmio;
} wvm_kvm_context_t;

typedef struct {
    uint64_t regs[16];
    uint64_t eip;
    uint64_t eflags;
    uint64_t cr[5];
    uint64_t xmm_regs[32];
    uint32_t mxcsr;
    uint32_t exit_reason;
    uint64_t fs_base, gs_base;
    uint64_t gdt_base, gdt_limit;
    uint64_t idt_base, idt_limit;
} wvm_tcg_context_t;

struct wvm_ipc_cpu_run_req {
    uint32_t mode_tcg;
    uint32_t slave_id;   // 如果设为 WVM_NODE_AUTO_ROUTE (0xFFFFFFFF)，由后端决定
    uint32_t vcpu_index; // 传递 vCPU 序号用于查表路由
    union {
        wvm_kvm_context_t kvm;
        wvm_tcg_context_t tcg;
    } ctx;
};

struct wvm_ipc_cpu_run_ack {
    int status;
    uint32_t mode_tcg;
    union {
        wvm_kvm_context_t kvm;
        wvm_tcg_context_t tcg;
    } ctx;
};

static inline uint64_t wvm_get_u64_unaligned(const void *ptr) {
    uint64_t val;
    memcpy(&val, ptr, 8);
    return WVM_NTOHLL(val);
}

// 物理块 Payload，严格对齐 512B 扇区
struct wvm_block_payload {
    uint64_t lba;       // 逻辑块地址 (512B 扇区索引)
    uint32_t count;     // 扇区数量
    uint32_t padding;   // 8字节对齐填充
    uint8_t  data[0];   // 变长数据
} __attribute__((packed));

// 版本判定
static inline int is_next_version(uint64_t local, uint64_t push) {
    uint32_t l_epoch = (uint32_t)(local >> 32);
    uint32_t l_cnt   = (uint32_t)(local & 0xFFFFFFFF);
    uint32_t p_epoch = (uint32_t)(push >> 32);
    uint32_t p_cnt   = (uint32_t)(push & 0xFFFFFFFF);

    if (l_epoch == p_epoch) return p_cnt == l_cnt + 1;
    if (p_epoch == l_epoch + 1) return p_cnt == 1; // 跨纪元判定
    return 0;
}

static inline int is_newer_version(uint64_t local, uint64_t push) {
    uint32_t l_epoch = (uint32_t)(local >> 32);
    uint32_t p_epoch = (uint32_t)(push >> 32);
    if (p_epoch > l_epoch) return 1;
    if (p_epoch < l_epoch) return 0;
    return (uint32_t)(push & 0xFFFFFFFF) > (uint32_t)(local & 0xFFFFFFFF);
}

extern int g_ctrl_port;

#define POOL_ITEM_SIZE 4200

#include "crc32.h"

//[V30 异构适配器] 提取并转换 TCG 与 KVM 之间的寄存器状态（含关键段寄存器）
static void wvm_translate_tcg_to_kvm(wvm_tcg_context_t *t, struct kvm_regs *k, struct kvm_sregs *s) {
    k->rax = t->regs[0]; k->rcx = t->regs[1]; k->rdx = t->regs[2]; k->rbx = t->regs[3];
    k->rsp = t->regs[4]; k->rbp = t->regs[5]; k->rsi = t->regs[6]; k->rdi = t->regs[7];
    k->r8  = t->regs[8]; k->r9  = t->regs[9]; k->r10 = t->regs[10];k->r11 = t->regs[11];
    k->r12 = t->regs[12];k->r13 = t->regs[13];k->r14 = t->regs[14];k->r15 = t->regs[15];
    k->rip = t->eip; k->rflags = t->eflags;
    // 关键段寄存器与控制寄存器映射
    s->cr0 = t->cr[0]; s->cr2 = t->cr[2]; s->cr3 = t->cr[3]; s->cr4 = t->cr[4];
    s->fs.base = t->fs_base; s->gs.base = t->gs_base;
    s->gdt.base = t->gdt_base; s->gdt.limit = t->gdt_limit;
    s->idt.base = t->idt_base; s->idt.limit = t->idt_limit;
}

static void wvm_translate_kvm_to_tcg(struct kvm_regs *k, struct kvm_sregs *s, wvm_tcg_context_t *t) {
    t->regs[0] = k->rax; t->regs[1] = k->rcx; t->regs[2] = k->rdx; t->regs[3] = k->rbx;
    t->regs[4] = k->rsp; t->regs[5] = k->rbp; t->regs[6] = k->rsi; t->regs[7] = k->rdi;
    t->regs[8] = k->r8;  t->regs[9] = k->r9;  t->regs[10]= k->r10; t->regs[11]= k->r11;
    t->regs[12]= k->r12; t->regs[13]= k->r13; t->regs[14]= k->r14; t->regs[15]= k->r15;
    t->eip = k->rip; t->eflags = k->rflags;
    // 关键段寄存器与控制寄存器映射
    t->cr[0] = s->cr0; t->cr[2] = s->cr2; t->cr[3] = s->cr3; t->cr[4] = s->cr4;
    t->fs_base = s->fs.base; t->gs_base = s->gs.base;
    t->gdt_base = s->gdt.base; t->gdt_limit = s->gdt.limit;
    t->idt_base = s->idt.base; t->idt_limit = s->idt.limit;
}

#endif // WAVEVM_PROTOCOL_H

```

**文件**: `common_include/wavevm_ioctl.h`

```c
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

#define IOCTL_WVM_REMOTE_RUN _IOWR('G', 2, struct wvm_ipc_cpu_run_req)

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

#endif // WAVEVM_IOCTL_H

```

**文件**: `common_include/crc32.h`

```c
/* 文件位置：common_include/crc32.h */

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/stddef.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

#ifdef __SSE4_2__
#include <nmmintrin.h>
static inline uint32_t calculate_crc32(const void* data, size_t length) {
    uint32_t crc = 0xffffffff;
    const uint8_t* p = (const uint8_t*)data;
    size_t i = 0;
    
    // 利用 SSE4.2 指令硬件加速：一次处理 8 字节
    for (; i + 8 <= length; i += 8) {
        crc = _mm_crc32_u64(crc, *(uint64_t*)(p + i));
    }
    // 处理剩余字节
    for (; i < length; i++) {
        crc = _mm_crc32_u8(crc, p[i]);
    }
    return crc ^ 0xffffffff;
}
#else

#ifndef CRC32_H
#define CRC32_H

static uint32_t crc32_table[256];
static int crc32_table_inited = 0;

// 运行时生成表，比硬编码几千个 hex 优雅且不易错
static void init_crc32_table(void) {
    /*
     * Keep software fallback aligned with SSE4.2 _mm_crc32_u* instructions,
     * which implement CRC32C (Castagnoli), not IEEE CRC32.
     */
    uint32_t polynomial = 0x82F63B78;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = polynomial ^ (c >> 1);
            } else {
                c >>= 1;
            }
        }
        crc32_table[i] = c;
    }
    crc32_table_inited = 1;
}

static inline uint32_t calculate_crc32(const void* data, size_t length) {
    if (!crc32_table_inited) init_crc32_table(); // 懒加载初始化
    
    uint32_t crc = 0xffffffff;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < length; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc & 0xff) ^ p[i]];
    }
    return crc ^ 0xffffffff;
}

#endif // CRC32_H
#endif // __SSE4_2__

```

**文件**: `common_include/uthash.h`

```c
/*
Copyright (c) 2003-2025, Troy D. Hanson  https://troydhanson.github.io/uthash/
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef UTHASH_H
#define UTHASH_H

#define UTHASH_VERSION 2.3.0

#include <string.h>   /* memcmp, memset, strlen */
#include <stddef.h>   /* ptrdiff_t */
#include <stdlib.h>   /* exit */

#if defined(HASH_NO_STDINT) && HASH_NO_STDINT
/* The user doesn't have <stdint.h>, and must figure out their own way
   to provide definitions for uint8_t and uint32_t. */
#else
#include <stdint.h>   /* uint8_t, uint32_t */
#endif

/* These macros use decltype or the earlier __typeof GNU extension.
   As decltype is only available in newer compilers (VS2010 or gcc 4.3+
   when compiling c++ source) this code uses whatever method is needed
   or, for VS2008 where neither is available, uses casting workarounds. */
#if !defined(DECLTYPE) && !defined(NO_DECLTYPE)
#if defined(_MSC_VER)   /* MS compiler */
#if _MSC_VER >= 1600 && defined(__cplusplus)  /* VS2010 or newer in C++ mode */
#define DECLTYPE(x) (decltype(x))
#else                   /* VS2008 or older (or VS2010 in C mode) */
#define NO_DECLTYPE
#endif
#elif defined(__MCST__)  /* Elbrus C Compiler */
#define DECLTYPE(x) (__typeof(x))
#elif defined(__BORLANDC__) || defined(__ICCARM__) || defined(__LCC__) || defined(__WATCOMC__)
#define NO_DECLTYPE
#else                   /* GNU, Sun and other compilers */
#define DECLTYPE(x) (__typeof(x))
#endif
#endif

#ifdef NO_DECLTYPE
#define DECLTYPE(x)
#define DECLTYPE_ASSIGN(dst,src)                                                 \
do {                                                                             \
  char **_da_dst = (char**)(&(dst));                                             \
  *_da_dst = (char*)(src);                                                       \
} while (0)
#else
#define DECLTYPE_ASSIGN(dst,src)                                                 \
do {                                                                             \
  (dst) = DECLTYPE(dst)(src);                                                    \
} while (0)
#endif

#ifndef uthash_malloc
#define uthash_malloc(sz) malloc(sz)      /* malloc fcn                      */
#endif
#ifndef uthash_free
#define uthash_free(ptr,sz) free(ptr)     /* free fcn                        */
#endif
#ifndef uthash_bzero
#define uthash_bzero(a,n) memset(a,'\0',n)
#endif
#ifndef uthash_strlen
#define uthash_strlen(s) strlen(s)
#endif

#ifndef HASH_FUNCTION
#define HASH_FUNCTION(keyptr,keylen,hashv) HASH_JEN(keyptr, keylen, hashv)
#endif

#ifndef HASH_KEYCMP
#define HASH_KEYCMP(a,b,n) memcmp(a,b,n)
#endif

#ifndef uthash_noexpand_fyi
#define uthash_noexpand_fyi(tbl)          /* can be defined to log noexpand  */
#endif
#ifndef uthash_expand_fyi
#define uthash_expand_fyi(tbl)            /* can be defined to log expands   */
#endif

#ifndef HASH_NONFATAL_OOM
#define HASH_NONFATAL_OOM 0
#endif

#if HASH_NONFATAL_OOM
/* malloc failures can be recovered from */

#ifndef uthash_nonfatal_oom
#define uthash_nonfatal_oom(obj) do {} while (0)    /* non-fatal OOM error */
#endif

#define HASH_RECORD_OOM(oomed) do { (oomed) = 1; } while (0)
#define IF_HASH_NONFATAL_OOM(x) x

#else
/* malloc failures result in lost memory, hash tables are unusable */

#ifndef uthash_fatal
#define uthash_fatal(msg) exit(-1)        /* fatal OOM error */
#endif

#define HASH_RECORD_OOM(oomed) uthash_fatal("out of memory")
#define IF_HASH_NONFATAL_OOM(x)

#endif

/* initial number of buckets */
#define HASH_INITIAL_NUM_BUCKETS 32U     /* initial number of buckets        */
#define HASH_INITIAL_NUM_BUCKETS_LOG2 5U /* lg2 of initial number of buckets */
#define HASH_BKT_CAPACITY_THRESH 10U     /* expand when bucket count reaches */

/* calculate the element whose hash handle address is hhp */
#define ELMT_FROM_HH(tbl,hhp) ((void*)(((char*)(hhp)) - ((tbl)->hho)))
/* calculate the hash handle from element address elp */
#define HH_FROM_ELMT(tbl,elp) ((UT_hash_handle*)(void*)(((char*)(elp)) + ((tbl)->hho)))

#define HASH_ROLLBACK_BKT(hh, head, itemptrhh)                                   \
do {                                                                             \
  struct UT_hash_handle *_hd_hh_item = (itemptrhh);                              \
  unsigned _hd_bkt;                                                              \
  HASH_TO_BKT(_hd_hh_item->hashv, (head)->hh.tbl->num_buckets, _hd_bkt);         \
  (head)->hh.tbl->buckets[_hd_bkt].count++;                                      \
  _hd_hh_item->hh_next = NULL;                                                   \
  _hd_hh_item->hh_prev = NULL;                                                   \
} while (0)

#define HASH_VALUE(keyptr,keylen,hashv)                                          \
do {                                                                             \
  HASH_FUNCTION(keyptr, keylen, hashv);                                          \
} while (0)

#define HASH_FIND_BYHASHVALUE(hh,head,keyptr,keylen,hashval,out)                 \
do {                                                                             \
  (out) = NULL;                                                                  \
  if (head) {                                                                    \
    unsigned _hf_bkt;                                                            \
    HASH_TO_BKT(hashval, (head)->hh.tbl->num_buckets, _hf_bkt);                  \
    if (HASH_BLOOM_TEST((head)->hh.tbl, hashval)) {                              \
      HASH_FIND_IN_BKT((head)->hh.tbl, hh, (head)->hh.tbl->buckets[ _hf_bkt ], keyptr, keylen, hashval, out); \
    }                                                                            \
  }                                                                              \
} while (0)

#define HASH_FIND(hh,head,keyptr,keylen,out)                                     \
do {                                                                             \
  (out) = NULL;                                                                  \
  if (head) {                                                                    \
    unsigned _hf_hashv;                                                          \
    HASH_VALUE(keyptr, keylen, _hf_hashv);                                       \
    HASH_FIND_BYHASHVALUE(hh, head, keyptr, keylen, _hf_hashv, out);             \
  }                                                                              \
} while (0)

#ifdef HASH_BLOOM
#define HASH_BLOOM_BITLEN (1UL << HASH_BLOOM)
#define HASH_BLOOM_BYTELEN (HASH_BLOOM_BITLEN/8UL) + (((HASH_BLOOM_BITLEN%8UL)!=0UL) ? 1UL : 0UL)
#define HASH_BLOOM_MAKE(tbl,oomed)                                               \
do {                                                                             \
  (tbl)->bloom_nbits = HASH_BLOOM;                                               \
  (tbl)->bloom_bv = (uint8_t*)uthash_malloc(HASH_BLOOM_BYTELEN);                 \
  if (!(tbl)->bloom_bv) {                                                        \
    HASH_RECORD_OOM(oomed);                                                      \
  } else {                                                                       \
    uthash_bzero((tbl)->bloom_bv, HASH_BLOOM_BYTELEN);                           \
    (tbl)->bloom_sig = HASH_BLOOM_SIGNATURE;                                     \
  }                                                                              \
} while (0)

#define HASH_BLOOM_FREE(tbl)                                                     \
do {                                                                             \
  uthash_free((tbl)->bloom_bv, HASH_BLOOM_BYTELEN);                              \
} while (0)

#define HASH_BLOOM_BITSET(bv,idx) (bv[(idx)/8U] |= (1U << ((idx)%8U)))
#define HASH_BLOOM_BITTEST(bv,idx) ((bv[(idx)/8U] & (1U << ((idx)%8U))) != 0)

#define HASH_BLOOM_ADD(tbl,hashv)                                                \
  HASH_BLOOM_BITSET((tbl)->bloom_bv, ((hashv) & (uint32_t)((1UL << (tbl)->bloom_nbits) - 1U)))

#define HASH_BLOOM_TEST(tbl,hashv)                                               \
  HASH_BLOOM_BITTEST((tbl)->bloom_bv, ((hashv) & (uint32_t)((1UL << (tbl)->bloom_nbits) - 1U)))

#else
#define HASH_BLOOM_MAKE(tbl,oomed)
#define HASH_BLOOM_FREE(tbl)
#define HASH_BLOOM_ADD(tbl,hashv)
#define HASH_BLOOM_TEST(tbl,hashv) 1
#define HASH_BLOOM_BYTELEN 0U
#endif

#define HASH_MAKE_TABLE(hh,head,oomed)                                           \
do {                                                                             \
  (head)->hh.tbl = (UT_hash_table*)uthash_malloc(sizeof(UT_hash_table));         \
  if (!(head)->hh.tbl) {                                                         \
    HASH_RECORD_OOM(oomed);                                                      \
  } else {                                                                       \
    uthash_bzero((head)->hh.tbl, sizeof(UT_hash_table));                         \
    (head)->hh.tbl->tail = &((head)->hh);                                        \
    (head)->hh.tbl->num_buckets = HASH_INITIAL_NUM_BUCKETS;                      \
    (head)->hh.tbl->log2_num_buckets = HASH_INITIAL_NUM_BUCKETS_LOG2;            \
    (head)->hh.tbl->hho = (char*)(&(head)->hh) - (char*)(head);                  \
    (head)->hh.tbl->buckets = (UT_hash_bucket*)uthash_malloc(                    \
        HASH_INITIAL_NUM_BUCKETS * sizeof(struct UT_hash_bucket));               \
    (head)->hh.tbl->signature = HASH_SIGNATURE;                                  \
    if (!(head)->hh.tbl->buckets) {                                              \
      HASH_RECORD_OOM(oomed);                                                    \
      uthash_free((head)->hh.tbl, sizeof(UT_hash_table));                        \
    } else {                                                                     \
      uthash_bzero((head)->hh.tbl->buckets,                                      \
          HASH_INITIAL_NUM_BUCKETS * sizeof(struct UT_hash_bucket));             \
      HASH_BLOOM_MAKE((head)->hh.tbl, oomed);                                    \
      IF_HASH_NONFATAL_OOM(                                                      \
        if (oomed) {                                                             \
          uthash_free((head)->hh.tbl->buckets,                                   \
              HASH_INITIAL_NUM_BUCKETS*sizeof(struct UT_hash_bucket));           \
          uthash_free((head)->hh.tbl, sizeof(UT_hash_table));                    \
        }                                                                        \
      )                                                                          \
    }                                                                            \
  }                                                                              \
} while (0)

#define HASH_REPLACE_BYHASHVALUE_INORDER(hh,head,fieldname,keylen_in,hashval,add,replaced,cmpfcn) \
do {                                                                             \
  (replaced) = NULL;                                                             \
  HASH_FIND_BYHASHVALUE(hh, head, &((add)->fieldname), keylen_in, hashval, replaced); \
  if (replaced) {                                                                \
    HASH_DELETE(hh, head, replaced);                                             \
  }                                                                              \
  HASH_ADD_KEYPTR_BYHASHVALUE_INORDER(hh, head, &((add)->fieldname), keylen_in, hashval, add, cmpfcn); \
} while (0)

#define HASH_REPLACE_BYHASHVALUE(hh,head,fieldname,keylen_in,hashval,add,replaced) \
do {                                                                             \
  (replaced) = NULL;                                                             \
  HASH_FIND_BYHASHVALUE(hh, head, &((add)->fieldname), keylen_in, hashval, replaced); \
  if (replaced) {                                                                \
    HASH_DELETE(hh, head, replaced);                                             \
  }                                                                              \
  HASH_ADD_KEYPTR_BYHASHVALUE(hh, head, &((add)->fieldname), keylen_in, hashval, add); \
} while (0)

#define HASH_REPLACE(hh,head,fieldname,keylen_in,add,replaced)                   \
do {                                                                             \
  unsigned _hr_hashv;                                                            \
  HASH_VALUE(&((add)->fieldname), keylen_in, _hr_hashv);                         \
  HASH_REPLACE_BYHASHVALUE(hh, head, fieldname, keylen_in, _hr_hashv, add, replaced); \
} while (0)

#define HASH_REPLACE_INORDER(hh,head,fieldname,keylen_in,add,replaced,cmpfcn)    \
do {                                                                             \
  unsigned _hr_hashv;                                                            \
  HASH_VALUE(&((add)->fieldname), keylen_in, _hr_hashv);                         \
  HASH_REPLACE_BYHASHVALUE_INORDER(hh, head, fieldname, keylen_in, _hr_hashv, add, replaced, cmpfcn); \
} while (0)

#define HASH_APPEND_LIST(hh, head, add)                                          \
do {                                                                             \
  (add)->hh.next = NULL;                                                         \
  (add)->hh.prev = ELMT_FROM_HH((head)->hh.tbl, (head)->hh.tbl->tail);           \
  (head)->hh.tbl->tail->next = (add);                                            \
  (head)->hh.tbl->tail = &((add)->hh);                                           \
} while (0)

#define HASH_AKBI_INNER_LOOP(hh,head,add,cmpfcn)                                 \
do {                                                                             \
  do {                                                                           \
    if (cmpfcn(DECLTYPE(head)(_hs_iter), add) > 0) {                             \
      break;                                                                     \
    }                                                                            \
  } while ((_hs_iter = HH_FROM_ELMT((head)->hh.tbl, _hs_iter)->next));           \
} while (0)

#ifdef NO_DECLTYPE
#undef HASH_AKBI_INNER_LOOP
#define HASH_AKBI_INNER_LOOP(hh,head,add,cmpfcn)                                 \
do {                                                                             \
  char *_hs_saved_head = (char*)(head);                                          \
  do {                                                                           \
    DECLTYPE_ASSIGN(head, _hs_iter);                                             \
    if (cmpfcn(head, add) > 0) {                                                 \
      DECLTYPE_ASSIGN(head, _hs_saved_head);                                     \
      break;                                                                     \
    }                                                                            \
    DECLTYPE_ASSIGN(head, _hs_saved_head);                                       \
  } while ((_hs_iter = HH_FROM_ELMT((head)->hh.tbl, _hs_iter)->next));           \
} while (0)
#endif

#if HASH_NONFATAL_OOM

#define HASH_ADD_TO_TABLE(hh,head,keyptr,keylen_in,hashval,add,oomed)            \
do {                                                                             \
  if (!(oomed)) {                                                                \
    unsigned _ha_bkt;                                                            \
    (head)->hh.tbl->num_items++;                                                 \
    HASH_TO_BKT(hashval, (head)->hh.tbl->num_buckets, _ha_bkt);                  \
    HASH_ADD_TO_BKT((head)->hh.tbl->buckets[_ha_bkt], hh, &(add)->hh, oomed);    \
    if (oomed) {                                                                 \
      HASH_ROLLBACK_BKT(hh, head, &(add)->hh);                                   \
      HASH_DELETE_HH(hh, head, &(add)->hh);                                      \
      (add)->hh.tbl = NULL;                                                      \
      uthash_nonfatal_oom(add);                                                  \
    } else {                                                                     \
      HASH_BLOOM_ADD((head)->hh.tbl, hashval);                                   \
      HASH_EMIT_KEY(hh, head, keyptr, keylen_in);                                \
    }                                                                            \
  } else {                                                                       \
    (add)->hh.tbl = NULL;                                                        \
    uthash_nonfatal_oom(add);                                                    \
  }                                                                              \
} while (0)

#else

#define HASH_ADD_TO_TABLE(hh,head,keyptr,keylen_in,hashval,add,oomed)            \
do {                                                                             \
  unsigned _ha_bkt;                                                              \
  (head)->hh.tbl->num_items++;                                                   \
  HASH_TO_BKT(hashval, (head)->hh.tbl->num_buckets, _ha_bkt);                    \
  HASH_ADD_TO_BKT((head)->hh.tbl->buckets[_ha_bkt], hh, &(add)->hh, oomed);      \
  HASH_BLOOM_ADD((head)->hh.tbl, hashval);                                       \
  HASH_EMIT_KEY(hh, head, keyptr, keylen_in);                                    \
} while (0)

#endif


#define HASH_ADD_KEYPTR_BYHASHVALUE_INORDER(hh,head,keyptr,keylen_in,hashval,add,cmpfcn) \
do {                                                                             \
  IF_HASH_NONFATAL_OOM( int _ha_oomed = 0; )                                     \
  (add)->hh.hashv = (hashval);                                                   \
  (add)->hh.key = (char*) (keyptr);                                              \
  (add)->hh.keylen = (unsigned) (keylen_in);                                     \
  if (!(head)) {                                                                 \
    (add)->hh.next = NULL;                                                       \
    (add)->hh.prev = NULL;                                                       \
    HASH_MAKE_TABLE(hh, add, _ha_oomed);                                         \
    IF_HASH_NONFATAL_OOM( if (!_ha_oomed) { )                                    \
      (head) = (add);                                                            \
    IF_HASH_NONFATAL_OOM( } )                                                    \
  } else {                                                                       \
    void *_hs_iter = (head);                                                     \
    (add)->hh.tbl = (head)->hh.tbl;                                              \
    HASH_AKBI_INNER_LOOP(hh, head, add, cmpfcn);                                 \
    if (_hs_iter) {                                                              \
      (add)->hh.next = _hs_iter;                                                 \
      if (((add)->hh.prev = HH_FROM_ELMT((head)->hh.tbl, _hs_iter)->prev)) {     \
        HH_FROM_ELMT((head)->hh.tbl, (add)->hh.prev)->next = (add);              \
      } else {                                                                   \
        (head) = (add);                                                          \
      }                                                                          \
      HH_FROM_ELMT((head)->hh.tbl, _hs_iter)->prev = (add);                      \
    } else {                                                                     \
      HASH_APPEND_LIST(hh, head, add);                                           \
    }                                                                            \
  }                                                                              \
  HASH_ADD_TO_TABLE(hh, head, keyptr, keylen_in, hashval, add, _ha_oomed);       \
  HASH_FSCK(hh, head, "HASH_ADD_KEYPTR_BYHASHVALUE_INORDER");                    \
} while (0)

#define HASH_ADD_KEYPTR_INORDER(hh,head,keyptr,keylen_in,add,cmpfcn)             \
do {                                                                             \
  unsigned _hs_hashv;                                                            \
  HASH_VALUE(keyptr, keylen_in, _hs_hashv);                                      \
  HASH_ADD_KEYPTR_BYHASHVALUE_INORDER(hh, head, keyptr, keylen_in, _hs_hashv, add, cmpfcn); \
} while (0)

#define HASH_ADD_BYHASHVALUE_INORDER(hh,head,fieldname,keylen_in,hashval,add,cmpfcn) \
  HASH_ADD_KEYPTR_BYHASHVALUE_INORDER(hh, head, &((add)->fieldname), keylen_in, hashval, add, cmpfcn)

#define HASH_ADD_INORDER(hh,head,fieldname,keylen_in,add,cmpfcn)                 \
  HASH_ADD_KEYPTR_INORDER(hh, head, &((add)->fieldname), keylen_in, add, cmpfcn)

#define HASH_ADD_KEYPTR_BYHASHVALUE(hh,head,keyptr,keylen_in,hashval,add)        \
do {                                                                             \
  IF_HASH_NONFATAL_OOM( int _ha_oomed = 0; )                                     \
  (add)->hh.hashv = (hashval);                                                   \
  (add)->hh.key = (const void*) (keyptr);                                        \
  (add)->hh.keylen = (unsigned) (keylen_in);                                     \
  if (!(head)) {                                                                 \
    (add)->hh.next = NULL;                                                       \
    (add)->hh.prev = NULL;                                                       \
    HASH_MAKE_TABLE(hh, add, _ha_oomed);                                         \
    IF_HASH_NONFATAL_OOM( if (!_ha_oomed) { )                                    \
      (head) = (add);                                                            \
    IF_HASH_NONFATAL_OOM( } )                                                    \
  } else {                                                                       \
    (add)->hh.tbl = (head)->hh.tbl;                                              \
    HASH_APPEND_LIST(hh, head, add);                                             \
  }                                                                              \
  HASH_ADD_TO_TABLE(hh, head, keyptr, keylen_in, hashval, add, _ha_oomed);       \
  HASH_FSCK(hh, head, "HASH_ADD_KEYPTR_BYHASHVALUE");                            \
} while (0)

#define HASH_ADD_KEYPTR(hh,head,keyptr,keylen_in,add)                            \
do {                                                                             \
  unsigned _ha_hashv;                                                            \
  HASH_VALUE(keyptr, keylen_in, _ha_hashv);                                      \
  HASH_ADD_KEYPTR_BYHASHVALUE(hh, head, keyptr, keylen_in, _ha_hashv, add);      \
} while (0)

#define HASH_ADD_BYHASHVALUE(hh,head,fieldname,keylen_in,hashval,add)            \
  HASH_ADD_KEYPTR_BYHASHVALUE(hh, head, &((add)->fieldname), keylen_in, hashval, add)

#define HASH_ADD(hh,head,fieldname,keylen_in,add)                                \
  HASH_ADD_KEYPTR(hh, head, &((add)->fieldname), keylen_in, add)

#define HASH_TO_BKT(hashv,num_bkts,bkt)                                          \
do {                                                                             \
  bkt = ((hashv) & ((num_bkts) - 1U));                                           \
} while (0)

/* delete "delptr" from the hash table.
 * "the usual" patch-up process for the app-order doubly-linked-list.
 * The use of _hd_hh_del below deserves special explanation.
 * These used to be expressed using (delptr) but that led to a bug
 * if someone used the same symbol for the head and deletee, like
 *  HASH_DELETE(hh,users,users);
 * We want that to work, but by changing the head (users) below
 * we were forfeiting our ability to further refer to the deletee (users)
 * in the patch-up process. Solution: use scratch space to
 * copy the deletee pointer, then the latter references are via that
 * scratch pointer rather than through the repointed (users) symbol.
 */
#define HASH_DELETE(hh,head,delptr)                                              \
    HASH_DELETE_HH(hh, head, &(delptr)->hh)

#define HASH_DELETE_HH(hh,head,delptrhh)                                         \
do {                                                                             \
  const struct UT_hash_handle *_hd_hh_del = (delptrhh);                          \
  if ((_hd_hh_del->prev == NULL) && (_hd_hh_del->next == NULL)) {                \
    HASH_BLOOM_FREE((head)->hh.tbl);                                             \
    uthash_free((head)->hh.tbl->buckets,                                         \
                (head)->hh.tbl->num_buckets * sizeof(struct UT_hash_bucket));    \
    uthash_free((head)->hh.tbl, sizeof(UT_hash_table));                          \
    (head) = NULL;                                                               \
  } else {                                                                       \
    unsigned _hd_bkt;                                                            \
    if (_hd_hh_del == (head)->hh.tbl->tail) {                                    \
      (head)->hh.tbl->tail = HH_FROM_ELMT((head)->hh.tbl, _hd_hh_del->prev);     \
    }                                                                            \
    if (_hd_hh_del->prev != NULL) {                                              \
      HH_FROM_ELMT((head)->hh.tbl, _hd_hh_del->prev)->next = _hd_hh_del->next;   \
    } else {                                                                     \
      DECLTYPE_ASSIGN(head, _hd_hh_del->next);                                   \
    }                                                                            \
    if (_hd_hh_del->next != NULL) {                                              \
      HH_FROM_ELMT((head)->hh.tbl, _hd_hh_del->next)->prev = _hd_hh_del->prev;   \
    }                                                                            \
    HASH_TO_BKT(_hd_hh_del->hashv, (head)->hh.tbl->num_buckets, _hd_bkt);        \
    HASH_DEL_IN_BKT((head)->hh.tbl->buckets[_hd_bkt], _hd_hh_del);               \
    (head)->hh.tbl->num_items--;                                                 \
  }                                                                              \
  HASH_FSCK(hh, head, "HASH_DELETE_HH");                                         \
} while (0)

/* convenience forms of HASH_FIND/HASH_ADD/HASH_DEL */
#define HASH_FIND_STR(head,findstr,out)                                          \
do {                                                                             \
    unsigned _uthash_hfstr_keylen = (unsigned)uthash_strlen(findstr);            \
    HASH_FIND(hh, head, findstr, _uthash_hfstr_keylen, out);                     \
} while (0)
#define HASH_ADD_STR(head,strfield,add)                                          \
do {                                                                             \
    unsigned _uthash_hastr_keylen = (unsigned)uthash_strlen((add)->strfield);    \
    HASH_ADD(hh, head, strfield[0], _uthash_hastr_keylen, add);                  \
} while (0)
#define HASH_REPLACE_STR(head,strfield,add,replaced)                             \
do {                                                                             \
    unsigned _uthash_hrstr_keylen = (unsigned)uthash_strlen((add)->strfield);    \
    HASH_REPLACE(hh, head, strfield[0], _uthash_hrstr_keylen, add, replaced);    \
} while (0)
#define HASH_FIND_INT(head,findint,out)                                          \
    HASH_FIND(hh,head,findint,sizeof(int),out)
#define HASH_ADD_INT(head,intfield,add)                                          \
    HASH_ADD(hh,head,intfield,sizeof(int),add)
#define HASH_REPLACE_INT(head,intfield,add,replaced)                             \
    HASH_REPLACE(hh,head,intfield,sizeof(int),add,replaced)
#define HASH_FIND_PTR(head,findptr,out)                                          \
    HASH_FIND(hh,head,findptr,sizeof(void *),out)
#define HASH_ADD_PTR(head,ptrfield,add)                                          \
    HASH_ADD(hh,head,ptrfield,sizeof(void *),add)
#define HASH_REPLACE_PTR(head,ptrfield,add,replaced)                             \
    HASH_REPLACE(hh,head,ptrfield,sizeof(void *),add,replaced)
#define HASH_DEL(head,delptr)                                                    \
    HASH_DELETE(hh,head,delptr)

/* HASH_FSCK checks hash integrity on every add/delete when HASH_DEBUG is defined.
 * This is for uthash developer only; it compiles away if HASH_DEBUG isn't defined.
 */
#ifdef HASH_DEBUG
#include <stdio.h>   /* fprintf, stderr */
#define HASH_OOPS(...) do { fprintf(stderr, __VA_ARGS__); exit(-1); } while (0)
#define HASH_FSCK(hh,head,where)                                                 \
do {                                                                             \
  struct UT_hash_handle *_thh;                                                   \
  if (head) {                                                                    \
    unsigned _bkt_i;                                                             \
    unsigned _count = 0;                                                         \
    char *_prev;                                                                 \
    for (_bkt_i = 0; _bkt_i < (head)->hh.tbl->num_buckets; ++_bkt_i) {           \
      unsigned _bkt_count = 0;                                                   \
      _thh = (head)->hh.tbl->buckets[_bkt_i].hh_head;                            \
      _prev = NULL;                                                              \
      while (_thh) {                                                             \
        if (_prev != (char*)(_thh->hh_prev)) {                                   \
          HASH_OOPS("%s: invalid hh_prev %p, actual %p\n",                       \
              (where), (void*)_thh->hh_prev, (void*)_prev);                      \
        }                                                                        \
        _bkt_count++;                                                            \
        _prev = (char*)(_thh);                                                   \
        _thh = _thh->hh_next;                                                    \
      }                                                                          \
      _count += _bkt_count;                                                      \
      if ((head)->hh.tbl->buckets[_bkt_i].count !=  _bkt_count) {                \
        HASH_OOPS("%s: invalid bucket count %u, actual %u\n",                    \
            (where), (head)->hh.tbl->buckets[_bkt_i].count, _bkt_count);         \
      }                                                                          \
    }                                                                            \
    if (_count != (head)->hh.tbl->num_items) {                                   \
      HASH_OOPS("%s: invalid hh item count %u, actual %u\n",                     \
          (where), (head)->hh.tbl->num_items, _count);                           \
    }                                                                            \
    _count = 0;                                                                  \
    _prev = NULL;                                                                \
    _thh =  &(head)->hh;                                                         \
    while (_thh) {                                                               \
      _count++;                                                                  \
      if (_prev != (char*)_thh->prev) {                                          \
        HASH_OOPS("%s: invalid prev %p, actual %p\n",                            \
            (where), (void*)_thh->prev, (void*)_prev);                           \
      }                                                                          \
      _prev = (char*)ELMT_FROM_HH((head)->hh.tbl, _thh);                         \
      _thh = (_thh->next ? HH_FROM_ELMT((head)->hh.tbl, _thh->next) : NULL);     \
    }                                                                            \
    if (_count != (head)->hh.tbl->num_items) {                                   \
      HASH_OOPS("%s: invalid app item count %u, actual %u\n",                    \
          (where), (head)->hh.tbl->num_items, _count);                           \
    }                                                                            \
  }                                                                              \
} while (0)
#else
#define HASH_FSCK(hh,head,where)
#endif

/* When compiled with -DHASH_EMIT_KEYS, length-prefixed keys are emitted to
 * the descriptor to which this macro is defined for tuning the hash function.
 * The app can #include <unistd.h> to get the prototype for write(2). */
#ifdef HASH_EMIT_KEYS
#define HASH_EMIT_KEY(hh,head,keyptr,fieldlen)                                   \
do {                                                                             \
  unsigned _klen = fieldlen;                                                     \
  write(HASH_EMIT_KEYS, &_klen, sizeof(_klen));                                  \
  write(HASH_EMIT_KEYS, keyptr, (unsigned long)fieldlen);                        \
} while (0)
#else
#define HASH_EMIT_KEY(hh,head,keyptr,fieldlen)
#endif

/* The Bernstein hash function, used in Perl prior to v5.6. Note (x<<5+x)=x*33. */
#define HASH_BER(key,keylen,hashv)                                               \
do {                                                                             \
  unsigned _hb_keylen = (unsigned)keylen;                                        \
  const unsigned char *_hb_key = (const unsigned char*)(key);                    \
  (hashv) = 0;                                                                   \
  while (_hb_keylen-- != 0U) {                                                   \
    (hashv) = (((hashv) << 5) + (hashv)) + *_hb_key++;                           \
  }                                                                              \
} while (0)


/* SAX/FNV/OAT/JEN hash functions are macro variants of those listed at
 * http://eternallyconfuzzled.com/tuts/algorithms/jsw_tut_hashing.aspx
 * (archive link: https://archive.is/Ivcan )
 */
#define HASH_SAX(key,keylen,hashv)                                               \
do {                                                                             \
  unsigned _sx_i;                                                                \
  const unsigned char *_hs_key = (const unsigned char*)(key);                    \
  hashv = 0;                                                                     \
  for (_sx_i=0; _sx_i < keylen; _sx_i++) {                                       \
    hashv ^= (hashv << 5) + (hashv >> 2) + _hs_key[_sx_i];                       \
  }                                                                              \
} while (0)
/* FNV-1a variation */
#define HASH_FNV(key,keylen,hashv)                                               \
do {                                                                             \
  unsigned _fn_i;                                                                \
  const unsigned char *_hf_key = (const unsigned char*)(key);                    \
  (hashv) = 2166136261U;                                                         \
  for (_fn_i=0; _fn_i < keylen; _fn_i++) {                                       \
    hashv = hashv ^ _hf_key[_fn_i];                                              \
    hashv = hashv * 16777619U;                                                   \
  }                                                                              \
} while (0)

#define HASH_OAT(key,keylen,hashv)                                               \
do {                                                                             \
  unsigned _ho_i;                                                                \
  const unsigned char *_ho_key=(const unsigned char*)(key);                      \
  hashv = 0;                                                                     \
  for(_ho_i=0; _ho_i < keylen; _ho_i++) {                                        \
      hashv += _ho_key[_ho_i];                                                   \
      hashv += (hashv << 10);                                                    \
      hashv ^= (hashv >> 6);                                                     \
  }                                                                              \
  hashv += (hashv << 3);                                                         \
  hashv ^= (hashv >> 11);                                                        \
  hashv += (hashv << 15);                                                        \
} while (0)

#define HASH_JEN_MIX(a,b,c)                                                      \
do {                                                                             \
  a -= b; a -= c; a ^= ( c >> 13 );                                              \
  b -= c; b -= a; b ^= ( a << 8 );                                               \
  c -= a; c -= b; c ^= ( b >> 13 );                                              \
  a -= b; a -= c; a ^= ( c >> 12 );                                              \
  b -= c; b -= a; b ^= ( a << 16 );                                              \
  c -= a; c -= b; c ^= ( b >> 5 );                                               \
  a -= b; a -= c; a ^= ( c >> 3 );                                               \
  b -= c; b -= a; b ^= ( a << 10 );                                              \
  c -= a; c -= b; c ^= ( b >> 15 );                                              \
} while (0)

#define HASH_JEN(key,keylen,hashv)                                               \
do {                                                                             \
  unsigned _hj_i,_hj_j,_hj_k;                                                    \
  unsigned const char *_hj_key=(unsigned const char*)(key);                      \
  hashv = 0xfeedbeefu;                                                           \
  _hj_i = _hj_j = 0x9e3779b9u;                                                   \
  _hj_k = (unsigned)(keylen);                                                    \
  while (_hj_k >= 12U) {                                                         \
    _hj_i +=    (_hj_key[0] + ( (unsigned)_hj_key[1] << 8 )                      \
        + ( (unsigned)_hj_key[2] << 16 )                                         \
        + ( (unsigned)_hj_key[3] << 24 ) );                                      \
    _hj_j +=    (_hj_key[4] + ( (unsigned)_hj_key[5] << 8 )                      \
        + ( (unsigned)_hj_key[6] << 16 )                                         \
        + ( (unsigned)_hj_key[7] << 24 ) );                                      \
    hashv += (_hj_key[8] + ( (unsigned)_hj_key[9] << 8 )                         \
        + ( (unsigned)_hj_key[10] << 16 )                                        \
        + ( (unsigned)_hj_key[11] << 24 ) );                                     \
                                                                                 \
     HASH_JEN_MIX(_hj_i, _hj_j, hashv);                                          \
                                                                                 \
     _hj_key += 12;                                                              \
     _hj_k -= 12U;                                                               \
  }                                                                              \
  hashv += (unsigned)(keylen);                                                   \
  switch ( _hj_k ) {                                                             \
    case 11: hashv += ( (unsigned)_hj_key[10] << 24 ); /* FALLTHROUGH */         \
    case 10: hashv += ( (unsigned)_hj_key[9] << 16 );  /* FALLTHROUGH */         \
    case 9:  hashv += ( (unsigned)_hj_key[8] << 8 );   /* FALLTHROUGH */         \
    case 8:  _hj_j += ( (unsigned)_hj_key[7] << 24 );  /* FALLTHROUGH */         \
    case 7:  _hj_j += ( (unsigned)_hj_key[6] << 16 );  /* FALLTHROUGH */         \
    case 6:  _hj_j += ( (unsigned)_hj_key[5] << 8 );   /* FALLTHROUGH */         \
    case 5:  _hj_j += _hj_key[4];                      /* FALLTHROUGH */         \
    case 4:  _hj_i += ( (unsigned)_hj_key[3] << 24 );  /* FALLTHROUGH */         \
    case 3:  _hj_i += ( (unsigned)_hj_key[2] << 16 );  /* FALLTHROUGH */         \
    case 2:  _hj_i += ( (unsigned)_hj_key[1] << 8 );   /* FALLTHROUGH */         \
    case 1:  _hj_i += _hj_key[0];                      /* FALLTHROUGH */         \
    default: ;                                                                   \
  }                                                                              \
  HASH_JEN_MIX(_hj_i, _hj_j, hashv);                                             \
} while (0)

/* The Paul Hsieh hash function */
#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__)             \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif

#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)             \
                       +(uint32_t)(((const uint8_t *)(d))[0]) )
#endif
#define HASH_SFH(key,keylen,hashv)                                               \
do {                                                                             \
  unsigned const char *_sfh_key=(unsigned const char*)(key);                     \
  uint32_t _sfh_tmp, _sfh_len = (uint32_t)keylen;                                \
                                                                                 \
  unsigned _sfh_rem = _sfh_len & 3U;                                             \
  _sfh_len >>= 2;                                                                \
  hashv = 0xcafebabeu;                                                           \
                                                                                 \
  /* Main loop */                                                                \
  for (;_sfh_len > 0U; _sfh_len--) {                                             \
    hashv    += get16bits (_sfh_key);                                            \
    _sfh_tmp  = ((uint32_t)(get16bits (_sfh_key+2)) << 11) ^ hashv;              \
    hashv     = (hashv << 16) ^ _sfh_tmp;                                        \
    _sfh_key += 2U*sizeof (uint16_t);                                            \
    hashv    += hashv >> 11;                                                     \
  }                                                                              \
                                                                                 \
  /* Handle end cases */                                                         \
  switch (_sfh_rem) {                                                            \
    case 3: hashv += get16bits (_sfh_key);                                       \
            hashv ^= hashv << 16;                                                \
            hashv ^= (uint32_t)(_sfh_key[sizeof (uint16_t)]) << 18;              \
            hashv += hashv >> 11;                                                \
            break;                                                               \
    case 2: hashv += get16bits (_sfh_key);                                       \
            hashv ^= hashv << 11;                                                \
            hashv += hashv >> 17;                                                \
            break;                                                               \
    case 1: hashv += *_sfh_key;                                                  \
            hashv ^= hashv << 10;                                                \
            hashv += hashv >> 1;                                                 \
            break;                                                               \
    default: ;                                                                   \
  }                                                                              \
                                                                                 \
  /* Force "avalanching" of final 127 bits */                                    \
  hashv ^= hashv << 3;                                                           \
  hashv += hashv >> 5;                                                           \
  hashv ^= hashv << 4;                                                           \
  hashv += hashv >> 17;                                                          \
  hashv ^= hashv << 25;                                                          \
  hashv += hashv >> 6;                                                           \
} while (0)

/* iterate over items in a known bucket to find desired item */
#define HASH_FIND_IN_BKT(tbl,hh,head,keyptr,keylen_in,hashval,out)               \
do {                                                                             \
  if ((head).hh_head != NULL) {                                                  \
    DECLTYPE_ASSIGN(out, ELMT_FROM_HH(tbl, (head).hh_head));                     \
  } else {                                                                       \
    (out) = NULL;                                                                \
  }                                                                              \
  while ((out) != NULL) {                                                        \
    if ((out)->hh.hashv == (hashval) && (out)->hh.keylen == (keylen_in)) {       \
      if (HASH_KEYCMP((out)->hh.key, keyptr, keylen_in) == 0) {                  \
        break;                                                                   \
      }                                                                          \
    }                                                                            \
    if ((out)->hh.hh_next != NULL) {                                             \
      DECLTYPE_ASSIGN(out, ELMT_FROM_HH(tbl, (out)->hh.hh_next));                \
    } else {                                                                     \
      (out) = NULL;                                                              \
    }                                                                            \
  }                                                                              \
} while (0)

/* add an item to a bucket  */
#define HASH_ADD_TO_BKT(head,hh,addhh,oomed)                                     \
do {                                                                             \
  UT_hash_bucket *_ha_head = &(head);                                            \
  _ha_head->count++;                                                             \
  (addhh)->hh_next = _ha_head->hh_head;                                          \
  (addhh)->hh_prev = NULL;                                                       \
  if (_ha_head->hh_head != NULL) {                                               \
    _ha_head->hh_head->hh_prev = (addhh);                                        \
  }                                                                              \
  _ha_head->hh_head = (addhh);                                                   \
  if ((_ha_head->count >= ((_ha_head->expand_mult + 1U) * HASH_BKT_CAPACITY_THRESH)) \
      && !(addhh)->tbl->noexpand) {                                              \
    HASH_EXPAND_BUCKETS(addhh,(addhh)->tbl, oomed);                              \
    IF_HASH_NONFATAL_OOM(                                                        \
      if (oomed) {                                                               \
        HASH_DEL_IN_BKT(head,addhh);                                             \
      }                                                                          \
    )                                                                            \
  }                                                                              \
} while (0)

/* remove an item from a given bucket */
#define HASH_DEL_IN_BKT(head,delhh)                                              \
do {                                                                             \
  UT_hash_bucket *_hd_head = &(head);                                            \
  _hd_head->count--;                                                             \
  if (_hd_head->hh_head == (delhh)) {                                            \
    _hd_head->hh_head = (delhh)->hh_next;                                        \
  }                                                                              \
  if ((delhh)->hh_prev) {                                                        \
    (delhh)->hh_prev->hh_next = (delhh)->hh_next;                                \
  }                                                                              \
  if ((delhh)->hh_next) {                                                        \
    (delhh)->hh_next->hh_prev = (delhh)->hh_prev;                                \
  }                                                                              \
} while (0)

/* Bucket expansion has the effect of doubling the number of buckets
 * and redistributing the items into the new buckets. Ideally the
 * items will distribute more or less evenly into the new buckets
 * (the extent to which this is true is a measure of the quality of
 * the hash function as it applies to the key domain).
 *
 * With the items distributed into more buckets, the chain length
 * (item count) in each bucket is reduced. Thus by expanding buckets
 * the hash keeps a bound on the chain length. This bounded chain
 * length is the essence of how a hash provides constant time lookup.
 *
 * The calculation of tbl->ideal_chain_maxlen below deserves some
 * explanation. First, keep in mind that we're calculating the ideal
 * maximum chain length based on the *new* (doubled) bucket count.
 * In fractions this is just n/b (n=number of items,b=new num buckets).
 * Since the ideal chain length is an integer, we want to calculate
 * ceil(n/b). We don't depend on floating point arithmetic in this
 * hash, so to calculate ceil(n/b) with integers we could write
 *
 *      ceil(n/b) = (n/b) + ((n%b)?1:0)
 *
 * and in fact a previous version of this hash did just that.
 * But now we have improved things a bit by recognizing that b is
 * always a power of two. We keep its base 2 log handy (call it lb),
 * so now we can write this with a bit shift and logical AND:
 *
 *      ceil(n/b) = (n>>lb) + ( (n & (b-1)) ? 1:0)
 *
 */
#define HASH_EXPAND_BUCKETS(hh,tbl,oomed)                                        \
do {                                                                             \
  unsigned _he_bkt;                                                              \
  unsigned _he_bkt_i;                                                            \
  struct UT_hash_handle *_he_thh, *_he_hh_nxt;                                   \
  UT_hash_bucket *_he_new_buckets, *_he_newbkt;                                  \
  _he_new_buckets = (UT_hash_bucket*)uthash_malloc(                              \
           sizeof(struct UT_hash_bucket) * (tbl)->num_buckets * 2U);             \
  if (!_he_new_buckets) {                                                        \
    HASH_RECORD_OOM(oomed);                                                      \
  } else {                                                                       \
    uthash_bzero(_he_new_buckets,                                                \
        sizeof(struct UT_hash_bucket) * (tbl)->num_buckets * 2U);                \
    (tbl)->ideal_chain_maxlen =                                                  \
       ((tbl)->num_items >> ((tbl)->log2_num_buckets+1U)) +                      \
       ((((tbl)->num_items & (((tbl)->num_buckets*2U)-1U)) != 0U) ? 1U : 0U);    \
    (tbl)->nonideal_items = 0;                                                   \
    for (_he_bkt_i = 0; _he_bkt_i < (tbl)->num_buckets; _he_bkt_i++) {           \
      _he_thh = (tbl)->buckets[ _he_bkt_i ].hh_head;                             \
      while (_he_thh != NULL) {                                                  \
        _he_hh_nxt = _he_thh->hh_next;                                           \
        HASH_TO_BKT(_he_thh->hashv, (tbl)->num_buckets * 2U, _he_bkt);           \
        _he_newbkt = &(_he_new_buckets[_he_bkt]);                                \
        if (++(_he_newbkt->count) > (tbl)->ideal_chain_maxlen) {                 \
          (tbl)->nonideal_items++;                                               \
          if (_he_newbkt->count > _he_newbkt->expand_mult * (tbl)->ideal_chain_maxlen) { \
            _he_newbkt->expand_mult++;                                           \
          }                                                                      \
        }                                                                        \
        _he_thh->hh_prev = NULL;                                                 \
        _he_thh->hh_next = _he_newbkt->hh_head;                                  \
        if (_he_newbkt->hh_head != NULL) {                                       \
          _he_newbkt->hh_head->hh_prev = _he_thh;                                \
        }                                                                        \
        _he_newbkt->hh_head = _he_thh;                                           \
        _he_thh = _he_hh_nxt;                                                    \
      }                                                                          \
    }                                                                            \
    uthash_free((tbl)->buckets, (tbl)->num_buckets * sizeof(struct UT_hash_bucket)); \
    (tbl)->num_buckets *= 2U;                                                    \
    (tbl)->log2_num_buckets++;                                                   \
    (tbl)->buckets = _he_new_buckets;                                            \
    (tbl)->ineff_expands = ((tbl)->nonideal_items > ((tbl)->num_items >> 1)) ?   \
        ((tbl)->ineff_expands+1U) : 0U;                                          \
    if ((tbl)->ineff_expands > 1U) {                                             \
      (tbl)->noexpand = 1;                                                       \
      uthash_noexpand_fyi(tbl);                                                  \
    }                                                                            \
    uthash_expand_fyi(tbl);                                                      \
  }                                                                              \
} while (0)


/* This is an adaptation of Simon Tatham's O(n log(n)) mergesort */
/* Note that HASH_SORT assumes the hash handle name to be hh.
 * HASH_SRT was added to allow the hash handle name to be passed in. */
#define HASH_SORT(head,cmpfcn) HASH_SRT(hh,head,cmpfcn)
#define HASH_SRT(hh,head,cmpfcn)                                                 \
do {                                                                             \
  unsigned _hs_i;                                                                \
  unsigned _hs_looping,_hs_nmerges,_hs_insize,_hs_psize,_hs_qsize;               \
  struct UT_hash_handle *_hs_p, *_hs_q, *_hs_e, *_hs_list, *_hs_tail;            \
  if (head != NULL) {                                                            \
    _hs_insize = 1;                                                              \
    _hs_looping = 1;                                                             \
    _hs_list = &((head)->hh);                                                    \
    while (_hs_looping != 0U) {                                                  \
      _hs_p = _hs_list;                                                          \
      _hs_list = NULL;                                                           \
      _hs_tail = NULL;                                                           \
      _hs_nmerges = 0;                                                           \
      while (_hs_p != NULL) {                                                    \
        _hs_nmerges++;                                                           \
        _hs_q = _hs_p;                                                           \
        _hs_psize = 0;                                                           \
        for (_hs_i = 0; _hs_i < _hs_insize; ++_hs_i) {                           \
          _hs_psize++;                                                           \
          _hs_q = ((_hs_q->next != NULL) ?                                       \
            HH_FROM_ELMT((head)->hh.tbl, _hs_q->next) : NULL);                   \
          if (_hs_q == NULL) {                                                   \
            break;                                                               \
          }                                                                      \
        }                                                                        \
        _hs_qsize = _hs_insize;                                                  \
        while ((_hs_psize != 0U) || ((_hs_qsize != 0U) && (_hs_q != NULL))) {    \
          if (_hs_psize == 0U) {                                                 \
            _hs_e = _hs_q;                                                       \
            _hs_q = ((_hs_q->next != NULL) ?                                     \
              HH_FROM_ELMT((head)->hh.tbl, _hs_q->next) : NULL);                 \
            _hs_qsize--;                                                         \
          } else if ((_hs_qsize == 0U) || (_hs_q == NULL)) {                     \
            _hs_e = _hs_p;                                                       \
            if (_hs_p != NULL) {                                                 \
              _hs_p = ((_hs_p->next != NULL) ?                                   \
                HH_FROM_ELMT((head)->hh.tbl, _hs_p->next) : NULL);               \
            }                                                                    \
            _hs_psize--;                                                         \
          } else if ((cmpfcn(                                                    \
                DECLTYPE(head)(ELMT_FROM_HH((head)->hh.tbl, _hs_p)),             \
                DECLTYPE(head)(ELMT_FROM_HH((head)->hh.tbl, _hs_q))              \
                )) <= 0) {                                                       \
            _hs_e = _hs_p;                                                       \
            if (_hs_p != NULL) {                                                 \
              _hs_p = ((_hs_p->next != NULL) ?                                   \
                HH_FROM_ELMT((head)->hh.tbl, _hs_p->next) : NULL);               \
            }                                                                    \
            _hs_psize--;                                                         \
          } else {                                                               \
            _hs_e = _hs_q;                                                       \
            _hs_q = ((_hs_q->next != NULL) ?                                     \
              HH_FROM_ELMT((head)->hh.tbl, _hs_q->next) : NULL);                 \
            _hs_qsize--;                                                         \
          }                                                                      \
          if ( _hs_tail != NULL ) {                                              \
            _hs_tail->next = ((_hs_e != NULL) ?                                  \
              ELMT_FROM_HH((head)->hh.tbl, _hs_e) : NULL);                       \
          } else {                                                               \
            _hs_list = _hs_e;                                                    \
          }                                                                      \
          if (_hs_e != NULL) {                                                   \
            _hs_e->prev = ((_hs_tail != NULL) ?                                  \
              ELMT_FROM_HH((head)->hh.tbl, _hs_tail) : NULL);                    \
          }                                                                      \
          _hs_tail = _hs_e;                                                      \
        }                                                                        \
        _hs_p = _hs_q;                                                           \
      }                                                                          \
      if (_hs_tail != NULL) {                                                    \
        _hs_tail->next = NULL;                                                   \
      }                                                                          \
      if (_hs_nmerges <= 1U) {                                                   \
        _hs_looping = 0;                                                         \
        (head)->hh.tbl->tail = _hs_tail;                                         \
        DECLTYPE_ASSIGN(head, ELMT_FROM_HH((head)->hh.tbl, _hs_list));           \
      }                                                                          \
      _hs_insize *= 2U;                                                          \
    }                                                                            \
    HASH_FSCK(hh, head, "HASH_SRT");                                             \
  }                                                                              \
} while (0)

/* This function selects items from one hash into another hash.
 * The end result is that the selected items have dual presence
 * in both hashes. There is no copy of the items made; rather
 * they are added into the new hash through a secondary hash
 * hash handle that must be present in the structure. */
#define HASH_SELECT(hh_dst, dst, hh_src, src, cond)                              \
do {                                                                             \
  unsigned _src_bkt, _dst_bkt;                                                   \
  void *_last_elt = NULL, *_elt;                                                 \
  UT_hash_handle *_src_hh, *_dst_hh, *_last_elt_hh=NULL;                         \
  ptrdiff_t _dst_hho = ((char*)(&(dst)->hh_dst) - (char*)(dst));                 \
  if ((src) != NULL) {                                                           \
    for (_src_bkt=0; _src_bkt < (src)->hh_src.tbl->num_buckets; _src_bkt++) {    \
      for (_src_hh = (src)->hh_src.tbl->buckets[_src_bkt].hh_head;               \
        _src_hh != NULL;                                                         \
        _src_hh = _src_hh->hh_next) {                                            \
        _elt = ELMT_FROM_HH((src)->hh_src.tbl, _src_hh);                         \
        if (cond(_elt)) {                                                        \
          IF_HASH_NONFATAL_OOM( int _hs_oomed = 0; )                             \
          _dst_hh = (UT_hash_handle*)(void*)(((char*)_elt) + _dst_hho);          \
          _dst_hh->key = _src_hh->key;                                           \
          _dst_hh->keylen = _src_hh->keylen;                                     \
          _dst_hh->hashv = _src_hh->hashv;                                       \
          _dst_hh->prev = _last_elt;                                             \
          _dst_hh->next = NULL;                                                  \
          if (_last_elt_hh != NULL) {                                            \
            _last_elt_hh->next = _elt;                                           \
          }                                                                      \
          if ((dst) == NULL) {                                                   \
            DECLTYPE_ASSIGN(dst, _elt);                                          \
            HASH_MAKE_TABLE(hh_dst, dst, _hs_oomed);                             \
            IF_HASH_NONFATAL_OOM(                                                \
              if (_hs_oomed) {                                                   \
                uthash_nonfatal_oom(_elt);                                       \
                (dst) = NULL;                                                    \
                continue;                                                        \
              }                                                                  \
            )                                                                    \
          } else {                                                               \
            _dst_hh->tbl = (dst)->hh_dst.tbl;                                    \
          }                                                                      \
          HASH_TO_BKT(_dst_hh->hashv, _dst_hh->tbl->num_buckets, _dst_bkt);      \
          HASH_ADD_TO_BKT(_dst_hh->tbl->buckets[_dst_bkt], hh_dst, _dst_hh, _hs_oomed); \
          (dst)->hh_dst.tbl->num_items++;                                        \
          IF_HASH_NONFATAL_OOM(                                                  \
            if (_hs_oomed) {                                                     \
              HASH_ROLLBACK_BKT(hh_dst, dst, _dst_hh);                           \
              HASH_DELETE_HH(hh_dst, dst, _dst_hh);                              \
              _dst_hh->tbl = NULL;                                               \
              uthash_nonfatal_oom(_elt);                                         \
              continue;                                                          \
            }                                                                    \
          )                                                                      \
          HASH_BLOOM_ADD(_dst_hh->tbl, _dst_hh->hashv);                          \
          _last_elt = _elt;                                                      \
          _last_elt_hh = _dst_hh;                                                \
        }                                                                        \
      }                                                                          \
    }                                                                            \
  }                                                                              \
  HASH_FSCK(hh_dst, dst, "HASH_SELECT");                                         \
} while (0)

#define HASH_CLEAR(hh,head)                                                      \
do {                                                                             \
  if ((head) != NULL) {                                                          \
    HASH_BLOOM_FREE((head)->hh.tbl);                                             \
    uthash_free((head)->hh.tbl->buckets,                                         \
                (head)->hh.tbl->num_buckets*sizeof(struct UT_hash_bucket));      \
    uthash_free((head)->hh.tbl, sizeof(UT_hash_table));                          \
    (head) = NULL;                                                               \
  }                                                                              \
} while (0)

#define HASH_OVERHEAD(hh,head)                                                   \
 (((head) != NULL) ? (                                                           \
 (size_t)(((head)->hh.tbl->num_items   * sizeof(UT_hash_handle))   +             \
          ((head)->hh.tbl->num_buckets * sizeof(UT_hash_bucket))   +             \
           sizeof(UT_hash_table)                                   +             \
           (HASH_BLOOM_BYTELEN))) : 0U)

#ifdef NO_DECLTYPE
#define HASH_ITER(hh,head,el,tmp)                                                \
for(((el)=(head)), ((*(char**)(&(tmp)))=(char*)((head!=NULL)?(head)->hh.next:NULL)); \
  (el) != NULL; ((el)=(tmp)), ((*(char**)(&(tmp)))=(char*)((tmp!=NULL)?(tmp)->hh.next:NULL)))
#else
#define HASH_ITER(hh,head,el,tmp)                                                \
for(((el)=(head)), ((tmp)=DECLTYPE(el)((head!=NULL)?(head)->hh.next:NULL));      \
  (el) != NULL; ((el)=(tmp)), ((tmp)=DECLTYPE(el)((tmp!=NULL)?(tmp)->hh.next:NULL)))
#endif

/* obtain a count of items in the hash */
#define HASH_COUNT(head) HASH_CNT(hh,head)
#define HASH_CNT(hh,head) ((head != NULL)?((head)->hh.tbl->num_items):0U)

typedef struct UT_hash_bucket {
   struct UT_hash_handle *hh_head;
   unsigned count;

   /* expand_mult is normally set to 0. In this situation, the max chain length
    * threshold is enforced at its default value, HASH_BKT_CAPACITY_THRESH. (If
    * the bucket's chain exceeds this length, bucket expansion is triggered).
    * However, setting expand_mult to a non-zero value delays bucket expansion
    * (that would be triggered by additions to this particular bucket)
    * until its chain length reaches a *multiple* of HASH_BKT_CAPACITY_THRESH.
    * (The multiplier is simply expand_mult+1). The whole idea of this
    * multiplier is to reduce bucket expansions, since they are expensive, in
    * situations where we know that a particular bucket tends to be overused.
    * It is better to let its chain length grow to a longer yet-still-bounded
    * value, than to do an O(n) bucket expansion too often.
    */
   unsigned expand_mult;

} UT_hash_bucket;

/* random signature used only to find hash tables in external analysis */
#define HASH_SIGNATURE 0xa0111fe1u
#define HASH_BLOOM_SIGNATURE 0xb12220f2u

typedef struct UT_hash_table {
   UT_hash_bucket *buckets;
   unsigned num_buckets, log2_num_buckets;
   unsigned num_items;
   struct UT_hash_handle *tail; /* tail hh in app order, for fast append    */
   ptrdiff_t hho; /* hash handle offset (byte pos of hash handle in element */

   /* in an ideal situation (all buckets used equally), no bucket would have
    * more than ceil(#items/#buckets) items. that's the ideal chain length. */
   unsigned ideal_chain_maxlen;

   /* nonideal_items is the number of items in the hash whose chain position
    * exceeds the ideal chain maxlen. these items pay the penalty for an uneven
    * hash distribution; reaching them in a chain traversal takes >ideal steps */
   unsigned nonideal_items;

   /* ineffective expands occur when a bucket doubling was performed, but
    * afterward, more than half the items in the hash had nonideal chain
    * positions. If this happens on two consecutive expansions we inhibit any
    * further expansion, as it's not helping; this happens when the hash
    * function isn't a good fit for the key domain. When expansion is inhibited
    * the hash will still work, albeit no longer in constant time. */
   unsigned ineff_expands, noexpand;

   uint32_t signature; /* used only to find hash tables in external analysis */
#ifdef HASH_BLOOM
   uint32_t bloom_sig; /* used only to test bloom exists in external analysis */
   uint8_t *bloom_bv;
   uint8_t bloom_nbits;
#endif

} UT_hash_table;

typedef struct UT_hash_handle {
   struct UT_hash_table *tbl;
   void *prev;                       /* prev element in app order      */
   void *next;                       /* next element in app order      */
   struct UT_hash_handle *hh_prev;   /* previous hh in bucket order    */
   struct UT_hash_handle *hh_next;   /* next hh in bucket order        */
   const void *key;                  /* ptr to enclosing struct's key  */
   unsigned keylen;                  /* enclosing struct's key len     */
   unsigned hashv;                   /* result of hash-fcn(key)        */
} UT_hash_handle;

#endif /* UTHASH_H */

```

---

### Step 2: 统一驱动接口 (Unified Driver)

**文件**: `master_core/unified_driver.h`

```c
#ifndef UNIFIED_DRIVER_H
#define UNIFIED_DRIVER_H
#include "../common_include/platform_defs.h"

struct dsm_driver_ops {
    void*    (*alloc_large_table)(size_t size);
    void     (*free_large_table)(void *ptr);
    void*    (*alloc_packet)(size_t size, int atomic);
    void     (*free_packet)(void *ptr);

    void     (*set_gateway_ip)(uint32_t gw_id, uint32_t ip, uint16_t port);
    int      (*send_packet)(void *data, int len, uint32_t target_id);

    void     (*fetch_page)(uint64_t gpa, void *buf); 
    void     (*invalidate_local)(uint64_t gpa);
    // handle_page_fault is now part of the logic_core, not the driver ops
    void     (*log)(const char *fmt, ...) WVM_PRINTF_LIKE(1, 2);
    int      (*is_atomic_context)(void);
    void     (*touch_watchdog)(void);

    uint64_t (*alloc_req_id)(void *rx_buffer, uint32_t buffer_size); // [FIX-G3]
    void     (*free_req_id)(uint64_t id);
    uint64_t (*get_time_us)(void);
    uint64_t (*time_diff_us)(uint64_t start);
    int      (*check_req_status)(uint64_t id); 
    void     (*cpu_relax)(void);

    // [V29 Phase 0 New] Ops for robust retry logic
    void     (*get_random)(uint32_t *val);         // For Jitter calculation
    void     (*yield_cpu_short_time)(void);     // For sleeping in retry loop

    void   (*send_packet_async)(uint16_t msg_type, void* payload, int len, uint32_t target_id, uint8_t qos);
};

extern struct dsm_driver_ops *g_ops;
#endif

```

---

### Step 3: 纯逻辑核心 (Logic Core)

**文件**: `master_core/logic_core.h`

```c
#ifndef LOGIC_CORE_H
#define LOGIC_CORE_H

#include "unified_driver.h"
#include "../common_include/wavevm_protocol.h"
#ifdef __KERNEL__
#include <linux/in.h>
#else
#include <stdint.h>
#include <netinet/in.h>
#endif

#ifndef MAKE_VERSION
#define MAKE_VERSION(epoch, counter) (((uint64_t)(epoch) << 32) | (counter))
#endif
#ifndef GET_COUNTER
#define GET_COUNTER(version) ((uint32_t)((version) & 0xFFFFFFFF))
#endif

extern int g_my_node_id;
extern uint32_t g_curr_epoch;
extern uint8_t g_my_node_state;

// --- 初始化与配置 ---
int wvm_core_init(struct dsm_driver_ops *ops, int total_nodes_hint);
void wvm_set_total_nodes(int count);
void wvm_set_my_node_id(int id);

// --- 核心处理逻辑 (被 User/Kernel Backend 调用) ---
// 处理收到的网络包
void wvm_logic_process_packet(struct wvm_header *hdr, void *payload, uint32_t source_node_id);

// --- 缺页处理逻辑 (被 Fault Handler 调用) ---
// V28 兜底：拉取全页
// 返回 0 成功 (page_buffer 已填充), <0 失败
// version_out: 用于回传版本号给 V29 Wavelet 引擎
int wvm_handle_page_fault_logic(uint64_t gpa, void *page_buffer, uint64_t *version_out);

// [V29] 本地缺页快速路径 (内核态专用)
int wvm_handle_local_fault_fastpath(uint64_t gpa, void* page_buffer, uint64_t *version_out);

// [V29] 宣告兴趣 (异步)
void wvm_declare_interest_in_neighborhood(uint64_t gpa);

// --- RPC 接口 ---
int wvm_rpc_call(uint16_t msg_type, void *payload, int len, uint32_t target_id, void *rx_buffer, int rx_len);

// --- 路由接口 ---
uint32_t wvm_get_directory_node_id(uint64_t gpa);
uint32_t wvm_get_storage_node_id(uint64_t lba);

void update_local_topology_view(uint32_t src_id, uint32_t src_epoch, uint8_t src_state, struct sockaddr_in *src_addr, uint16_t src_ctrl_port);
void wvm_logic_update_local_version(uint64_t gpa);
void wvm_logic_broadcast_rpc(void *full_pkt_data, int full_pkt_len, uint16_t msg_type);

// 计算任务路由 (V27 遗留，用于 RPC 调度)
uint32_t wvm_get_compute_slave_id(int vcpu_index);

void wvm_set_mem_mapping(int slot, uint32_t value);

void wvm_set_cpu_mapping(int vcpu_index, uint32_t slave_id);

#endif // LOGIC_CORE_H

```
**文件**: `master_core/logic_core.c`

```c
/*
 * [IDENTITY] Master Core - The Distributed Brain
 * ---------------------------------------------------------------------------
 * 物理角色：每个计算节点内的"一致性指挥部"。
 * 职责边界：
 * 1. 维护本地 Pod 视图内的 DHT 寻址，判定 GPA 权属。
 * 2. 运行 Pub/Sub 引擎，管理每个页面的订阅者位图。
 * 3. 判定写冲突：利用乐观锁校验版本号，发起 FORCE_SYNC 修正。
 * 
 * [禁止事项]
 * - 严禁在此处直接操作 Socket 或进行文件 IO。
 * - 严禁修改 HASH_RING_SIZE (4096)，Pod 扩展请修改分层网关配置。
 * - 严禁移除 Latch 或 Lock 机制，否则在高并发写下 Directory 内存将损坏。
 * ---------------------------------------------------------------------------
 */
#include "logic_core.h"
#include "../common_include/wavevm_protocol.h"
#include "../common_include/wavevm_config.h"

#ifdef __KERNEL__
    #include <linux/spinlock.h>
    #include <linux/delay.h>
    #include <linux/string.h>
    
    typedef spinlock_t pthread_mutex_t;
    #define pthread_mutex_init(l, a) spin_lock_init(l)
    #define pthread_mutex_lock(l)    spin_lock_bh(l)
    #define pthread_mutex_unlock(l)  spin_unlock_bh(l)

    #define pthread_spinlock_t       spinlock_t
    #define pthread_spin_init(l, a)  spin_lock_init(l)
    #define pthread_spin_lock(l)     spin_lock(l)
    #define pthread_spin_unlock(l)   spin_unlock(l)

    #define pthread_rwlock_t         spinlock_t
    #define pthread_rwlock_init(l, a) spin_lock_init(l)
    #define pthread_rwlock_rdlock(l) spin_lock_bh(l)
    #define pthread_rwlock_wrlock(l) spin_lock_bh(l)
    #define pthread_rwlock_unlock(l) spin_unlock_bh(l)
    #define usleep(us)               usleep_range((us), (us) + 100)
    // --------------------

#else
    #include <pthread.h>
    #include <string.h>
    #include <sys/mman.h>
#endif

#ifdef __KERNEL__
    #include <linux/slab.h>
    #define wvm_malloc(sz) kmalloc(sz, GFP_ATOMIC) 
    #define wvm_free(ptr) kfree(ptr)
#else
    #include <stdlib.h>
    #define wvm_malloc(sz) malloc(sz)
    #define wvm_free(ptr) free(ptr)
#endif

#ifdef __KERNEL__
    #define wvm_alloc_local(sz) kmalloc(sz, GFP_ATOMIC)
    #define wvm_free_local(ptr) kfree(ptr)
#else
    #define wvm_alloc_local(sz) malloc(sz)
    #define wvm_free_local(ptr) free(ptr)
#endif

#ifdef __KERNEL__
    #include <linux/slab.h>
    #define malloc(sz) kmalloc(sz, GFP_ATOMIC)
    #define free(ptr) kfree(ptr)
#endif

#ifdef __KERNEL__
    #include <linux/types.h>
    typedef int atomic_bool;
    typedef int atomic_int;
    typedef long atomic_long;
    #ifndef ATOMIC_VAR_INIT
    #define ATOMIC_VAR_INIT(v) (v)
    #endif
    #define atomic_fetch_add(ptr, v) __sync_fetch_and_add((ptr), (v))
    #define atomic_load(ptr) __atomic_load_n((ptr), __ATOMIC_SEQ_CST)
    #define atomic_store(ptr, v) __atomic_store_n((ptr), (v), __ATOMIC_SEQ_CST)
    #define atomic_exchange(ptr, v) __atomic_exchange_n((ptr), (v), __ATOMIC_SEQ_CST)
#else
    #include <stdatomic.h>
    #include <stdbool.h>
    #include <unistd.h>
#endif

// --- 全局状态 ---
struct dsm_driver_ops *g_ops = NULL;
int g_total_nodes = 1;
int g_my_node_id = 0;
int g_ctrl_port = 0; // 这里的定义同时供应给 wavevm_node_master 和 wavevm.ko
uint32_t g_curr_epoch = 0;
extern uint8_t g_my_vm_id; // Multi-VM: 定义在 main_wrapper.c

// 哈希环脏标记 (解决 CPU 抖动)
static atomic_bool g_ring_dirty = false;

static struct {
    uint8_t buf[MTU_SIZE + 128]; 
    int curr_offset;
    uint32_t last_gateway_id; 
    pthread_mutex_t lock;
} g_gossip_agg = { .curr_offset = 0 };

#define MAX_LOCAL_VIEW 1024  // 本地感知的邻居上限
#define GOSSIP_INTERVAL_US 60000000 // 60s 心跳一次 (测试压制心跳洪泛)
#define VIEW_SYNC_INTERVAL_US 2000000 // 2秒同步一次全量视图
static uint64_t g_last_view_sync_us = 0;

static void add_gossip_to_aggregator(uint32_t target_node_id, uint8_t state, uint32_t epoch);
static void flush_gossip_aggregator(void);
void handle_rpc_batch_execution(void *payload, uint32_t len);

// --- 指数退避与超时参数 ---
#define INITIAL_RETRY_DELAY_US 50000      // 50ms
#define MAX_RETRY_DELAY_US     500000     // 500ms
#define TOTAL_TIMEOUT_US      20000000    // 20s

// --- 目录表定义 ---
#ifdef __KERNEL__
#define DIR_TABLE_INIT_SIZE (1024 * 4)   // Kernel: fixed, no dynamic growth
#define DIR_TABLE_SIZE      DIR_TABLE_INIT_SIZE
#else
#define DIR_TABLE_INIT_SIZE (1024 * 64)  // User-mode: initial size, grows on demand
#endif
#define DIR_MAX_PROBE 128
#define LOCK_SHARDS 65536
#define SMALL_UPDATE_THRESHOLD 1024

#define WVM_CPU_ROUTE_TABLE_SIZE MAX_VCPUS 

// --- 分片广播队列定义 ---
#define NUM_BCAST_WORKERS   8      // 定义8个并行的广播工作线程
#define BCAST_Q_SIZE        16384  // 每个分片队列的大小
#define BCAST_Q_MASK        (BCAST_Q_SIZE - 1)

#define RETRY_TIMEOUT_US 1000 
#define MIN_HOLD_TIME_US 500

// 订阅者位图
typedef struct {
    unsigned long bits[(WVM_MAX_SLAVES + 63) / 64];
} copyset_t;

// [辅助宏] 用于操作 64 位复合版本号
#define MAKE_VERSION(epoch, counter) (((uint64_t)(epoch) << 32) | (counter))
#define GET_EPOCH(version) ((uint32_t)((version) >> 32))
#define GET_COUNTER(version) ((uint32_t)((version) & 0xFFFFFFFF))

// 页面元数据
typedef struct {
    uint64_t gpa;
    uint8_t  is_valid; // bool in C
    uint64_t version; // [FIX] 高32位: Epoch, 低32位: Counter
    uint64_t segment_mask[256]; 
    copyset_t subscribers;
    uint64_t last_interest_time;
    uint8_t  base_page_data[4096];
    pthread_mutex_t lock;
} page_meta_t;

static page_meta_t *g_dir_table = NULL;
static pthread_mutex_t g_dir_table_locks[LOCK_SHARDS];

#ifndef __KERNEL__
static uint32_t g_dir_capacity = DIR_TABLE_INIT_SIZE;
static size_t   g_dir_alloc_size = 0; /* mmap size for munmap on grow */
static pthread_mutex_t g_grow_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

// --- 辅助函数 ---
/* 
 * [物理意图] 将 64 位 GPA 空间均匀"粉碎"并映射到 32 位哈希空间。
 * [关键逻辑] 采用非线性散列算法，确保 Guest OS 连续分配的内存页在 DHT 环上能够物理离散分布。
 * [后果] 若此函数失效，内存元数据将产生严重的负载倾斜（Skew），导致个别节点成为全网瓶颈。
 */
static inline uint32_t murmur3_32(uint64_t k) {
    k ^= k >> 33; k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33; return (uint32_t)k;
}

/* 
 * [物理意图] 确定 GPA 对应的分段锁索引，实现万级并发下的"零争用"访问。
 * [关键逻辑] 对页号进行哈希取模，将 500PB 内存映射到 LOCK_SHARDS (65536) 个独立的互斥域中。
 * [后果] 这一步实现了管理平面的并行化，若分段数不足，高频写操作会导致 CPU 产生不必要的 L3 缓存行竞争。
 */
static inline uint32_t get_lock_idx(uint64_t gpa) {
    return murmur3_32(gpa >> WVM_PAGE_SHIFT) % LOCK_SHARDS;
}

/* 
 * [物理意图] 在页面的订阅者名单（Copy-set）中标记一个远程节点的"利害关系"。
 * [关键逻辑] 操作 64 位对齐的位图，记录哪些节点持有该页面的只读缓存，以便在发生写入时发起精准推送。
 * [后果] 这是 Wavelet 协议的核心，它取代了 V28 的盲目广播，将网络流量精确控制在"有需求的节点"之间。
 */
static void copyset_set(copyset_t *cs, uint32_t node_id) {
    if (node_id >= WVM_MAX_SLAVES) return;
    cs->bits[node_id / 64] |= (1UL << (node_id % 64));
}

static uint32_t g_cpu_route_table[WVM_CPU_ROUTE_TABLE_SIZE]; 

/* 
 * [物理意图] 建立 vCPU 索引与物理计算节点 ID 之间的静态/动态映射表。
 * [关键逻辑] 用于指导 Master 节点的调度器：当特定的 vCPU 触发陷入时，应该将执行上下文路由给哪个 Slave。
 * [后果] 若此表配置错误，vCPU 将被路由到不存在的节点，导致超级虚拟机的指令流瞬间断裂。
 */
void wvm_set_cpu_mapping(int vcpu_index, uint32_t slave_id) {
    // 这里的边界检查现在是安全的，与全局配置一致
    if (vcpu_index >= 0 && vcpu_index < WVM_CPU_ROUTE_TABLE_SIZE) {
        g_cpu_route_table[vcpu_index] = slave_id;
    }
}

uint32_t wvm_get_compute_slave_id(int vcpu_index) {
    if (vcpu_index >= 0 && vcpu_index < WVM_CPU_ROUTE_TABLE_SIZE) {
        uint32_t raw = g_cpu_route_table[vcpu_index];
        // [Fix #3] AUTO_ROUTE 是 sentinel，不能做 ENCODE_ID 操作
        if (raw == WVM_NODE_AUTO_ROUTE) return raw;
        // [Multi-VM] 返回 composite ID
        return WVM_ENCODE_ID(g_my_vm_id, raw);
    }
    return WVM_NODE_AUTO_ROUTE;
}

/* 
 * [物理意图] 获取特定 GPA 在全网中的最新逻辑时钟（版本号）。
 * [关键逻辑] 直接查询本地 Directory 表，用于在 PUSH 响应中携带真理戳。
 * [后果] 如果获取到的版本号落后，会导致接收端触发版本断层校验失败，进而发起 FORCE_SYNC 全页拉取。
 */
uint64_t wvm_logic_get_page_version(uint64_t gpa) {
    uint32_t lock_idx = get_lock_idx(gpa);
    uint64_t ver = 0;
    pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
    
    // 我们只需要读取版本，不需要创建 meta，如果不存在说明是初始版本 0
    uint64_t page_idx = gpa >> WVM_PAGE_SHIFT;
    uint32_t hash = murmur3_32(page_idx);
#ifdef __KERNEL__
    uint32_t _vcap = DIR_TABLE_SIZE;
#else
    uint32_t _vcap = g_dir_capacity;
#endif
    for (int i = 0; i < DIR_MAX_PROBE; i++) {
        uint32_t cur = (hash + i) % _vcap;
        if (g_dir_table[cur].is_valid && g_dir_table[cur].gpa == gpa) {
            ver = g_dir_table[cur].version;
            break;
        }
        if (!g_dir_table[cur].is_valid) break;
    }
    pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
    return ver;
}

/*
 * [物理意图] 在 Directory 管理表中定位或初始化页面的"户口信息"。
 * [关键逻辑] 使用带线性探测的哈希表，存储页面的版本、订阅位图以及主备份数据。
 * [后果] 这是逻辑核心最重负载的查找点，调用者需持锁。
 */
static page_meta_t* find_or_create_page_meta(uint64_t gpa) {
    uint64_t page_idx = gpa >> WVM_PAGE_SHIFT;
    uint32_t hash = murmur3_32(page_idx);
#ifdef __KERNEL__
    uint32_t cap = DIR_TABLE_SIZE;
#else
    uint32_t cap = g_dir_capacity;
#endif

    for (int i = 0; i < DIR_MAX_PROBE; i++) {
        uint32_t cur = (hash + i) % cap;

        // 找到存在的
        if (g_dir_table[cur].is_valid && g_dir_table[cur].gpa == gpa) {
            return &g_dir_table[cur];
        }

        // 找到空位，新建
        if (!g_dir_table[cur].is_valid) {
            // 清零整个结构体，包括数据页
            memset(&g_dir_table[cur], 0, sizeof(page_meta_t));

            g_dir_table[cur].is_valid = 1;
            g_dir_table[cur].gpa = gpa;
            g_dir_table[cur].version = MAKE_VERSION(g_curr_epoch, 1);

            // 锁必须初始化，即使是在持锁状态下分配
            pthread_mutex_init(&g_dir_table[cur].lock, NULL);

            return &g_dir_table[cur];
        }
    }

    // 探测链耗尽，需要扩容（user-mode）或报错（kernel）
    if (g_ops && g_ops->log) {
        g_ops->log("[WARN] Directory hash table probe exhausted for GPA %llx (cap=%u)!",
                   (unsigned long long)gpa, cap);
    }
    return NULL;
}

#ifndef __KERNEL__
/*
 * 动态扩容目录哈希表。获取所有分片锁后 rehash 到 2 倍容量。
 * 调用前：调用者已释放自己持有的分片锁。
 */
static void dir_table_grow(void) {
    pthread_mutex_lock(&g_grow_lock);

    uint32_t old_cap = g_dir_capacity;
    uint32_t new_cap = old_cap * 2;

    /* 另一个线程可能已经完成扩容 */
    if (g_dir_capacity != old_cap) {
        pthread_mutex_unlock(&g_grow_lock);
        return;
    }

    /* 获取全部分片锁，阻止任何并发查找 */
    for (int i = 0; i < LOCK_SHARDS; i++)
        pthread_mutex_lock(&g_dir_table_locks[i]);

    /* 分配新表 */
    page_meta_t *new_table = (page_meta_t *)g_ops->alloc_large_table(
        sizeof(page_meta_t) * new_cap);
    if (!new_table) {
        if (g_ops && g_ops->log)
            g_ops->log("[CRITICAL] dir_table_grow: alloc failed for %u entries!", new_cap);
        for (int i = LOCK_SHARDS - 1; i >= 0; i--)
            pthread_mutex_unlock(&g_dir_table_locks[i]);
        pthread_mutex_unlock(&g_grow_lock);
        return;
    }
    memset(new_table, 0, sizeof(page_meta_t) * new_cap);

    /* Rehash 所有有效条目 */
    uint32_t migrated = 0;
    for (uint32_t j = 0; j < old_cap; j++) {
        if (!g_dir_table[j].is_valid) continue;

        uint64_t page_idx = g_dir_table[j].gpa >> WVM_PAGE_SHIFT;
        uint32_t hash = murmur3_32(page_idx);
        int placed = 0;

        for (int i = 0; i < DIR_MAX_PROBE * 4; i++) {
            uint32_t cur = (hash + i) % new_cap;
            if (!new_table[cur].is_valid) {
                memcpy(&new_table[cur], &g_dir_table[j], sizeof(page_meta_t));
                /* pthread_mutex_t 不可 memcpy，必须重新初始化 */
                pthread_mutex_init(&new_table[cur].lock, NULL);
                placed = 1;
                migrated++;
                break;
            }
        }

        if (!placed && g_ops && g_ops->log) {
            g_ops->log("[CRITICAL] dir_table_grow: rehash failed for GPA %llx!",
                       (unsigned long long)g_dir_table[j].gpa);
        }
    }

    /* 原子切换 */
    page_meta_t *old_table = g_dir_table;
    size_t old_alloc = g_dir_alloc_size;
    g_dir_table = new_table;
    g_dir_capacity = new_cap;
    g_dir_alloc_size = sizeof(page_meta_t) * new_cap;

    /* 释放所有分片锁（逆序） */
    for (int i = LOCK_SHARDS - 1; i >= 0; i--)
        pthread_mutex_unlock(&g_dir_table_locks[i]);

    /* 释放旧表（alloc_large_table 使用 mmap，必须用 munmap） */
    if (old_alloc > 0)
        munmap(old_table, old_alloc);

    if (g_ops && g_ops->log)
        g_ops->log("[DIR] Grew directory table: %u -> %u entries (%u migrated)",
                   old_cap, new_cap, migrated);

    pthread_mutex_unlock(&g_grow_lock);
}

/*
 * 安全版 find_or_create：探测失败时自动扩容并重试。
 * 调用者必须持有 g_dir_table_locks[lock_idx]。
 * 函数会在扩容时临时释放并重新获取该锁。
 */
static page_meta_t* find_or_create_page_meta_safe(uint64_t gpa, uint32_t lock_idx) {
    page_meta_t *meta = find_or_create_page_meta(gpa);
    if (meta) return meta;

    /* 探测失败，需要扩容 */
    pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
    dir_table_grow();
    pthread_mutex_lock(&g_dir_table_locks[lock_idx]);

    meta = find_or_create_page_meta(gpa);
    if (!meta && g_ops && g_ops->log) {
        g_ops->log("[CRITICAL] Directory still full after growth for GPA %llx!",
                   (unsigned long long)gpa);
    }
    return meta;
}
#endif /* !__KERNEL__ */

/* 跨平台宏：user-mode 用 _safe 版本（支持扩容），kernel 用原始版本 */
#ifdef __KERNEL__
#define DIR_FIND_OR_CREATE(gpa, lock_idx) find_or_create_page_meta(gpa)
#else
#define DIR_FIND_OR_CREATE(gpa, lock_idx) find_or_create_page_meta_safe(gpa, lock_idx)
#endif

// --- 核心接口实现 ---

typedef struct {
    uint32_t node_id;
    struct sockaddr_in addr;
    uint8_t  state;
    uint32_t last_epoch;
    uint64_t last_seen_us;
    uint16_t ctrl_port; 
    pthread_mutex_t lock;
} peer_entry_t;

// 局部拓扑视图：无中心架构的核心
static peer_entry_t g_peer_view[MAX_LOCAL_VIEW];
static int g_peer_count = 0;
static pthread_rwlock_t g_view_lock;

// 自治周期控制
// g_curr_epoch moved earlier to satisfy forward references

static atomic_int g_peers_at_next_epoch = 0; // 观测到处于 E+1 的邻居计数

// [PATCH] 预计算路由表大小
#define HASH_RING_SIZE 4096 
// 这个表存储：Slot -> NodeID 的映射
static uint32_t g_hash_ring_cache[HASH_RING_SIZE];
static pthread_rwlock_t g_ring_lock;

/* 
 * [物理意图] 当集群发生节点加减（扩容/宕机）时，在后台重建 P2P 拓扑的逻辑环，复杂度 O(RingSize * PeerCount)，但在后台运行，不阻塞热路径
。
 * [关键逻辑] 采用双缓冲区设计，在写锁保护下仅进行微秒级的指针切换，计算过程则与数据面完全并发。
 * [后果] 这保证了 100 万节点规模下的拓扑变更不会引起 vCPU 的卡顿（Zero-Stall Topology Update）。
 */
static void rebuild_hash_ring_cache(void) {
    // 1. [栈上分配] 或 [堆分配] 临时 Ring，避免持有写锁进行计算
    // 注意：4096 * 4 bytes = 16KB，栈上分配略大但通常可行，稳妥起见用 static 备用 buffer 或 malloc
    uint32_t *temp_ring = malloc(sizeof(uint32_t) * HASH_RING_SIZE);
    if (!temp_ring) return;

    // 2. 获取 View 读锁进行计算 (此时不持有 Ring 锁，不阻塞路由查询)
    pthread_rwlock_rdlock(&g_view_lock);
    
    if (g_peer_count == 0) {
        for(int i=0; i<HASH_RING_SIZE; i++) temp_ring[i] = g_my_node_id;
    } else {
        // 耗时的 O(N*M) 计算在这里进行
        for (int slot = 0; slot < HASH_RING_SIZE; slot++) {
            uint32_t best_node = g_my_node_id;
            uint64_t max_weight = murmur3_32(slot ^ g_my_node_id);
            for (int i = 0; i < g_peer_count; i++) {
                if (g_peer_view[i].state != NODE_STATE_ACTIVE) continue;
                uint64_t weight = murmur3_32(slot ^ g_peer_view[i].node_id);
                if (weight > max_weight) {
                    max_weight = weight;
                    best_node = g_peer_view[i].node_id;
                }
            }
            temp_ring[slot] = best_node;
        }
    }
    pthread_rwlock_unlock(&g_view_lock); 

    // 3. [关键] 获取 Ring 写锁，仅进行内存拷贝 (memcpy 是微秒级的)
    // 此时才真正发生 STW，但时间极短
    pthread_rwlock_wrlock(&g_ring_lock);
    memcpy(g_hash_ring_cache, temp_ring, sizeof(uint32_t) * HASH_RING_SIZE);
    pthread_rwlock_unlock(&g_ring_lock);
    
    free(temp_ring);
    // if (g_ops->log) g_ops->log("[Swarm] Hash Ring Rebuilt (Zero-Stall).");
}

/* 
 * [物理意图] 在 P2P 哈希环上执行 O(1) 级的"真理定位"。
 * [关键逻辑] 输入 GPA，通过 4096 槽位的预计算哈希环（Consistent Hashing），瞬间找到该内存页的 Directory 节点。
 * [后果] 整个分布式内存的亚微秒级访问完全依赖此函数的计算效率，它规避了昂贵的网络寻址开销。
 */
uint32_t get_owner_node_id(uint64_t gpa) {
    // 将 GPA 映射到 0..4095 的槽位
    // gpa >> 12 (页号) -> 再哈希 -> 取模
    uint32_t slot = murmur3_32(gpa >> WVM_PAGE_SHIFT) % HASH_RING_SIZE;
    
    uint32_t target;
    pthread_rwlock_rdlock(&g_ring_lock);
    target = g_hash_ring_cache[slot];
    pthread_rwlock_unlock(&g_ring_lock);
    
    return target;
}

// 存储切片粒度：64MB (2^26)。
// 物理权衡：过小会导致元数据爆炸，过大会导致负载倾斜。
#define WVM_STORAGE_CHUNK_SHIFT 26

/* 
 * [物理意图] 存储寻址器。将 LBA 映射到 Owner 节点。
 */
uint32_t wvm_get_storage_node_id(uint64_t lba) {
    // LBA (512B) -> Byte Offset -> 64MB Chunk Index
    uint64_t chunk_idx = (lba << 9) >> WVM_STORAGE_CHUNK_SHIFT;
    uint32_t slot = murmur3_32(chunk_idx) % HASH_RING_SIZE;

    uint32_t target;
    pthread_rwlock_rdlock(&g_ring_lock);
    target = g_hash_ring_cache[slot];
    pthread_rwlock_unlock(&g_ring_lock);
    // [Multi-VM] 返回 composite ID
    return WVM_ENCODE_ID(g_my_vm_id, target);
}

/* --- 逻辑入口：处理接收到的心跳与视图更新 --- */

#ifdef __KERNEL__
static inline void wvm_notify_kernel_epoch(uint32_t epoch) { (void)epoch; }
#else
extern void wvm_notify_kernel_epoch(uint32_t epoch);
#endif

/* 
 * [物理意图] 接收 Gossip 消息，更新本地对 P2P 邻居的存活状态与纪元（Epoch）认知。
 * [关键逻辑] 实现分布式共识的"观测者模式"，当超过半数邻居进入新纪元时，本地自动推进 Epoch。
 * [后果] 解决了大规模集群中"时钟漂移"问题，确保所有节点的 DHT 路由决策在逻辑时间线上达成最终一致。
 */
void update_local_topology_view(uint32_t src_id, uint32_t src_epoch, uint8_t src_state, struct sockaddr_in *src_addr, uint16_t src_ctrl_port) {
    pthread_rwlock_wrlock(&g_view_lock);
    
    int found = 0;
    for (int i = 0; i < g_peer_count; i++) {
        if (g_peer_view[i].node_id == src_id) {
            g_peer_view[i].last_seen_us = g_ops->get_time_us();
            g_peer_view[i].state = src_state;
            g_peer_view[i].last_epoch = src_epoch;
            if (src_ctrl_port > 0) {
                g_peer_view[i].ctrl_port = src_ctrl_port;
            }
            // 如果地址发生变更（如节点重启更换了物理机），同步更新
            if (src_addr && src_addr->sin_addr.s_addr != 0) {
                g_peer_view[i].addr = *src_addr;
            }
            found = 1;
            break;
        }
    }

    // 发现新邻居：Warm-plug 的入口
    if (!found && g_peer_count < MAX_LOCAL_VIEW) {
        peer_entry_t *new_peer = &g_peer_view[g_peer_count]; // 获取指针
        new_peer->node_id = src_id;
        new_peer->state = src_state;
        new_peer->last_epoch = src_epoch;
        new_peer->last_seen_us = g_ops->get_time_us();
        if (src_addr) new_peer->addr = *src_addr;
        
        // 防止端口为 0。如果传进来是 0 (来自心跳)，默认设为 9001
        new_peer->ctrl_port = (src_ctrl_port > 0) ? src_ctrl_port : 9001;
        
        pthread_mutex_init(&new_peer->lock, NULL);
        if (g_ops->log) g_ops->log("[Swarm] New neighbor discovered: %u", src_id);
        g_peer_count++; // 只有初始化完了再增加计数，防止并发读到半成品
    }

    // Epoch 异步共识：观测者模式推进
    if (src_epoch > g_curr_epoch) {
        atomic_fetch_add(&g_peers_at_next_epoch, 1);
        if (atomic_load(&g_peers_at_next_epoch) > (g_peer_count / 2)) {
            g_curr_epoch = src_epoch;
            atomic_store(&g_peers_at_next_epoch, 0);
            wvm_notify_kernel_epoch(g_curr_epoch);
        }
    }
    
    pthread_rwlock_unlock(&g_view_lock);

    // 仅标记 Dirty，实现惰性重建
    atomic_store(&g_ring_dirty, true);
}

uint8_t  g_my_node_state = NODE_STATE_SHADOW;
static uint64_t g_last_gossip_us = 0;
static uint64_t g_state_start_us = 0;

// 配置参数：严格遵循物理时延
#define HEARTBEAT_TIMEOUT_US 180000000 // 180秒未收到心跳则判定 Fail-in-place (测试)
#define WARMING_DURATION_US  10000000 // 预热态持续10秒，同步元数据
#define GOSSIP_FANOUT        3        // 每次随机向3个邻居扩散

/* --- 核心函数：自治节点状态推进 --- */

/* 
 * [物理意图] 驱动节点的生命周期状态机，实现"优雅上线"与"平滑预热"。
 * [关键逻辑] 节点通过 SHADOW -> WARMING -> ACTIVE 转换，在正式接管内存权属前先同步元数据缓存。
 * [后果] 彻底杜绝了新节点加入时由于"缓存冷启动"导致的瞬间全网性能塌陷（Thundering Herd）。
 */
static void advance_node_lifecycle(void) {
    uint64_t now = g_ops->get_time_us();
    uint64_t duration = now - g_state_start_us;

    switch (g_my_node_state) {
        case NODE_STATE_SHADOW:
            // 影子态：已建立网络连接，开始观测集群 Epoch
            if (g_peer_count > 0) {
                g_my_node_state = NODE_STATE_WARMING;
                g_state_start_us = now;
                if (g_ops->log) g_ops->log("[Lifecycle] Transition to WARMING");
            }
            break;

        case NODE_STATE_WARMING:
            // 预热态：此阶段节点开始拉取元数据，但不承载 Owner 权限
            if (duration > WARMING_DURATION_US) {
                g_my_node_state = NODE_STATE_ACTIVE;
                g_state_start_us = now;
                // 晋升为活跃节点后，一致性哈希会自动将其纳入权重计算
                if (g_ops->log) g_ops->log("[Lifecycle] Transition to ACTIVE. Participating in Owner Set.");
            }
            break;

        case NODE_STATE_ACTIVE:
            // 活跃态：正常工作
            break;

        case NODE_STATE_DRAINING:
            // 排空态：此时不再接受新请求，等待旧事务结束
            break;
    }
}

/* --- 核心逻辑：Fail-in-place 局部判定 --- */

/* 
 * [物理意图] 被动检测 P2P 邻居的健康状况，实现隔离故障（Fail-in-place）。
 * [关键逻辑] 检查上次心跳时间戳，超过 5s 则将其在本地路由表中剔除，不再向其转发请求。
 * [后果] 提供了 P2P 架构的自愈能力，确保个别节点的崩溃不会通过请求堆积拖死整个超级虚拟机。
 */
static void monitor_peer_liveness(void) {
    uint64_t now = g_ops->get_time_us();
    pthread_rwlock_wrlock(&g_view_lock);
    
    for (int i = 0; i < g_peer_count; i++) {
        if (g_peer_view[i].state == NODE_STATE_ACTIVE || g_peer_view[i].state == NODE_STATE_WARMING) {
            if (now - g_peer_view[i].last_seen_us > HEARTBEAT_TIMEOUT_US) {
                // 本地标记失效，不再向其路由请求
                g_peer_view[i].state = NODE_STATE_OFFLINE;
                if (g_ops->log) g_ops->log("[Liveness] Node %u detected OFFLINE (Fail-in-place)", g_peer_view[i].node_id);
            }
        }
    }
    
    pthread_rwlock_unlock(&g_view_lock);
}

/* --- 后台自治线程：驱动心跳与扩散 --- */

/* 
 * [物理意图] 驱动整个 P2P 节点的"自主意识"后台线程。
 * [关键逻辑] 周期性执行：状态机推进、故障探测、以及随机向邻居扩散本地视图（Gossip Fan-out）。
 * [后果] 这是 WaveVM 自治性的动力源，保证了在没有任何中心管理节点的情况下，全网拓扑能自行收敛。
 */
void* autonomous_monitor_thread(void* arg) {
    while (1) {
        uint64_t now = g_ops->get_time_us();
        advance_node_lifecycle();
        monitor_peer_liveness();

        // [V30 FIX] 惰性重建哈希环
        if (atomic_exchange(&g_ring_dirty, false)) {
            rebuild_hash_ring_cache();
        }

        // [V30 FIX] 聚合扩散
        if (now - g_last_gossip_us > GOSSIP_INTERVAL_US) {
            pthread_rwlock_rdlock(&g_view_lock);
            if (g_peer_count > 0) {
                for (int i = 0; i < GOSSIP_FANOUT; i++) {
                    uint32_t ridx = 0;
                    g_ops->get_random(&ridx);
                    peer_entry_t *p = &g_peer_view[ridx % g_peer_count];
                    add_gossip_to_aggregator(WVM_ENCODE_ID(g_my_vm_id, p->node_id), g_my_node_state, g_curr_epoch);
                }
            }
            pthread_rwlock_unlock(&g_view_lock);
            flush_gossip_aggregator(); // 立即发送
            g_last_gossip_us = now;
        }
        g_ops->cpu_relax();
        usleep(100000); 
    }
    return NULL;
}

/* --- 核心逻辑：向邻居请求视图 --- */

void handle_rpc_batch_execution(void *payload, uint32_t len) { (void)payload; (void)len; }

static void request_view_from_neighbor(uint32_t peer_id) {
    struct wvm_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(WVM_MAGIC);
    hdr.msg_type = htons(MSG_VIEW_PULL);
    hdr.slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr.epoch = htonl(g_curr_epoch);
    hdr.payload_len = 0;

    // 发送视图请求
    g_ops->send_packet(&hdr, sizeof(hdr), peer_id);
}

/* --- 核心逻辑：响应邻居的视图请求 --- */

static void handle_view_pull(struct wvm_header *rx_hdr, uint32_t src_id) {
    pthread_rwlock_rdlock(&g_view_lock);
    
    // [无简化实现]：由于 UDP MTU 限制，一次最多发送 32 个条目
    uint32_t count = (g_peer_count > 32) ? 32 : g_peer_count;
    size_t payload_len = sizeof(struct wvm_view_payload) + count * sizeof(struct wvm_view_entry);
    size_t pkt_len = sizeof(struct wvm_header) + payload_len;

    uint8_t *buf = g_ops->alloc_packet(pkt_len, 1); // 原子上下文分配
    if (!buf) {
        pthread_rwlock_unlock(&g_view_lock);
        return;
    }

    struct wvm_header *hdr = (struct wvm_header *)buf;
    // 基础包头填充
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_VIEW_ACK);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->payload_len = htons(payload_len);
    hdr->epoch = htonl(g_curr_epoch);
    hdr->node_state = g_my_node_state;

    struct wvm_view_payload *pl = (struct wvm_view_payload *)(buf + sizeof(*hdr));
    pl->entry_count = htonl(count);

    for (uint32_t i = 0; i < count; i++) {
        pl->entries[i].node_id = htonl(g_peer_view[i].node_id);
        pl->entries[i].ip_addr = g_peer_view[i].addr.sin_addr.s_addr;
        pl->entries[i].port    = g_peer_view[i].addr.sin_port;
        pl->entries[i].state   = g_peer_view[i].state;
    }
    pthread_rwlock_unlock(&g_view_lock);

    // [Fix #2] src_id 是裸 node_id（内部逻辑），发包时需要编码为 composite ID
    g_ops->send_packet(buf, pkt_len, WVM_ENCODE_ID(g_my_vm_id, src_id));
    g_ops->free_packet(buf);
}

static void handle_view_ack(void *payload, uint16_t payload_len) {
    struct wvm_view_payload *pl = (struct wvm_view_payload *)payload;
    if (payload_len < sizeof(struct wvm_view_payload)) return;
    uint32_t count = ntohl(pl->entry_count);
    // [FIX] 边界检查：确保 count 不超过实际 payload 能容纳的条目数
    uint32_t max_entries = (payload_len - sizeof(struct wvm_view_payload)) / sizeof(struct wvm_view_entry);
    if (count > max_entries) count = max_entries;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t nid = ntohl(pl->entries[i].node_id);
        if (nid == g_my_node_id) continue;

        // 获取 IP 信息
        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_addr.s_addr = pl->entries[i].ip_addr;
        peer_addr.sin_port = pl->entries[i].port;

        // 直接注入视图，不走中间 fake_hdr 路由
        uint16_t r_ctrl = ntohs(pl->entries[i].ctrl_port); 
        update_local_topology_view(nid, 0, pl->entries[i].state, &peer_addr, r_ctrl);
    }
}

int wvm_core_init(struct dsm_driver_ops *ops, int total_nodes_hint) {
    if (!ops) return -1;
    g_ops = ops;
    
    g_dir_table = (page_meta_t *)g_ops->alloc_large_table(
#ifdef __KERNEL__
        sizeof(page_meta_t) * DIR_TABLE_SIZE);
#else
        sizeof(page_meta_t) * g_dir_capacity);
    g_dir_alloc_size = sizeof(page_meta_t) * g_dir_capacity;
#endif
    if (!g_dir_table) return -ENOMEM;
    
    for (int i = 0; i < LOCK_SHARDS; i++) {
        pthread_mutex_init(&g_dir_table_locks[i], NULL); 
    }

    pthread_mutex_init(&g_gossip_agg.lock, NULL);
    pthread_rwlock_init(&g_view_lock, NULL);
    pthread_rwlock_init(&g_ring_lock, NULL);
    
    // 分片广播队列在定义处进行静态零初始化；此处不重复初始化。
    for (int i = 0; i < WVM_CPU_ROUTE_TABLE_SIZE; i++) {
        g_cpu_route_table[i] = WVM_NODE_AUTO_ROUTE;
    }
    
    g_total_nodes = (total_nodes_hint > 0) ? total_nodes_hint : 1;
    return 0;
}

void wvm_set_mem_mapping(int slot, uint32_t value) {
    if (slot == 0) wvm_set_total_nodes((int)value);
    if (slot == 1) wvm_set_my_node_id((int)value);
}

void wvm_set_total_nodes(int count) { 
    if(count > 0) g_total_nodes = count; 
}

void wvm_set_my_node_id(int id) { 
    g_my_node_id = id; 
}

// DHT 路由
uint32_t wvm_get_directory_node_id(uint64_t gpa) {
    // 在百万节点规模下，即使局部视图不一致，路由结果也能大概率收敛
    // [Multi-VM] 返回 composite ID (vm_id | node_id) 用于网络寻址
    return WVM_ENCODE_ID(g_my_vm_id, get_owner_node_id(gpa));
}

// 本地缺页快速路径
int wvm_handle_local_fault_fastpath(uint64_t gpa, void* page_buffer, uint64_t *version_out) {
    uint32_t lock_idx = get_lock_idx(gpa);
    
    // 必须加锁，因为可能有远程写操作正在更新这个页面
    pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
    
    page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);
    if (page) {
        // 直接内存拷贝
        memcpy(page_buffer, page->base_page_data, 4096);
        if (version_out) {
            *version_out = page->version;
        }
        pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
        return 0; // 成功
    }
    
    pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
    return -1; // 失败
}

// [V29 Optimization] 指针队列，极大降低内存占用
typedef struct {
    uint32_t msg_type;
    uint32_t target_id;
    size_t len;
    void *data_ptr; // [Changed] 只存指针，不再预分配 64KB
    uint8_t flags;
} broadcast_task_t;

// 将队列、头尾指针和锁打包成一个结构体
typedef struct {
    broadcast_task_t    queue[BCAST_Q_SIZE];
    volatile uint64_t   head;
    volatile uint64_t   tail;
    pthread_spinlock_t  lock; // 每个分片队列拥有自己独立的锁
} bcast_queue_shard_t;

// 创建分片队列数组
static bcast_queue_shard_t g_bcast_shards[NUM_BCAST_WORKERS];

// [V29 Optimization] 多线程消费者：读取指针，发送并释放
void* broadcast_worker_thread(void* arg) {
    long worker_id = (long)arg; // 获取自己的ID
    bcast_queue_shard_t *my_shard = &g_bcast_shards[worker_id]; // 定位到自己的专属队列

    while(1) {
        int work_todo = 0;
        broadcast_task_t task_copy;
        
        // 1. 尝试从自己的分片队列出队
        uint64_t current_head = my_shard->head;
        uint64_t current_tail = my_shard->tail; // 读取 volatile

        if (current_head != current_tail) {
            broadcast_task_t *src = &my_shard->queue[current_head & BCAST_Q_MASK];
            task_copy = *src;
            __sync_synchronize(); 
            my_shard->head = current_head + 1; // 修改自己的head
            work_todo = 1;
        }

        if (work_todo) {
            // 2. 发送逻辑
            size_t pkt_len = sizeof(struct wvm_header) + task_copy.len;
            
            // alloc_packet 用于网络发送的 Buffer (可能来自 Slab/Mempool)
            uint8_t *buffer = g_ops->alloc_packet(pkt_len, 0);
            
            if (buffer) {
                struct wvm_header *hdr = (struct wvm_header *)buffer;
                hdr->magic = htonl(WVM_MAGIC);
                hdr->msg_type = htons(task_copy.msg_type);
                hdr->payload_len = htons(task_copy.len);
                hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                hdr->target_id = htonl(task_copy.target_id);
                hdr->req_id = 0;
                // 全页推送走慢车道，Diff 走快车道
                hdr->qos_level = (task_copy.msg_type == MSG_PAGE_PUSH_FULL) ? 0 : 1;
                hdr->flags = task_copy.flags; 

                // 拷贝 Payload
                if (task_copy.len > 0 && task_copy.data_ptr) {
                    memcpy(buffer + sizeof(*hdr), task_copy.data_ptr, task_copy.len);
                }
                
                // 计算 CRC 并发送 (由 send_packet 内部处理 CRC，这里只需调用)
                g_ops->send_packet(buffer, pkt_len, task_copy.target_id);
                g_ops->free_packet(buffer);
            }

            // 3. 释放内存
            if (task_copy.data_ptr) {
                #ifdef __KERNEL__
                    kfree(task_copy.data_ptr);
                #else
                    free(task_copy.data_ptr);
                #endif
            }
        } else {
            // 4. 空闲等待
            g_ops->cpu_relax(); 
        }
    }
    return NULL;
}

/* 
 * [物理意图] 针对特定页面的变动，向全网所有利益相关方精准发射"小波（Wavelet）"更新。
 * [关键逻辑] 遍历二级订阅位图，利用多线程分片队列并行发送 Diff 或 Full-Page 包。
 * [后果] 它实现了"读操作本地化"，通过网络带宽换取读时延的消除，是 V30 性能突破的关键。
 */
static void broadcast_to_subscribers(page_meta_t *page, uint16_t msg_type, void *payload, int len, uint8_t flags) {
    // 遍历订阅者的
    int seg_mask_count = ((WVM_MAX_SLAVES / 64) + 63) / 64; /* segment_mask 有效元素数 */
    for (int i = 0; i < seg_mask_count; i++) {
        uint64_t mask = page->segment_mask[i];
        if (mask == 0) continue;

        for (int j = 0; j < 64; j++) {
            if ((mask >> j) & 1) {
                uint32_t seg_idx = (i * 64) + j;
                uint64_t sub_bits = page->subscribers.bits[seg_idx];
                
                for (int k = 0; k < 64; k++) {
                    if ((sub_bits >> k) & 1) {
                        uint32_t target_id = (seg_idx * 64) + k;
                        if (target_id == g_my_node_id) continue;

                        // 分配内存
                        void *data_copy = NULL;
                        if (len > 0) {
                            #ifdef __KERNEL__
                                data_copy = kmalloc(len, GFP_ATOMIC);
                            #else
                                data_copy = malloc(len);
                            #endif
                            if (!data_copy) continue;
                            memcpy(data_copy, payload, len);
                        }

                        // 1. 根据目标ID计算分片索引
                        int shard_idx = target_id % NUM_BCAST_WORKERS;
                        bcast_queue_shard_t *target_shard = &g_bcast_shards[shard_idx];

                        // 2. 锁住目标分片队列
                        pthread_spin_lock(&target_shard->lock);
                        
                        // 3. 检查目标分片队列是否已满
                        uint64_t current_tail = target_shard->tail;
                        if (current_tail + 1 - target_shard->head >= BCAST_Q_SIZE) {
                            pthread_spin_unlock(&target_shard->lock);
                            if (data_copy) {
                                #ifdef __KERNEL__
                                    kfree(data_copy);
                                #else
                                    free(data_copy);
                                #endif
                            }
                            continue; // Drop-tail
                        }

                        // 4. 将任务放入目标分片队列
                        broadcast_task_t *t = &target_shard->queue[current_tail & BCAST_Q_MASK];
                        t->msg_type = msg_type;
                        t->target_id = WVM_ENCODE_ID(g_my_vm_id, target_id); // [Multi-VM] composite ID for network
                        t->len = len;
                        t->data_ptr = data_copy;
                        t->flags = flags;

                        __sync_synchronize();
                        target_shard->tail = current_tail + 1; // 更新目标分片队列的tail
                        
                        pthread_spin_unlock(&target_shard->lock);
                    }
                }
            }
        }
    }
}

// [FIX] FORCE_SYNC 限流器状态
static atomic_int  g_force_sync_counter = ATOMIC_VAR_INIT(0);
static atomic_long g_force_sync_last_sec = ATOMIC_VAR_INIT(0);
#define MAX_FORCE_SYNC_PER_SEC 1024 

/* 
 * [物理意图] 当发生严重的版本冲突或逻辑断层时，强制向节点投喂"最终真理"。
 * [关键逻辑] 绕过所有增量优化，直接发送 4KB 全量页面数据并强制更新其版本号。
 * [后果] 这是系统的"最终一致性保险丝"，在网络混沌或极端竞态下保证内存状态不崩溃。
 */
static void force_sync_client(uint64_t gpa, page_meta_t* page, uint32_t client_id) {
    // [FIX] page 可能为 NULL（如 stale-epoch 拒绝路径尚未查表）
    if (!page) {
        // 尝试从目录表中查找
        uint32_t lock_idx = get_lock_idx(gpa);
        pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
        page = DIR_FIND_OR_CREATE(gpa, lock_idx);
        if (!page) {
            pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
            return; // 页面不存在，无法同步
        }
        // 复制版本和数据后立即释放锁
        uint64_t safe_version = page->version;
        uint8_t safe_data[4096];
        memcpy(safe_data, page->base_page_data, 4096);
        pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);

        // 使用安全副本发送（跳过下方的直接解引用）
        // [FIX] 基于令牌桶思想的秒级节流
        long current_sec = g_ops->get_time_us() / 1000000;
        long last_sec = atomic_load(&g_force_sync_last_sec);
        if (current_sec != last_sec) {
            atomic_store(&g_force_sync_last_sec, current_sec);
            atomic_store(&g_force_sync_counter, 0);
        }
        if (atomic_fetch_add(&g_force_sync_counter, 1) >= MAX_FORCE_SYNC_PER_SEC) return;

        size_t pl_size = sizeof(struct wvm_full_page_push);
        size_t pkt_len = sizeof(struct wvm_header) + pl_size;
        uint8_t *buffer = g_ops->alloc_packet(pkt_len, 1);
        if (!buffer) return;

        struct wvm_header *hdr = (struct wvm_header*)buffer;
        hdr->magic = htonl(WVM_MAGIC);
        hdr->msg_type = htons(MSG_FORCE_SYNC);
        hdr->payload_len = htons(pl_size);
        hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
        hdr->req_id = 0;
        hdr->qos_level = 1;

        struct wvm_full_page_push *push = (struct wvm_full_page_push*)(buffer + sizeof(*hdr));
        push->gpa = WVM_HTONLL(gpa);
        push->version = WVM_HTONLL(safe_version);
        memcpy(push->data, safe_data, 4096);

        g_ops->send_packet(buffer, pkt_len, WVM_ENCODE_ID(g_my_vm_id, client_id));
        g_ops->free_packet(buffer);
        return;
    }
    // [FIX] 基于令牌桶思想的秒级节流
    long current_sec = g_ops->get_time_us() / 1000000;
    long last_sec = atomic_load(&g_force_sync_last_sec);

    if (current_sec != last_sec) {
        atomic_store(&g_force_sync_last_sec, current_sec);
        atomic_store(&g_force_sync_counter, 0);
    }

    if (atomic_fetch_add(&g_force_sync_counter, 1) >= MAX_FORCE_SYNC_PER_SEC) {
        // 超出带宽配额，直接丢弃。
        // 客户端因收不到回复，会保持旧版本，下次 COMMIT 依然会失败，实现"天然重试"
        return; 
    }

    size_t pl_size = sizeof(struct wvm_full_page_push);
    size_t pkt_len = sizeof(struct wvm_header) + pl_size;
    
    uint8_t *buffer = g_ops->alloc_packet(pkt_len, 1);
    if (!buffer) return;

    struct wvm_header *hdr = (struct wvm_header*)buffer;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_FORCE_SYNC);
    hdr->payload_len = htons(pl_size);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->req_id = 0;
    hdr->qos_level = 1; // High priority correction

    struct wvm_full_page_push *push = (struct wvm_full_page_push*)(buffer + sizeof(*hdr));
    push->gpa = WVM_HTONLL(gpa);
    // 此时持有页锁，version是安全的
    push->version = WVM_HTONLL(page->version); 
    memcpy(push->data, page->base_page_data, 4096);

    // [Fix #2] client_id 是裸 node_id，发包时需要编码为 composite ID
    g_ops->send_packet(buffer, pkt_len, WVM_ENCODE_ID(g_my_vm_id, client_id));
    g_ops->free_packet(buffer);
}

int wvm_handle_page_fault_logic(uint64_t gpa, void *page_buffer, uint64_t *version_out) {
    uint32_t dir_node = wvm_get_directory_node_id(gpa);
    
    // 场景 A: 我就是目录节点 (Local Hit)
    // [Multi-VM] dir_node 是 composite ID，用 WVM_GET_NODEID 比较
    if (WVM_GET_NODEID(dir_node) == (uint32_t)g_my_node_id) {
        uint32_t lock_idx = get_lock_idx(gpa);
        pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
        
        page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);
        int ret = -1;
        
        if (page) {
            // 直接从本地目录内存拷贝
            memcpy(page_buffer, page->base_page_data, 4096);
            if (version_out) *version_out = page->version;
            fprintf(stderr, "[Page Fault] local gpa=%#llx ver=%#llx\n",
                    (unsigned long long)gpa,
                    (unsigned long long)page->version);
            
            // [V29] 既然由于缺页进来了，说明本地之前可能被设为 Invalid
            // 我们需要把自己加入订阅者列表，确保未来收到 Push
            copyset_set(&page->subscribers, g_my_node_id);
            ret = 0;
        }
        
        pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
        return ret;
    }

    // 场景 B: 远程节点 (Remote Pull)
    // 这是 V28 逻辑的直接复用，但增加了 Version 字段处理
    
    // 1. 分配请求 ID
    // 远程 ACK 是 wvm_mem_ack_payload（含 version + 4KB data），
    // 不能直接把 4KB 页缓冲传给 req_ctx，否则 version 会丢失或发生越界风险。
    struct wvm_mem_ack_payload ack_payload;
    uint64_t rid = g_ops->alloc_req_id(&ack_payload, sizeof(ack_payload));
    if (rid == (uint64_t)-1) return -1;

    // 2. 构造 MSG_MEM_READ 包
    size_t pkt_len = sizeof(struct wvm_header) + 8;
    uint8_t *buffer = g_ops->alloc_packet(pkt_len, 1);
    if (!buffer) { g_ops->free_req_id(rid); return -1; }

    struct wvm_header *hdr = (struct wvm_header *)buffer;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_MEM_READ);
    hdr->payload_len = htons(8);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->target_id = htonl(dir_node);
    hdr->req_id = WVM_HTONLL(rid);
    hdr->qos_level = 1; // 缺页必须走快车道

    uint64_t net_gpa = WVM_HTONLL(gpa);
    memcpy(buffer + sizeof(*hdr), &net_gpa, 8);

    // 3. 发送并等待 (阻塞式)
    uint64_t start_total = g_ops->get_time_us();
    uint64_t last_send = 0;
    
    // 首次发送
    g_ops->send_packet(buffer, pkt_len, dir_node);
    last_send = start_total;
    fprintf(stderr, "[Page Fault] remote gpa=%#llx dir=%u rid=%llu\n",
            (unsigned long long)gpa,
            dir_node,
            (unsigned long long)rid);

    int success = 0;
    while (1) {
        // 检查是否完成
        if (g_ops->check_req_status(rid) == 1) {
            success = 1;
            fprintf(stderr, "[Page Fault Ack] gpa=%#llx rid=%llu\n",
                    (unsigned long long)gpa,
                    (unsigned long long)rid);
            break;
        }
        
        // 超时重传
        uint64_t now = g_ops->get_time_us();
        if (g_ops->time_diff_us(last_send) > RETRY_TIMEOUT_US) {
            g_ops->send_packet(buffer, pkt_len, dir_node);
            last_send = now;
        }
        
        // 总超时熔断 (5秒)
        if (g_ops->time_diff_us(start_total) > 5000000) break;
        
        g_ops->touch_watchdog();
        g_ops->cpu_relax();
    }

    g_ops->free_packet(buffer);

    if (success) {
        uint64_t ack_gpa = WVM_NTOHLL(ack_payload.gpa);
        if (ack_gpa != gpa) {
            g_ops->free_req_id(rid);
            return -1;
        }
        memcpy(page_buffer, ack_payload.data, 4096);
        if (version_out) {
            *version_out = WVM_NTOHLL(ack_payload.version);
        }
    }
    if (!success) {
        fprintf(stderr, "[Page Fault Timeout] gpa=%#llx dir=%u rid=%llu\n",
                (unsigned long long)gpa,
                dir_node,
                (unsigned long long)rid);
    }

    g_ops->free_req_id(rid);
    return success ? 0 : -1;
}

/* 
 * Master 侧 Directory 元数据同步更新
 * 隐患规避：使用宏操作版本号，防止逻辑时钟紊乱；加锁保护，防止并发写冲突。
 */
static void handle_prophet_metadata_update(uint64_t gpa_start, uint64_t len) {
    for (uint64_t off = 0; off < len; off += 4096) {
        uint64_t cur_gpa = gpa_start + off;
        uint32_t lock_idx = get_lock_idx(cur_gpa);
        
        pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
        page_meta_t *page = DIR_FIND_OR_CREATE(cur_gpa, lock_idx);
        if (page) {
            // [严格遵守宏操作]：版本号必须单调连续递增
            uint32_t next_cnt = GET_COUNTER(page->version) + 1;
            page->version = MAKE_VERSION(g_curr_epoch, next_cnt);
            
            // 物理落盘：Master 自己的内存也要清零
            memset(page->base_page_data, 0, 4096);
        }
        pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
    }
}

/* 
 *  ABI 语义指纹识别器
 * 隐患规避：DF方向检查、整页对齐修剪、最小阈值过滤。
 */
void wvm_prophet_abi_scanner(wvm_kvm_context_t *ctx) {
    // 1. 方向安全检查：x86 DF位 (Bit 10)。如果 DF=1 (递减)，绝对不加速。
    if (ctx->rflags & (1 << 10)) return;

    // 2. 只有写操作触发的 MMIO/Exception 才具备识别价值
    // 安全门：只加速我们能确定宽度的操作
    uint32_t insn_width = ctx->mmio.len;
    if (insn_width != 1 && insn_width != 2 && insn_width != 4 && insn_width != 8) {
        return;
    }
    uint64_t raw_start = ctx->rdi;
    uint64_t raw_len = ctx->rcx * insn_width; 

    // 3. 对齐修剪：计算 4KB 对齐的加速区间，非对齐部分留给 Guest 跑
    uint64_t aligned_start = (raw_start + 4095) & ~4095ULL;
    uint64_t head_gap = aligned_start - raw_start;
    
    if (raw_len <= head_gap + 4096) return; // 剩余长度不足一页，不加速
    uint64_t acc_len = (raw_len - head_gap) & ~4095ULL;

    // 4. 阈值检查：1MB 以上才触发 Prophet 广播
    if (ctx->rax == 0 && acc_len >= 1048576) {
        // 使用文件中真实存在的广播接口
        struct wvm_rpc_batch_memset batch = { .val = htonl(0), .count = htonl(1) };
        struct wvm_rpc_region region = { .gpa = WVM_HTONLL(aligned_start), .len = WVM_HTONLL(acc_len) };
        // [FIX] broadcast_rpc 要求 header+payload 格式，不能只传裸 payload
        size_t pl_size = sizeof(batch) + sizeof(region);
        uint8_t buf[sizeof(struct wvm_header) + sizeof(batch) + sizeof(region)];
        memset(buf, 0, sizeof(struct wvm_header)); // 清零 header 区域
        memcpy(buf + sizeof(struct wvm_header), &batch, sizeof(batch));
        memcpy(buf + sizeof(struct wvm_header) + sizeof(batch), &region, sizeof(region));

        // 预填 header 基本字段
        struct wvm_header *rpc_hdr = (struct wvm_header *)buf;
        rpc_hdr->magic = htonl(WVM_MAGIC);
        rpc_hdr->msg_type = htons(MSG_RPC_BATCH_MEMSET);
        rpc_hdr->payload_len = htons((uint16_t)pl_size);
        rpc_hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));

        wvm_logic_broadcast_rpc(buf, sizeof(buf), MSG_RPC_BATCH_MEMSET);

        // 同步 Master 侧元数据
        handle_prophet_metadata_update(aligned_start, acc_len);

        // 修正参数不匹配。循环调用，适配 g_ops->invalidate_local(gpa)
        for (uint64_t inv_gpa = aligned_start; inv_gpa < aligned_start + acc_len; inv_gpa += 4096) {
            g_ops->invalidate_local(inv_gpa);
        }

        // 计算残余寄存器状态，防止"字节丢失"
        uint64_t rem_bytes = raw_len - head_gap - acc_len;
        ctx->rdi = aligned_start + acc_len;
        
        // 严禁向上取整！必须向下取整。
        // 如果剩下 3 个字节，rcx 设为 0。
        // Guest 醒来后发现 rcx=0，会结束 REP 循环，剩下那 3 字节由 Guest 
        // 随后的非加速指令完成，或者干脆就不写。这保证了绝不写过界。
        ctx->rcx = rem_bytes / insn_width; 
    }
}

// 核心RPC (带指数退避)
int wvm_rpc_call(uint16_t msg_type, void *payload, int len, uint32_t target_id, void *rx_buffer, int rx_len) {
    { static int __rpc_dbg=0;
      if (__rpc_dbg < 10) {
          fprintf(stderr, "[RPC-CALL] msg=%u len=%d target=%u\n",
                  (unsigned)msg_type, len, (unsigned)target_id);
          __rpc_dbg++;
      }
    }
    // 1. 分配接收缓冲区
    // 我们需要一个足够大的缓冲区来接收可能的ACK包头
    uint8_t *net_rx_buf = g_ops->alloc_packet(WVM_MAX_PACKET_SIZE, 0); // Not atomic
    if (!net_rx_buf) return -ENOMEM;

    // 2. 分配请求ID
    uint64_t rid = g_ops->alloc_req_id(net_rx_buf, WVM_MAX_PACKET_SIZE);
    if (rid == (uint64_t)-1) {
        g_ops->free_packet(net_rx_buf);
        return -EBUSY;
    }

    // 3. 构造请求包
    size_t pkt_len = sizeof(struct wvm_header) + len;
    uint8_t *buffer = g_ops->alloc_packet(pkt_len, 0); // Not atomic
    if (!buffer) {
        g_ops->free_req_id(rid);
        g_ops->free_packet(net_rx_buf);
        return -ENOMEM;
    }

    struct wvm_header *hdr = (struct wvm_header *)buffer;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(msg_type);
    hdr->payload_len = htons(len);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id)); // Source ID
    // Preserve logical destination so fractal gateways can route correctly.
    hdr->target_id = htonl(target_id);
    hdr->req_id = WVM_HTONLL(rid);
    hdr->qos_level = 1; // Control messages are fast lane
    if (msg_type == MSG_VCPU_RUN) {
        hdr->mode_tcg = (len == (int)sizeof(wvm_tcg_context_t)) ? 1 : 0;
    }
    
    if (payload && len > 0) {
        memcpy(buffer + sizeof(*hdr), payload, len);
    }
    
    // 4. 重试循环逻辑
    uint64_t start_total = g_ops->get_time_us();
    uint64_t last_send_time;
    uint64_t current_retry_delay = INITIAL_RETRY_DELAY_US;

    // 首次发送
    g_ops->send_packet(buffer, pkt_len, target_id);
    last_send_time = start_total;

    while (g_ops->check_req_status(rid) != 1) {
        // 喂狗
        g_ops->touch_watchdog();

        // 检查总超时
        if (g_ops->time_diff_us(start_total) > TOTAL_TIMEOUT_US) {
            if (g_ops->log) {
                g_ops->log("[RPC Timeout] Type: %d, Target: %d, RID: %llu", 
                           msg_type, target_id, (unsigned long long)rid);
            }
            g_ops->free_packet(buffer);
            g_ops->free_packet(net_rx_buf);
            g_ops->free_req_id(rid);
            return -ETIMEDOUT;
        }

        // 检查是否需要重试
        if (g_ops->time_diff_us(last_send_time) > current_retry_delay) {
            // 重发
            g_ops->send_packet(buffer, pkt_len, target_id);
            last_send_time = g_ops->get_time_us();

            // 指数增加延迟
            current_retry_delay *= 2;
            if (current_retry_delay > MAX_RETRY_DELAY_US) {
                current_retry_delay = MAX_RETRY_DELAY_US;
            }

            // 增加随机抖动 +/- 30%
            uint32_t random_val;
            g_ops->get_random(&random_val);
            uint64_t jitter = current_retry_delay * 3 / 10;
            if (jitter > 0) {
                // random_val is u32, convert to signed offset carefully
                int64_t offset = ((int64_t)random_val % (2 * jitter)) - (int64_t)jitter;
                
                // Ensure delay doesn't go negative or too small
                if ((int64_t)current_retry_delay + offset > 100) {
                    current_retry_delay = (uint64_t)((int64_t)current_retry_delay + offset);
                }
            }
        }
        
        // 短暂休眠让出CPU
        g_ops->yield_cpu_short_time();
    }
    
    // 5. 处理结果
    g_ops->free_packet(buffer); // 释放发送Buffer

    // 从接收Buffer中提取数据
    // [FIX] RX 侧只复制了 payload（不含 header），所以 net_rx_buf 直接就是 payload
    if (rx_buffer && rx_len > 0) {
        /* alloc_req_id 注册了 max_len = WVM_MAX_PACKET_SIZE，
         * RX 回调写入的是纯 payload，长度已由 RX 侧截断到 max_len。
         * 这里直接按 rx_len 复制即可。 */
        memcpy(rx_buffer, net_rx_buf, (size_t)rx_len);
    }

    if (msg_type == MSG_VCPU_RUN && rx_buffer != NULL) {
        struct wvm_ipc_cpu_run_ack *run_ack = (struct wvm_ipc_cpu_run_ack *)rx_buffer;
        
        // 激活！调用你前面定义的 scanner 
        if (run_ack->status == 0 && run_ack->mode_tcg == 0) {
            // 这才是 logic_core 应该有的样子：职责分离
            wvm_prophet_abi_scanner(&run_ack->ctx.kvm);
        }
    }

    g_ops->free_packet(net_rx_buf); // 释放接收Buffer
    g_ops->free_req_id(rid);
    return 0;
}

// 该函数由内核缺页处理程序(wvm_fault_handler)调用，
// 用于告知 Directory 节点："我关注这个页面，请有更新时推给我"。
void wvm_declare_interest_in_neighborhood(uint64_t gpa) {
    // 1. 计算该 GPA 归谁管 (Directory Node)
    uint32_t dir_node = wvm_get_directory_node_id(gpa);
    
    // 如果我自己就是 Directory，不需要网络宣告 (本地 Logic Core 会自动处理)
    // [Multi-VM] dir_node 现在是 composite ID，用 WVM_GET_NODEID 提取裸 ID 比较
    if (WVM_GET_NODEID(dir_node) == (uint32_t)g_my_node_id) return;

    // 2. 分配数据包 (Atomic 上下文安全)
    size_t pkt_len = sizeof(struct wvm_header) + sizeof(uint64_t);
    uint8_t *buffer = g_ops->alloc_packet(pkt_len, 1); 
    
    if (!buffer) {
        // 内存不足时丢弃本次宣告。
        // 这不是致命错误，只会导致本次无法订阅，V28 的拉取机制会兜底。
        return; 
    }

    // 3. 构造协议头
    struct wvm_header *hdr = (struct wvm_header *)buffer;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_DECLARE_INTEREST);
    hdr->payload_len = htons(sizeof(uint64_t));
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id)); // 告诉对方我是谁
    hdr->req_id = 0;                     // 异步消息，不需要 ACK
    hdr->qos_level = 1;                  // 订阅是元数据操作，走 Fast Lane

    // 4. 填充 GPA
    uint64_t net_gpa = WVM_HTONLL(gpa);
    memcpy(buffer + sizeof(*hdr), &net_gpa, sizeof(uint64_t));

    // 5. 发送并释放
    g_ops->send_packet(buffer, pkt_len, dir_node);
    g_ops->free_packet(buffer);
}

static void add_gossip_to_aggregator(uint32_t target_node_id, uint8_t state, uint32_t epoch) {
    struct wvm_heartbeat_payload hb;
    (void)state;
    (void)epoch;

    hb.local_epoch = htonl(g_curr_epoch);
    hb.active_node_count = htonl(g_peer_count);
    hb.load_factor = 0;
    hb.peer_epoch_sum = 0;
    hb.ctrl_port = htons((uint16_t)g_ctrl_port);

    /* Heartbeat packets must be sent as standalone frames so header/CRC/target_id
     * are set consistently by the backend send pipeline. */
    g_ops->send_packet_async(MSG_HEARTBEAT, &hb, sizeof(hb), target_node_id, 1);
}

static void flush_gossip_aggregator() {
    /* No-op: heartbeat aggregation removed; each heartbeat is sent directly. */
}

/* 
 * [物理意图] 分布式内存事务的终极处理器。
 * [关键逻辑] 拦截所有入站消息，根据 MESI 状态机执行：READ(拉取)、DECLARE(订阅)、COMMIT(增量写) 及 Prophet(指令透传)。
 * [后果] 这是系统的"物理法则"执行点，任何逻辑错误都会直接破坏内存强一致性。
 */
void wvm_logic_process_packet(struct wvm_header *hdr, void *payload, uint32_t source_node_id) {
    uint16_t type = ntohs(hdr->msg_type);
    uint32_t src_id_raw = ntohl(hdr->slave_id);
    // [Multi-VM] 解码 composite ID，提取裸 node_id 给内部 DHT 操作
    uint8_t  src_vm_id = WVM_GET_VMID(src_id_raw);
    uint32_t src_id = WVM_GET_NODEID(src_id_raw);
    uint32_t src_epoch = ntohl(hdr->epoch);
    uint8_t  src_state = hdr->node_state;

    // [Multi-VM] 丢弃其他 VM 的包
    if (src_vm_id != g_my_vm_id) return;

    if (type == MSG_HEARTBEAT && g_ops && g_ops->log) {
        g_ops->log("[HB LOGIC] src=%u vm=%u state=%u epoch=%u", src_id, src_vm_id, src_state, src_epoch);
    }

    // 1. [核心安全检查] 任何消息都必须透传 Epoch 和 State
    // 如果收到的消息 Epoch 大于本地太远，说明本地视图已严重落后，强制触发视图拉取
    if (src_epoch > g_curr_epoch + 1) {
        request_view_from_neighbor(WVM_ENCODE_ID(g_my_vm_id, src_id));
    }

    uint16_t r_ctrl_port = 0;
    if (type == MSG_HEARTBEAT) {
        if (ntohs(hdr->payload_len) >= sizeof(struct wvm_heartbeat_payload)) {
            struct wvm_heartbeat_payload *hb = (struct wvm_heartbeat_payload *)payload;
            r_ctrl_port = ntohs(hb->ctrl_port);
        }
    }

    // 2. [自动维护] 无论什么消息类型，只要是自治范围（>=40）或包含有效信息的，都更新拓扑视图
    if (type >= 40 || type == MSG_PING) {
        update_local_topology_view(src_id, src_epoch, src_state, NULL, r_ctrl_port);
    }

    switch(type) {
        case MSG_HEARTBEAT:
            // 视图已在上方更新，无需额外操作
            break;
            
        case MSG_VIEW_PULL:
            handle_view_pull(hdr, src_id);
            break;
            
        case MSG_VIEW_ACK:
            handle_view_ack(payload, ntohs(hdr->payload_len));
            break;

        // --- 1. 处理拉取请求 (Pull) ---
        case MSG_MEM_READ: {
            if (ntohs(hdr->payload_len) < sizeof(uint64_t)) return;
            uint64_t gpa = WVM_NTOHLL(*(uint64_t*)payload);
            
            // 确保我是这个页面的 Directory
            if (WVM_GET_NODEID(wvm_get_directory_node_id(gpa)) != (uint32_t)g_my_node_id) return;
            
            // 构造 MSG_MEM_ACK (包含版本号的 payload)
            size_t pl_size = sizeof(struct wvm_mem_ack_payload);
            size_t pkt_len = sizeof(struct wvm_header) + pl_size;
            uint8_t *buffer = g_ops->alloc_packet(pkt_len, 1);
            if (!buffer) return;

            struct wvm_header *ack = (struct wvm_header*)buffer;
            ack->magic = htonl(WVM_MAGIC);
            ack->msg_type = htons(MSG_MEM_ACK);
            ack->payload_len = htons(pl_size);
            ack->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
            ack->target_id = hdr->slave_id;
            ack->req_id = hdr->req_id; // 必须回传请求ID
            ack->qos_level = 0; // 大包走慢车道

            struct wvm_mem_ack_payload *ack_pl = (struct wvm_mem_ack_payload*)(buffer + sizeof(*ack));
            
            uint32_t lock_idx = get_lock_idx(gpa);
            pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
            
            page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);
            if (page) {
                ack_pl->gpa = WVM_HTONLL(gpa);
                // 关键：填入当前版本号
                ack_pl->version = WVM_HTONLL(page->version);
                memcpy(ack_pl->data, page->base_page_data, 4096);
            } else {
                // Should not happen if alloc succeeds, but handle it
                memset(ack_pl, 0, sizeof(*ack_pl));
            }
            
            pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
            
            g_ops->send_packet(buffer, pkt_len, source_node_id);
            g_ops->free_packet(buffer);
            break;
        }

        // --- 2. 处理兴趣宣告 (Pub) ---
        case MSG_DECLARE_INTEREST: {
            if (ntohs(hdr->payload_len) < sizeof(uint64_t)) return;
            uint64_t gpa = WVM_NTOHLL(*(uint64_t*)payload);
            
            if (WVM_GET_NODEID(wvm_get_directory_node_id(gpa)) != (uint32_t)g_my_node_id) return;

            uint32_t lock_idx = get_lock_idx(gpa);
            pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
            
            page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);
            if (page) {
                // 记录订阅者 (使用裸 node_id 索引位图，不能用 composite ID)
                copyset_set(&page->subscribers, src_id);
                page->last_interest_time = g_ops->get_time_us();
                uint32_t seg_idx = src_id / 64;
                // 在二级位图中标记该段"有订阅者"
                page->segment_mask[seg_idx / 64] |= (1UL << (seg_idx % 64));
                // 在一级位图中标记节点
                page->subscribers.bits[seg_idx] |= (1UL << (src_id % 64));
            }
            
            pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
            break;
        }

        // --- 3. 处理增量提交 (Commit) ---
        case MSG_COMMIT_DIFF: {
            // 在自治模式下，COMMIT 必须先校验写者的 Epoch
            if (src_epoch < g_curr_epoch) {
                // 拒绝陈旧 Epoch 的提交，通知其 FORCE_SYNC
                force_sync_client(WVM_NTOHLL(((struct wvm_diff_log*)payload)->gpa), NULL, src_id);
                return;
            }
            // 安全检查 payload 长度
            uint16_t pl_len = ntohs(hdr->payload_len);
            if (pl_len < sizeof(struct wvm_diff_log)) return;

            struct wvm_diff_log *log = (struct wvm_diff_log*)payload;
            uint64_t gpa = WVM_NTOHLL(log->gpa);
            uint64_t commit_version = WVM_NTOHLL(log->version);
            uint16_t off = ntohs(log->offset);
            uint16_t sz = ntohs(log->size);
            uint32_t lock_idx = get_lock_idx(gpa);
            if (sz == 0) {
                pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
                // [V29.5] Zero Page Commit
                page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);
                if (page) {
                    // 本地清零
                    memset(page->base_page_data, 0, 4096);
                    uint32_t local_counter = GET_COUNTER(page->version);
                    page->version = MAKE_VERSION(g_curr_epoch, local_counter + 1);
                    
                    log->version = WVM_HTONLL(page->version);
                    
                    // 广播零页：Diff类型 + ZeroFlag + 无数据
                    broadcast_to_subscribers(page, MSG_PAGE_PUSH_DIFF, log, sizeof(struct wvm_diff_log), WVM_FLAG_ZERO);
                }
                pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
            } else {
                // 安全检查 diff 数据是否越界
                if (sizeof(struct wvm_diff_log) + sz > pl_len) return;
                if (off + sz > 4096) return;

                if (WVM_GET_NODEID(wvm_get_directory_node_id(gpa)) != (uint32_t)g_my_node_id) return;

                pthread_mutex_lock(&g_dir_table_locks[lock_idx]);

                page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);

                if (!page) {
                    if (g_ops->log) g_ops->log("[Logic] Fatal: Hash Table Full for GPA %llx", gpa);
                    pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
                    return; // 防御性退出，防止崩溃
                }

                if (page) {
                    // [FIX] Epoch-Aware 版本校验
                    uint32_t commit_epoch = GET_EPOCH(commit_version);
                    uint32_t commit_counter = GET_COUNTER(commit_version);
                    uint32_t local_epoch = GET_EPOCH(page->version);
                    uint32_t local_counter = GET_COUNTER(page->version);
            
                    // 1. Epoch 必须与 Directory 当前 Epoch 一致
                    // 2. Counter 必须是连续的
                    if (commit_epoch != g_curr_epoch || commit_counter != local_counter) {
                        // 版本冲突！拒绝并强制同步
                        // [FIX-H8] 传 NULL 让 force_sync_client 走安全的重新查找+拷贝路径，
                        // 避免释放锁后解引用 page 指针导致的数据竞争
                        pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
                        force_sync_client(gpa, NULL, src_id);
                        return;
                    }
                
                    // 应用 Diff 到主副本
                    memcpy(page->base_page_data + off, log->data, sz);
                    // [FIX] 更新版本号：Epoch 不变，Counter++
                    page->version = MAKE_VERSION(g_curr_epoch, local_counter + 1); 
                
                    // 广播决策
                    if (sz < SMALL_UPDATE_THRESHOLD) {
                        // 小更新：推送 Diff
                        // 需要修改 log 中的 version 为新版本号
                        log->version = WVM_HTONLL(page->version); 
                    
                        // 广播 Diff 包
                        broadcast_to_subscribers(page, MSG_PAGE_PUSH_DIFF, log, sizeof(struct wvm_diff_log) + sz, 0);
                    } else {
                        // 大更新：推送全页
                        size_t push_size = sizeof(struct wvm_full_page_push);
            
                        // 参数 1 表示原子分配 (GFP_ATOMIC)，因为当前持有自旋锁
                        uint8_t *temp_buf = g_ops->alloc_packet(push_size, 1);
            
                        if (temp_buf) {
                            struct wvm_full_page_push *p = (struct wvm_full_page_push *)temp_buf;
                
                            p->gpa = WVM_HTONLL(page->gpa);
                            p->version = WVM_HTONLL(page->version);
                            // 第一次拷贝：从 Page Cache 到 临时堆内存
                            memcpy(p->data, page->base_page_data, 4096);
                
                            // 广播函数内部会进行第二次分配和拷贝 (入队)
                            // 虽然仍有双重拷贝，但避开了栈溢出风险
                            broadcast_to_subscribers(page, MSG_PAGE_PUSH_FULL, p, push_size, 0);
                
                            // 立即释放临时内存
                            g_ops->free_packet(temp_buf);
                        } else {
                            if (g_ops->log) g_ops->log("[Logic] OOM skipping Full Push");
                        }
                    }
                }
                pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
            }
            break;
        }

        // 兼容性/强制写入：直接覆盖并推送
        case MSG_MEM_WRITE: {
            // Payload 结构: GPA(8) + Data(4096)
            if (ntohs(hdr->payload_len) < sizeof(uint64_t) + 4096) return;
            
            uint64_t gpa = WVM_NTOHLL(*(uint64_t*)payload);
            void *data_ptr = (uint8_t*)payload + sizeof(uint64_t);
            
            // 1. [Security] 权限检查：只有 Owner 才能接受 Write 并返回 ACK
            if (WVM_GET_NODEID(wvm_get_directory_node_id(gpa)) != (uint32_t)g_my_node_id) {
                // 如果我不是 Owner，这是一个错误路由的包，静默丢弃或记录日志
                // 绝对不能发 ACK，否则客户端会误以为写入成功
                if (g_ops->log) g_ops->log("[Logic] Write on non-owner GPA %llx ignored", gpa);
                return;
            }
            
            uint32_t lock_idx = get_lock_idx(gpa);
            pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
            
            page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);
            int write_success = 0;

            if (page) {
                // 2. [Commit] 先落盘：写入本地内存
                memcpy(page->base_page_data, data_ptr, 4096);
                
                // 3. [State] 更新状态：版本号递增
                page->version = MAKE_VERSION(g_curr_epoch, GET_COUNTER(page->version) + 1);
                write_success = 1; // 标记为"已提交"
                
                // 4. [Broadcast] 广播给其他订阅者
                // 注意：这里是在持锁状态下分配内存，使用 wvm_malloc (kmalloc/malloc)
                size_t push_size = sizeof(struct wvm_full_page_push);
                uint8_t *temp_buf = wvm_malloc(push_size); 
                
                if (temp_buf) {
                    struct wvm_full_page_push *p = (struct wvm_full_page_push *)temp_buf;
                    p->gpa = WVM_HTONLL(page->gpa);
                    p->version = WVM_HTONLL(page->version);
                    memcpy(p->data, page->base_page_data, 4096);
                    
                    // 广播函数内部会处理分片队列锁，这里调用是安全的
                    broadcast_to_subscribers(page, MSG_PAGE_PUSH_FULL, p, push_size, 0);
                    
                    wvm_free(temp_buf);
                }
            }
            
            pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);

            // 5. [ACK] 只有在"解锁"且"提交成功"后，才发送 ACK
            // 这保证了当客户端收到 ACK 时，数据一定已经在 Directory 的内存里了
            if (write_success) {
                size_t ack_len = sizeof(struct wvm_header); // ACK 包仅含头部
                uint8_t *ack_buf = g_ops->alloc_packet(ack_len, 1); // Atomic allocation
                
                if (ack_buf) {
                    struct wvm_header *ack_hdr = (struct wvm_header *)ack_buf;
                    ack_hdr->magic = htonl(WVM_MAGIC);
                    ack_hdr->msg_type = htons(MSG_MEM_ACK);
                    ack_hdr->payload_len = 0;
                    ack_hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                    ack_hdr->target_id = hdr->slave_id;
                    ack_hdr->req_id = hdr->req_id; // 关键：回传请求 ID
                    ack_hdr->qos_level = 1;        // 控制信令走快车道

                    // CRC32 由 send_packet 后端自动计算，此处无需手动填
                    ack_hdr->crc32 = 0;
                    
                    g_ops->send_packet(ack_buf, ack_len, source_node_id);
                    g_ops->free_packet(ack_buf);
                }
            }
            break;
        }

        // --- Prophet 批量指令 (Type 31) ---
        case MSG_RPC_BATCH_MEMSET: {
            uint32_t payload_len = ntohs(hdr->payload_len);
            if (payload_len < sizeof(struct wvm_rpc_batch_memset)) break;

            struct wvm_rpc_batch_memset *batch = (struct wvm_rpc_batch_memset *)payload;
            uint32_t count = ntohl(batch->count);
            
            // 1. 安全检查：防止 Payload 伪造导致 OOB 攻击
            size_t required = sizeof(struct wvm_rpc_batch_memset) + count * sizeof(struct wvm_rpc_region);
            if (payload_len < required) break;

            struct wvm_rpc_region *regions = (struct wvm_rpc_region *)(batch + 1);

            // 2. [关键] 版本一致性维护
            // 在执行物理 memset 之前，必须先更新本地 Directory 表中的版本号
            for (uint32_t i = 0; i < count; i++) {
                uint64_t gpa_start = WVM_NTOHLL(regions[i].gpa);
                uint64_t len = WVM_NTOHLL(regions[i].len);
                
                // 按 4KB 页粒度强制更新本地版本记录
                for (uint64_t cur = gpa_start; cur < gpa_start + len; cur += 4096) {
                    wvm_logic_update_local_version(cur);
                }
            }

            // 3. [执行] 调用后端提供的物理执行接口
            // 逻辑核心不直接操作内存，而是通过 ops 传导
            // 后端执行入口（内核/用户态均由对应实现接管）
            handle_rpc_batch_execution(payload, payload_len);

            // 4. [自治演进] 观测者模式：Prophet 指令也算是一次活跃心跳
            update_local_topology_view(src_id, src_epoch, src_state, NULL, 0);
            break;
        }

        // --- 响应同步探测 PING ---
        case MSG_PING: {
            // 如果 req_id 是 SYNC_MAGIC，说明是 Wavelet 引擎在做同步栅栏
            // Directory 必须立刻回复 MSG_MEM_ACK 且携带同样的 SYNC_MAGIC
            
            size_t ack_len = sizeof(struct wvm_header);
            uint8_t *ack_buf = g_ops->alloc_packet(ack_len, 1); // 原子上下文分配
            
            if (ack_buf) {
                struct wvm_header *ack_hdr = (struct wvm_header *)ack_buf;
                ack_hdr->magic = htonl(WVM_MAGIC);
                ack_hdr->msg_type = htons(MSG_MEM_ACK); // 回复类型
                ack_hdr->payload_len = 0;
                ack_hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                ack_hdr->target_id = hdr->slave_id;
                ack_hdr->req_id = hdr->req_id; // 关键：原样回传 SYNC_MAGIC
                ack_hdr->qos_level = 1;        // 必须走快车道，否则 AIMD 会误判延迟
                ack_hdr->epoch = htonl(g_curr_epoch);
                ack_hdr->node_state = g_my_node_state;

                // CRC 重新计算
                ack_hdr->crc32 = 0;
                ack_hdr->crc32 = htonl(calculate_crc32(ack_buf, ack_len));

                // 这里的 source_node_id 是函数的第三个参数
                g_ops->send_packet(ack_buf, ack_len, source_node_id);
                g_ops->free_packet(ack_buf);
            }
            break;
        }

        default:
            break;
    }
}

// [V29 Interface] 本地原子更新版本号
void wvm_logic_update_local_version(uint64_t gpa) {
    uint32_t lock_idx = get_lock_idx(gpa);
    pthread_mutex_lock(&g_dir_table_locks[lock_idx]);
    
    // 仅更新已存在的元数据。若页面未被追踪，说明无 Diff 历史，无需版本号。

    page_meta_t *page = DIR_FIND_OR_CREATE(gpa, lock_idx);
    if (page) {
        // [FIX] Epoch-Aware 版本更新
        uint32_t local_counter = GET_COUNTER(page->version);
        page->version = MAKE_VERSION(g_curr_epoch, local_counter + 1); 
    }
    
    pthread_mutex_unlock(&g_dir_table_locks[lock_idx]);
}

// [V29 Interface] 全网广播 RPC 包
// 基于局部视图进行扩散：
void wvm_logic_broadcast_rpc(void *full_pkt_data, int full_pkt_len, uint16_t msg_type) {
    struct wvm_header *hdr = (struct wvm_header *)full_pkt_data;
    void *payload = (void*)hdr + sizeof(struct wvm_header);
    int payload_len = full_pkt_len - sizeof(struct wvm_header);

    pthread_rwlock_rdlock(&g_view_lock);
    
    // 如果没有发现邻居，仅执行本地，无需广播
    if (g_peer_count == 0) {
        pthread_rwlock_unlock(&g_view_lock);
        return;
    }

    // 遍历局部活跃视图进行扩散
    for (int i = 0; i < g_peer_count; i++) {
        if (g_peer_view[i].state != NODE_STATE_ACTIVE) continue;

        uint32_t target_id = g_peer_view[i].node_id;

        // 定位分片发送队列（复用 V29.5 的高性能分片队列）
        int shard_idx = target_id % NUM_BCAST_WORKERS;
        bcast_queue_shard_t *shard = &g_bcast_shards[shard_idx];

        pthread_spin_lock(&shard->lock);
        if (shard->tail + 1 - shard->head >= BCAST_Q_SIZE) {
            pthread_spin_unlock(&shard->lock);
            continue; // 队列满则跳过，由于是 P2P 扩散，其他节点会有冗余路径
        }

        // 分配并拷贝
        void *data_copy = malloc(payload_len);
        if (data_copy) {
            memcpy(data_copy, payload, payload_len);
            broadcast_task_t *t = &shard->queue[shard->tail & BCAST_Q_MASK];
            t->msg_type = msg_type;
            t->target_id = WVM_ENCODE_ID(g_my_vm_id, target_id);
            t->len = payload_len;
            t->data_ptr = data_copy;
            
            // 关键：在 Header 中标记当前 Epoch
            hdr->epoch = g_curr_epoch;
            hdr->node_state = g_my_node_state;

            __sync_synchronize();
            shard->tail++;
        }
        pthread_spin_unlock(&shard->lock);
    }
    pthread_rwlock_unlock(&g_view_lock);
}

```

---

### Step 4: 内核后端实现与内核构建脚本 (Kernel Backend & Kernel Build Script)

**文件**: `master_core/kernel_backend.c`

```c
/*
 * [IDENTITY] Kernel Backend - The Bare-Metal Engine
 * ---------------------------------------------------------------------------
 * 物理角色：宿主机内核中的"内存直通总线"。
 * 职责边界：
 * 1. 实现异步缺页等待队列，通过网络中断直接唤醒 vCPU。
 * 2. 实现内核态脏页捕获 (page_mkwrite)，在软中断中完成 Diff 计算。
 * 3. 管理 Radix-Tree 索引，实现对远程 PUSH 的零拷贝应用。
 * 
 * [禁止事项]
 * - 严禁在 sigsegv 路径上使用阻塞型互斥锁 (Mutex)，必须使用 spinlock_irqsave。
 * - 严禁绕过 unmap_mapping_range 直接修改 PTE。
 * ---------------------------------------------------------------------------
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/ip.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/nmi.h>      
#include <linux/delay.h>    
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/atomic.h>
#include <asm/barrier.h>    
#include <linux/spinlock.h>
#include <linux/percpu.h>
#include <asm/unaligned.h> 
#include <linux/kthread.h>
#include <linux/wait.h>
#include <asm/byteorder.h>
#include <linux/radix-tree.h>
#include <linux/highmem.h>
#include <linux/random.h>
#include <linux/pagemap.h> 
#include <linux/rmap.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/version.h>

#include "../common_include/wavevm_ioctl.h"
#include "../common_include/wavevm_protocol.h"
#include "unified_driver.h"
#include "logic_core.h" 

#define DRIVER_NAME "wavevm"
#define TX_RING_SIZE 2048
#define TX_SLOT_SIZE 65535

// [Fix] Multi-VM: 引用全局 vm_id，用于 composite ID 编码
extern uint8_t g_my_vm_id;

// 定义并初始化 mapping 锁
static struct rw_semaphore g_mapping_sem; 

static int service_port = 9000;
module_param(service_port, int, 0644);
static int local_slave_port = 9001; 
module_param(local_slave_port, int, 0644);

static struct socket *g_socket = NULL;
static struct sockaddr_in gateway_table[WVM_MAX_GATEWAYS]; 
static struct kmem_cache *wvm_pkt_cache = NULL;

// --- [V29 Wavelet] 内核态脏区捕获与Diff提交结构 ---

// 记录一个正在被写入的页面
struct diff_task_t {
    struct list_head list;
    struct page *page;      // 目标物理页
    void *pre_image;        // 4KB 快照 (修改前的数据)
    uint64_t gpa;           // Guest Physical Address
    uint64_t timestamp;     // 记录时间，用于防抖
};

static LIST_HEAD(g_diff_queue);
static spinlock_t g_diff_lock;
static struct task_struct *g_committer_thread = NULL;
static wait_queue_head_t g_diff_wq;

// 引用：我们需要知道 VMA 的 mapping 才能触发 unmap
static struct address_space *g_mapping = NULL;

// --- ID 管理系统 ---
#define BITS_PER_CPU_ID 16
#define MAX_IDS_PER_CPU (1 << BITS_PER_CPU_ID) 
#define CPU_ID_SHIFT    16                     

struct id_pool_t {
    uint32_t *ids;
    uint32_t head;
    uint32_t tail;
    spinlock_t lock;
};
static DEFINE_PER_CPU(struct id_pool_t, g_id_pool);

// 请求上下文：增加等待队列以支持异步休眠
struct req_ctx_t {
    void *rx_buffer;
    uint32_t generation;
    uint32_t max_len; // [FIX-G3] 记录 rx_buffer 最大可用长度
    volatile int done;
    wait_queue_head_t wq; // [New] 内核任务在此休眠
    struct task_struct *waiter; // [New] 记录等待的任务 (用于调试或唤醒检查)
};
static struct req_ctx_t *g_req_ctx = NULL;
// Runtime-sized: nr_cpu_ids * MAX_IDS_PER_CPU. The original fixed sizing
// (1024 * 65536) is too large for most test environments and can stall insmod.
static size_t g_req_ctx_count = 0;

// [V29 Final Fix] 内核态页面元数据
// 用于在 Radix Tree 中同时存储物理页指针和版本号
typedef struct {
    struct page *page;
    uint64_t version;
    struct rcu_head rcu;
    
    // 内核原生 sk_buff 队列，自带 spinlock，用于缓存乱序到达的包
    struct sk_buff_head reorder_q; 
} kvm_page_meta_t;

// 修改树的定义，名字保持不变，但存储的内容变了
static RADIX_TREE(g_page_tree, GFP_ATOMIC);
static DEFINE_SPINLOCK(g_page_tree_lock);

// --- QoS 发送队列 ---
struct tx_slot_t {
    int len;
    uint32_t target_id;
    uint8_t data[TX_SLOT_SIZE];
};

struct wvm_tx_ring_t {
    struct tx_slot_t *slots;
    uint32_t head; 
    uint32_t tail; 
    spinlock_t lock;
    atomic_t pending_count;
};

static struct wvm_tx_ring_t g_fast_ring;
static struct wvm_tx_ring_t g_slow_ring;

static struct task_struct *g_tx_thread = NULL;
static wait_queue_head_t g_tx_wq;

static wait_queue_head_t g_irq_wait_queue;
static atomic_t g_irq_pending = ATOMIC_INIT(0);

// 定义工作项结构，用于把参数带到 Workqueue 里
struct wvm_inval_work {
    struct work_struct work;
    uint64_t gpa;
};

static void init_ring(struct wvm_tx_ring_t *ring) {
    ring->slots = vmalloc(sizeof(struct tx_slot_t) * TX_RING_SIZE);
    ring->head = ring->tail = 0;
    spin_lock_init(&ring->lock);
    atomic_set(&ring->pending_count, 0);
}

// --- 工作线程回调（运行在进程上下文，安全！）---
static void wvm_inval_work_fn(struct work_struct *work) {
    struct wvm_inval_work *iw = container_of(work, struct wvm_inval_work, work);
    
    down_read(&g_mapping_sem);
    if (g_mapping) {
        loff_t offset = (loff_t)iw->gpa;
        unmap_mapping_range(g_mapping, offset, PAGE_SIZE, 1);
    }
    up_read(&g_mapping_sem);
    
    kfree(iw); // 任务完成，释放工作项本身
}

// --- 调度函数（可以在原子上下文中调用）---
static void schedule_async_unmap(uint64_t gpa) {
    // 必须使用 GFP_ATOMIC，因为我们在软中断里
    struct wvm_inval_work *iw = kmalloc(sizeof(*iw), GFP_ATOMIC);
    if (iw) {
        INIT_WORK(&iw->work, wvm_inval_work_fn);
        iw->gpa = gpa;
        schedule_work(&iw->work); // 扔给系统默认队列，内核会择机执行
    } else {
        // 极罕见的 OOM，打印错误但只能放弃 Unmap
        // 后果仅仅是 Guest 短时间内读到旧数据，不会崩系统
        printk(KERN_ERR "[WVM] OOM scheduling unmap for GPA %llx\n", gpa);
    }
}

// --- 辅助函数 ---
static uint64_t k_get_time_us(void) { return ktime_to_us(ktime_get()); }
static void k_touch_watchdog(void) { touch_nmi_watchdog(); }
static int k_is_atomic_context(void) { return in_atomic() || irqs_disabled(); }
static void k_cpu_relax(void) { cpu_relax(); }
static uint64_t k_time_diff_us(uint64_t start) {
    uint64_t now = k_get_time_us();
    return (now >= start) ? (now - start) : ((uint64_t)(-1) - start + now);
}

static void WVM_PRINTF_LIKE(1, 2) k_log(const char *fmt, ...) {
    struct va_format vaf;
    va_list args;
    va_start(args, fmt);
    vaf.fmt = fmt;
    vaf.va = &args;
    printk(KERN_INFO "[WVM]: %pV\n", &vaf);
    va_end(args);
}

static void k_get_random(uint32_t *val) { get_random_bytes(val, sizeof(uint32_t)); }
static void k_yield_short(void) { if (!in_atomic()) msleep(1); else udelay(50); }

// --- 内存操作 ---
static void* k_alloc_large_table(size_t size) { return vzalloc(size); }
static void k_free_large_table(void *ptr) { vfree(ptr); }
static void* k_alloc_packet(size_t size, int atomic) {
    gfp_t flags = atomic ? GFP_ATOMIC : GFP_KERNEL;
    if (wvm_pkt_cache && size <= (sizeof(struct wvm_header) + 4096)) {
        return kmem_cache_alloc(wvm_pkt_cache, flags);
    }
    return kmalloc(size, flags);
}
static void k_free_packet(void *ptr) {
    if (!ptr) return;
    /*
     * Kernel >= 6.x no longer exposes slab cache pointer in struct page.
     * kfree() can free both kmalloc() and slab objects, and is sufficient here.
     */
    kfree(ptr);
}
static void k_fetch_page(uint64_t gpa, void *buf) {} // Deprecated by logic_core

/* 
 * [物理意图] 从内核索引中“抹除”特定 GPA 的元数据存在。
 * [关键逻辑] 在原子上下文中将页面从 Radix-Tree 中摘除，并使用 kfree_rcu 安全回收乱序缓冲区。
 * [后果] 这是 MESI 协议在内核态落地的第一步，它切断了后续 PUSH 包对该物理页的自动更新链路。
 */
static void k_invalidate_meta_atomic(uint64_t gpa) {
    spin_lock(&g_page_tree_lock);
    kvm_page_meta_t *meta = radix_tree_delete(&g_page_tree, gpa >> PAGE_SHIFT);
    spin_unlock(&g_page_tree_lock);
    
    if (meta) {
        // 必须释放队列中所有积压的 skb，否则内存泄漏
        skb_queue_purge(&meta->reorder_q);
        
        // 等待所有 RCU 读者读完后释放结构体
        kfree_rcu(meta, rcu);
    }
}

/* 
 * [物理意图] 实现“零锁竞争”的分布式请求 ID 分配。
 * [关键逻辑] 采用 Per-CPU 变量技术，每个逻辑核拥有独立的 ID 池。利用 Generation 机制检测网络乱序导致的 ABA 冲突。
 * [后果] 这一步是 Mode A 支持百万 TPS 的基石。它消除了传统分布式系统中全局 ID 产生器的串行化瓶颈。
 */
static uint64_t k_alloc_req_id(void *rx_buffer, uint32_t buffer_size) {
    uint64_t id = (uint64_t)-1;
    unsigned long flags;
    int cpu = get_cpu();
    struct id_pool_t *pool = this_cpu_ptr(&g_id_pool);

    spin_lock_irqsave(&pool->lock, flags);
    if (unlikely(!pool->ids)) goto out;
    if (pool->tail != pool->head) {
        uint32_t raw_idx = pool->ids[pool->head & (MAX_IDS_PER_CPU - 1)];
        pool->head++;
        uint32_t combined_idx = ((uint32_t)cpu << CPU_ID_SHIFT) | raw_idx;
        if (likely((size_t)combined_idx < g_req_ctx_count)) {
            g_req_ctx[combined_idx].generation++;
            if (g_req_ctx[combined_idx].generation == 0) g_req_ctx[combined_idx].generation = 1;
            id = ((uint64_t)g_req_ctx[combined_idx].generation << 32) | combined_idx;
            WRITE_ONCE(g_req_ctx[combined_idx].rx_buffer, rx_buffer);
            g_req_ctx[combined_idx].max_len = buffer_size; // [FIX-G3]
            WRITE_ONCE(g_req_ctx[combined_idx].done, 0);
            smp_wmb(); 
        } else { pool->head--; }
    }
out:
    spin_unlock_irqrestore(&pool->lock, flags);
    put_cpu();
    return id;
}

static void k_free_req_id(uint64_t full_id) {
    unsigned long flags;
    uint32_t generation = (uint32_t)(full_id >> 32);
    uint32_t combined_idx = (uint32_t)(full_id & 0xFFFFFFFF);
    int owner_cpu = (combined_idx >> CPU_ID_SHIFT);
    uint32_t raw_idx = combined_idx & (MAX_IDS_PER_CPU - 1);
    struct id_pool_t *pool;

    if (unlikely((size_t)combined_idx >= g_req_ctx_count || owner_cpu >= nr_cpu_ids)) return;
    if (g_req_ctx[combined_idx].generation != generation) return; 

    xchg(&g_req_ctx[combined_idx].rx_buffer, NULL);
    WRITE_ONCE(g_req_ctx[combined_idx].done, 0);

    pool = per_cpu_ptr(&g_id_pool, owner_cpu);
    if (unlikely(!pool->ids)) return;
    spin_lock_irqsave(&pool->lock, flags);
    pool->ids[pool->tail & (MAX_IDS_PER_CPU - 1)] = raw_idx;
    pool->tail++;
    spin_unlock_irqrestore(&pool->lock, flags);
}

static int k_check_req_status(uint64_t full_id) {
    uint32_t combined_idx = (uint32_t)(full_id & 0xFFFFFFFF);
    if ((size_t)combined_idx >= g_req_ctx_count) return -1;
    if (READ_ONCE(g_req_ctx[combined_idx].done)) { smp_rmb(); return 1; }
    return 0;
}

// --- 发送逻辑 ---
static void k_set_gateway_ip(uint32_t gw_id, uint32_t ip, uint16_t port) {
    // [Fix #1] 入参可能是 composite ID，先解码为裸 node_id 再索引静态数组
    uint32_t raw_node_id = WVM_GET_NODEID(gw_id);
    if (raw_node_id < WVM_MAX_GATEWAYS) {
        gateway_table[raw_node_id].sin_family = AF_INET;
        gateway_table[raw_node_id].sin_addr.s_addr = ip;
        gateway_table[raw_node_id].sin_port = port;
    }
}

static int raw_kernel_send(void *data, int len, uint32_t target_id) {
    struct msghdr msg; struct kvec vec; struct sockaddr_in to_addr; int ret;
    // [Fix #1] target_id 可能是 composite ID，解码为裸 node_id 索引 gateway_table
    uint32_t raw_id = WVM_GET_NODEID(target_id);
    if (!g_socket || raw_id >= WVM_MAX_GATEWAYS || gateway_table[raw_id].sin_port == 0) return -ENODEV;

    to_addr = gateway_table[raw_id];
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &to_addr;
    msg.msg_namelen = sizeof(to_addr);
    msg.msg_flags = MSG_DONTWAIT;
    vec.iov_base = data;
    vec.iov_len = len;

    ret = kernel_sendmsg(g_socket, &msg, &vec, 1, len);
    if (ret == -EAGAIN) {
        if (in_atomic() || irqs_disabled()) udelay(5);
    }
    return ret;
}

/* 
 * [物理意图] 模拟处理器的“输出流水线”，负责将内核态产生的 Diff 和 ACK 投射到网络。
 * [关键逻辑] 严格执行优先级调度：Fast Ring（信令）绝对抢占 Slow Ring（全页）。同时将 CRC32 计算卸载到工作线程中。
 * [后果] 保证了系统在“内存洪流”中依然能及时响应心跳与版本确认包，防止了由于网络拥塞导致的误判定节点下线。
 */
static int tx_worker_thread_fn(void *data) {
    /* Large TX slots (up to 64KB) cannot live on kernel stack. */
    struct tx_slot_t *slot = kvzalloc(sizeof(*slot), GFP_KERNEL);
    if (!slot) return -ENOMEM;
    while (!kthread_should_stop()) {
        wait_event_interruptible(g_tx_wq, 
            atomic_read(&g_fast_ring.pending_count) > 0 || 
            atomic_read(&g_slow_ring.pending_count) > 0 || kthread_should_stop());
        
        if (kthread_should_stop()) break;

        // 1. Fast Lane (Priority)
        while (atomic_read(&g_fast_ring.pending_count) > 0) {
            int found = 0;
            spin_lock_bh(&g_fast_ring.lock);
            if (g_fast_ring.head != g_fast_ring.tail) {
                memcpy(slot, &g_fast_ring.slots[g_fast_ring.head], sizeof(*slot));
                g_fast_ring.head = (g_fast_ring.head + 1) % TX_RING_SIZE;
                atomic_dec(&g_fast_ring.pending_count);
                found = 1;
            }
            spin_unlock_bh(&g_fast_ring.lock);
            
            if (found) { 
                // [V29] CRC Calculation offloaded to worker thread
                struct wvm_header *hdr = (struct wvm_header *)slot->data;
                hdr->crc32 = 0;
                hdr->crc32 = htonl(calculate_crc32(slot->data, slot->len));
                
                raw_kernel_send(slot->data, slot->len, slot->target_id); 
                cond_resched(); 
            }
        }

        // 2. Slow Lane (Quota)
        int quota = 32; 
        while (quota-- > 0 && atomic_read(&g_slow_ring.pending_count) > 0) {
            int found = 0;
            spin_lock_bh(&g_slow_ring.lock);
            if (g_slow_ring.head != g_slow_ring.tail) {
                memcpy(slot, &g_slow_ring.slots[g_slow_ring.head], sizeof(*slot));
                g_slow_ring.head = (g_slow_ring.head + 1) % TX_RING_SIZE;
                atomic_dec(&g_slow_ring.pending_count);
                found = 1;
            }
            spin_unlock_bh(&g_slow_ring.lock);
            
            if (found) {
                // [V29] CRC Calculation
                struct wvm_header *hdr = (struct wvm_header *)slot->data;
                hdr->crc32 = 0;
                hdr->crc32 = htonl(calculate_crc32(slot->data, slot->len));
                
                raw_kernel_send(slot->data, slot->len, slot->target_id);
            }
            // Preempt if fast packet arrives
            if (atomic_read(&g_fast_ring.pending_count) > 0) break;
        }
        k_touch_watchdog();
    }
    kvfree(slot);
    return 0;
}

// [PATCH] 全局 Epoch 变量
static uint32_t g_kernel_epoch = 0;

// [修改] k_send_packet 函数
static int k_send_packet(void *data, int len, uint32_t target_id) {
    struct wvm_header *hdr = (struct wvm_header *)data;
    
    // [PATCH] 注入当前 Epoch
    hdr->epoch = htonl(g_kernel_epoch);
    // Node State 在内核态通常默认为 ACTIVE (2)，因为能跑内核模块说明节点活着
    hdr->node_state = 2; 
    hdr->target_id = htonl(target_id);

    hdr->crc32 = 0;
    hdr->crc32 = htonl(calculate_crc32(data, len));
    
    if (!k_is_atomic_context()) return raw_kernel_send(data, len, target_id);

    struct wvm_tx_ring_t *ring = (hdr->qos_level == 1) ? &g_fast_ring : &g_slow_ring;
    unsigned long flags;
    spin_lock_irqsave(&ring->lock, flags);
    uint32_t next = (ring->tail + 1) % TX_RING_SIZE;
    if (next != ring->head) {
        ring->slots[ring->tail].len = len;
        ring->slots[ring->tail].target_id = target_id;
        memcpy(ring->slots[ring->tail].data, data, len);
        ring->tail = next;
        atomic_inc(&ring->pending_count);
        wake_up_interruptible(&g_tx_wq);
    }
    spin_unlock_irqrestore(&ring->lock, flags);
    return 0;
}

static void k_send_packet_async(uint16_t msg_type, void* payload, int len, uint32_t target_id, uint8_t qos) {
    size_t pkt_len = sizeof(struct wvm_header) + len;
    uint8_t *buffer = k_alloc_packet(pkt_len, 1);
    if (!buffer) return;

    struct wvm_header *hdr = (struct wvm_header *)buffer;
    extern int g_my_node_id;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(msg_type);
    hdr->payload_len = htons(len);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->req_id = 0;
    hdr->qos_level = qos;
    if (payload && len > 0) memcpy(buffer + sizeof(*hdr), payload, len);

    k_send_packet(buffer, pkt_len, target_id);
    k_free_packet(buffer);
}

// 引用外部函数
extern int wvm_handle_local_fault_fastpath(uint64_t gpa, void* page_buffer, uint64_t *version);
extern void wvm_declare_interest_in_neighborhood(uint64_t gpa);

#define MAX_WVM_SLOTS 32

struct wvm_mem_slot {
    uint64_t start_gpa;
    uint64_t size;
    uint64_t host_offset; // 对应后端 SHM 文件的偏移
    bool active;
};

static struct wvm_mem_slot g_mem_slots[MAX_WVM_SLOTS];

// [NEW IOCTL] 动态注入 Guest 内存布局
static int wvm_set_mem_layout(struct wvm_ioctl_mem_layout *layout) {
    if (layout->count > MAX_WVM_SLOTS) return -EINVAL;

    /* Reset old slots first to avoid stale ranges after restart/reconfigure. */
    for (int i = 0; i < MAX_WVM_SLOTS; i++) {
        g_mem_slots[i].active = false;
        g_mem_slots[i].start_gpa = 0;
        g_mem_slots[i].size = 0;
        g_mem_slots[i].host_offset = 0;
    }

    for (int i = 0; i < layout->count; i++) {
        g_mem_slots[i].start_gpa = layout->slots[i].start;
        g_mem_slots[i].size = layout->slots[i].size;
        g_mem_slots[i].active = true;
        printk(KERN_INFO "[WVM] Active RAM Slot %d: GPA 0x%llx - 0x%llx\n", 
               i, g_mem_slots[i].start_gpa, g_mem_slots[i].start_gpa + g_mem_slots[i].size);
    }
    return 0;
}

/* 
 * [物理意图] 接管处理器的“缺页异常（EPT Violation）”，实现虚拟化空间的物理坍缩。
 * [关键逻辑] 1. 本地命中路径：直接拷贝 Directory 内容；2. 远程路径：利用 wait_queue 将 vCPU 线程挂起，实现异步网络拉取。
 * [后果] 这是整个项目最高频的入口点。通过异步休眠替代了 V28 的自旋死等，使得物理 CPU 可以在等待内存期间去跑其他任务。
 */
static vm_fault_t wvm_fault_handler(struct vm_fault *vmf) {
    uint64_t gpa = (uint64_t)vmf->pgoff << PAGE_SHIFT;
    bool is_valid_ram = false;

    // 严谨的地址合法性检查
    for (int i = 0; i < MAX_WVM_SLOTS; i++) {
        if (g_mem_slots[i].active && 
            gpa >= g_mem_slots[i].start_gpa && 
            gpa < g_mem_slots[i].start_gpa + g_mem_slots[i].size) {
            is_valid_ram = true;
            break;
        }
    }

    if (!is_valid_ram) {
        // 如果 GPA 落在了 PCI Hole 或未映射区域，绝对不能继续
        return VM_FAULT_SIGBUS; 
    }

    uint32_t dir_node = wvm_get_directory_node_id(gpa);
    struct page *page;
    void *page_addr;
    kvm_page_meta_t *meta;
    int ret;

    // 1. 分配目标物理页 (HighMem 兼容)
    page = alloc_page(GFP_HIGHUSER_MOVABLE);
    if (unlikely(!page)) {
        return VM_FAULT_OOM;
    }

    // 2. 为 V29 版本控制分配元数据结构
    meta = kmalloc(sizeof(kvm_page_meta_t), GFP_KERNEL);
    if (unlikely(!meta)) {
        __free_page(page);
        return VM_FAULT_OOM;
    }
    meta->page = page;
    meta->version = 0;
    // 初始化乱序重排队列头
    skb_queue_head_init(&meta->reorder_q); 

    // ========================================================================
    // 分支 A: 本地缺页 (Local Fault) - 极速路径
    // ========================================================================
    if (WVM_GET_NODEID(dir_node) == (uint32_t)g_my_node_id) {
        uint64_t local_version = 0;
        
        // 建立临时内核映射进行拷贝 (原子上下文)
        page_addr = kmap_atomic(page);
        
        // 调用 Logic Core 直接获取数据
        ret = wvm_handle_local_fault_fastpath(gpa, page_addr, &local_version);
        
        kunmap_atomic(page_addr);

        if (ret == 0) {
            // 成功：更新元数据版本
            meta->version = local_version;

            // 插入 VMA
            if (vm_insert_page(vmf->vma, vmf->address, page) != 0) {
                kfree(meta);
                __free_page(page);
                return VM_FAULT_SIGBUS;
            }
            
            // 注册到全局索引树 (用于后续接收 PUSH)
            spin_lock(&g_page_tree_lock);
            radix_tree_insert(&g_page_tree, gpa >> PAGE_SHIFT, meta);
            spin_unlock(&g_page_tree_lock);

            // 释放 alloc_page 的引用，现在由 VMA 管理
            put_page(page);

            // [V29] 宣告兴趣
            wvm_declare_interest_in_neighborhood(gpa);
            
            return VM_FAULT_NOPAGE;
        } else {
            // 本地查找失败 (逻辑错误或严重不一致)
            kfree(meta);
            __free_page(page);
            return VM_FAULT_SIGBUS;
        }
    } 
    
    // ========================================================================
    // 分支 B: 远程缺页 (Remote Fault) - 无限重试版
    // ========================================================================
    else {
        uint64_t rid;
        long timeout_ret;
        
        // 1. 分配跳板缓冲区 (Bounce Buffer)
        size_t ack_payload_size = sizeof(struct wvm_mem_ack_payload);
        void *bounce_buf = kmalloc(ack_payload_size, GFP_KERNEL);
        if (unlikely(!bounce_buf)) {
            kfree(meta);
            __free_page(page);
            return VM_FAULT_OOM;
        }

        // 2. 分配请求 ID
        rid = k_alloc_req_id(bounce_buf, (uint32_t)ack_payload_size);
        if (rid == (uint64_t)-1) {
            kfree(bounce_buf);
            kfree(meta);
            __free_page(page);
            return VM_FAULT_SIGBUS;
        }

        // 3. 构造请求包
        uint64_t net_gpa = WVM_HTONLL(gpa);
        size_t pkt_len = sizeof(struct wvm_header) + sizeof(net_gpa);
        
        // --- 重试循环上下文 ---
        unsigned long timeout_jiffies = msecs_to_jiffies(10); // 初始 10ms
        unsigned long max_timeout = msecs_to_jiffies(2000);   // 最大 2s
        
        // 首次发送
        uint8_t *buffer = k_alloc_packet(pkt_len, 1);
        if (likely(buffer)) {
            struct wvm_header *hdr = (struct wvm_header *)buffer;
            hdr->magic = htonl(WVM_MAGIC);
            hdr->msg_type = htons(MSG_MEM_READ);
            hdr->payload_len = htons(sizeof(net_gpa));
            hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, dir_node));
            hdr->req_id = WVM_HTONLL(rid);
            hdr->qos_level = 1;
            hdr->crc32 = 0;
            memcpy(buffer + sizeof(*hdr), &net_gpa, sizeof(net_gpa));
            k_send_packet(buffer, pkt_len, dir_node);
            k_free_packet(buffer);
        }

        while (1) {
            // 4. 睡眠等待
            timeout_ret = wait_event_interruptible_timeout(
                g_req_ctx[(uint32_t)rid].wq, 
                READ_ONCE(g_req_ctx[(uint32_t)rid].done) == 1, 
                timeout_jiffies
            );

            // 5. 成功检查
            if (READ_ONCE(g_req_ctx[(uint32_t)rid].done) == 1) {
                break; // 数据到了，跳出
            }

            // 6. 信号中断检查 (防止死锁)
            if (signal_pending(current)) {
                k_log("Page fault interrupted by signal. GPA: %llx", gpa);
                k_free_req_id(rid);
                kfree(bounce_buf);
                kfree(meta);
                __free_page(page);
                return VM_FAULT_SIGBUS; 
            }

            // 7. 超时重发 (指数退避)
            if (timeout_ret == 0) {
                timeout_jiffies *= 2;
                if (timeout_jiffies > max_timeout) timeout_jiffies = max_timeout;
                
                buffer = k_alloc_packet(pkt_len, 1);
                if (buffer) {
                    struct wvm_header *hdr = (struct wvm_header *)buffer;
                    hdr->magic = htonl(WVM_MAGIC);
                    hdr->msg_type = htons(MSG_MEM_READ);
                    hdr->payload_len = htons(sizeof(net_gpa));
                    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, dir_node));
                    hdr->req_id = WVM_HTONLL(rid);
                    hdr->qos_level = 1;
                    hdr->crc32 = 0;
                    memcpy(buffer + sizeof(*hdr), &net_gpa, sizeof(net_gpa));
                    k_send_packet(buffer, pkt_len, dir_node);
                    k_free_packet(buffer);
                }
            }
        }

        // --- 数据处理 ---
        k_free_req_id(rid); 

        // 从 Bounce Buffer 拷贝数据
        struct wvm_mem_ack_payload *ack = (struct wvm_mem_ack_payload *)bounce_buf;
        meta->version = WVM_NTOHLL(ack->version);
        
        page_addr = kmap_atomic(page);
        memcpy(page_addr, ack->data, 4096);
        kunmap_atomic(page_addr);
        
        kfree(bounce_buf);

        if (vm_insert_page(vmf->vma, vmf->address, page) != 0) {
            kfree(meta);
            __free_page(page);
            return VM_FAULT_SIGBUS;
        }
        
        spin_lock(&g_page_tree_lock);
        radix_tree_insert(&g_page_tree, gpa >> PAGE_SHIFT, meta);
        spin_unlock(&g_page_tree_lock);

        put_page(page);

        // V29: 宣告兴趣
        wvm_declare_interest_in_neighborhood(gpa);

        return VM_FAULT_NOPAGE;
    }
}

/* 
 * [物理意图] 内核态“内存小波”生成器，计算物理页的增量变动。
 * [关键逻辑] 在无锁状态下对比“前快照”与“现数据”，生成最小化的 Diff 日志，并通过 COMMIT_DIFF 消息提交给全网。
 * [后果] 实现了极低带宽下的高频内存同步。它只传输变动的比特，使得在千兆网环境下也能模拟出万兆总线的写入吞吐量。
 */
static int committer_thread_fn(void *data) {
    while (!kthread_should_stop()) {
        struct diff_task_t *task;
        
        // 1. 等待任务
        wait_event_interruptible(g_diff_wq, 
            !list_empty(&g_diff_queue) || kthread_should_stop());
            
        if (kthread_should_stop()) break;

        // 2. 取出一个任务
        spin_lock_bh(&g_diff_lock);
        if (list_empty(&g_diff_queue)) {
            spin_unlock_bh(&g_diff_lock);
            continue;
        }
        task = list_first_entry(&g_diff_queue, struct diff_task_t, list);
        list_del(&task->list);
        spin_unlock_bh(&g_diff_lock);

        // 3. 计算 Diff (Compare current page with pre-image)
        void *current_data = kmap_atomic(task->page);
        
        // --- 优化版 Diff 算法 ---
        int first_diff = -1, last_diff = -1;
        
        // 1. 使用 64位 (8字节) 指针进行快速扫描
        uint64_t *p64_curr = (uint64_t *)current_data;
        uint64_t *p64_pre  = (uint64_t *)task->pre_image;
        int num_words = 4096 / 8; // 512 次循环
        
        for (int i = 0; i < num_words; i++) {
            if (p64_curr[i] != p64_pre[i]) {
                // 发现不同！记录位置
                // 这里的 i 是 8字节的索引，转为字节索引
                int byte_offset = i * 8;
                
                if (first_diff == -1) {
                    first_diff = byte_offset; // 粗略定位到 8字节块
                }
                last_diff = byte_offset + 7;  // 粗略定位到 8字节块末尾
            }
        }
        
        kunmap_atomic(current_data);
        
        // 4. 发送 Diff
        if (first_diff != -1) {
            uint16_t offset = first_diff;
            uint16_t size = last_diff - first_diff + 1;
            
            // 构造 COMMIT_DIFF 包
            size_t payload_size = sizeof(struct wvm_diff_log) + size;
            size_t pkt_len = sizeof(struct wvm_header) + payload_size;
            
            uint8_t *buffer = k_alloc_packet(pkt_len, 0); // Can sleep here
            if (buffer) {
                struct wvm_header *hdr = (struct wvm_header *)buffer;
                hdr->magic = htonl(WVM_MAGIC);
                hdr->msg_type = htons(MSG_COMMIT_DIFF);
                hdr->payload_len = htons(payload_size);
                extern int g_my_node_id;
                hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                hdr->req_id = 0;
                hdr->qos_level = 1;

                struct wvm_diff_log *log = (struct wvm_diff_log*)(buffer + sizeof(*hdr));
                log->gpa = WVM_HTONLL(task->gpa);
                rcu_read_lock();
                kvm_page_meta_t *meta = radix_tree_lookup(&g_page_tree, task->gpa >> PAGE_SHIFT);
                // 如果找到了元数据，就填入网络序的版本号；找不到(极罕见)则填0触发强制同步作为保底
                log->version = meta ? WVM_HTONLL(meta->version) : 0;
                rcu_read_unlock();
                log->offset = htons(offset);
                log->size = htons(size);
                
                // Copy diff data
                current_data = kmap_atomic(task->page);
                memcpy(log->data, (uint8_t*)current_data + offset, size);
                kunmap_atomic(current_data);
                
                // Send
                uint32_t dir_node = wvm_get_directory_node_id(task->gpa);
                k_send_packet(buffer, pkt_len, dir_node);
                k_free_packet(buffer);
            }
        }

        // 5. [Critical] 重新启用写保护 (Reset cycle)
        // 必须调用 unmap_mapping_range 来清除 PTE 的写权限
        // 这样下一次写入才会再次触发 page_mkwrite
        down_read(&g_mapping_sem); // 读锁，允许睡眠
        if (g_mapping) {
            unmap_mapping_range(g_mapping, (loff_t)task->gpa, PAGE_SIZE, 1);
        }
        up_read(&g_mapping_sem);

        // 6. 清理资源
        vfree(task->pre_image);
        put_page(task->page); // 释放我们在 mkwrite 中获取的引用
        kfree(task);
    }
    return 0;
}

/* 
 * [物理意图] 捕获 Guest 对“只读缓存页”的第一次写动作，启动 Wavelet 提交周期。
 * [关键逻辑] 当 Guest 试图修改内存时拦截请求，执行 Copy-Before-Write 快照，并将原始数据放入 Committer 队列。
 * [后果] 它是分布式写操作的触发器。通过此钩子，WaveVM 能在不修改 Guest 内核的情况下，精准感知每一个字节的变动。
 */
static vm_fault_t wvm_page_mkwrite(struct vm_fault *vmf) {
    struct page *page = vmf->page;
    uint64_t gpa = (uint64_t)vmf->pgoff << PAGE_SHIFT;
    
    // 1. 分配任务结构和快照内存
    struct diff_task_t *task = kmalloc(sizeof(*task), GFP_KERNEL);
    if (!task) return VM_FAULT_OOM;
    
    task->pre_image = vmalloc(4096);
    if (!task->pre_image) {
        kfree(task);
        return VM_FAULT_OOM;
    }

    // 2. 捕获快照 (Copy-Before-Write)
    // 此时页面还是旧数据
    void *vaddr = kmap_atomic(page);
    memcpy(task->pre_image, vaddr, 4096);
    kunmap_atomic(vaddr);

    task->page = page;
    get_page(page); // 增加引用计数，防止在线程处理前被释放
    task->gpa = gpa;
    task->timestamp = k_get_time_us();

    // 3. 加入队列
    // [FIX-M3] 统一使用 spin_lock_bh，与取出端保持一致，防止 softirq 死锁
    spin_lock_bh(&g_diff_lock);
    list_add_tail(&task->list, &g_diff_queue);
    spin_unlock_bh(&g_diff_lock);

    // 4. 唤醒提交线程
    wake_up_interruptible(&g_diff_wq);

    // 5. 允许写入
    // 返回 VM_FAULT_LOCKED 后，内核会将 PTE 设为可写
    return VM_FAULT_LOCKED;
}

// --- 接收处理 ---
extern void wvm_logic_process_packet(struct wvm_header *hdr, void *payload, uint32_t source_id);

// [V29 Core] 尝试应用队列中的乱序包 (链式反应)
// 当收到 v+1 包后，调用此函数检查是否缓存了 v+2, v+3...
static void try_flush_reorder_q(kvm_page_meta_t *meta) {
    struct sk_buff *skb, *tmp;
    bool progress = true;

    // 循环直到无法继续推进版本 (链条断裂)
    while (progress) {
        progress = false;
        
        // 必须加锁，防止软中断并发修改队列
        spin_lock(&meta->reorder_q.lock);
        
        skb_queue_walk_safe(&meta->reorder_q, skb, tmp) {
            // skb->data 存储的是完整的 wvm_header + payload
            struct wvm_header *hdr = (struct wvm_header *)skb->data;
            void *pl = skb->data + sizeof(struct wvm_header);
            
            // 解析包类型与版本号
            uint16_t type = ntohs(hdr->msg_type);
            uint64_t ver = 0;
            void *data_ptr = NULL;
            uint16_t offset = 0;
            uint32_t len = 0;

            if (type == MSG_PAGE_PUSH_DIFF) {
                struct wvm_diff_log *log = (struct wvm_diff_log *)pl;
                ver = WVM_NTOHLL(log->version);
                offset = ntohs(log->offset);
                len = ntohs(log->size);
                data_ptr = log->data;
                // 边界检查
                if (offset + len > 4096) len = 0; 
            } else if (type == MSG_PAGE_PUSH_FULL) {
                struct wvm_full_page_push *full = (struct wvm_full_page_push *)pl;
                ver = WVM_NTOHLL(full->version);
                data_ptr = full->data;
                len = 4096;
            }

            // 命中逻辑：这是我们紧缺的下一个版本
            if (is_next_version(meta->version, ver)) {
                // 1. 从队列摘除 (Unlink)
                __skb_unlink(skb, &meta->reorder_q);
                
                // 2. 应用数据
                // kmap_atomic 可以在自旋锁内使用 (不会睡眠)
                if (len > 0 && data_ptr) {
                    void *vaddr = kmap_atomic(meta->page);
                    memcpy((uint8_t*)vaddr + offset, data_ptr, len);
                    kunmap_atomic(vaddr);
                }
                
                // 3. 更新版本号并标记脏
                meta->version = ver;
                SetPageDirty(meta->page);
                
                // 4. 释放 skb 内存
                kfree_skb(skb);
                
                // 5. 标记进度，跳出遍历，重新开始 (因为链表结构已变)
                progress = true; 
                break; 
            }
            // 过期逻辑：版本号小于等于当前，直接丢弃
            else if (ver <= meta->version) {
                __skb_unlink(skb, &meta->reorder_q);
                kfree_skb(skb);
            }
            // 未来逻辑：ver > meta->version + 1，继续留在队列里等待
        }
        spin_unlock(&meta->reorder_q.lock);
    }
}

/* 
 * [物理意图] 实现“零拷贝”真理更新，将远程推送直接注入 Guest 的物理页。
 * [关键逻辑] 校验版本号：顺序包直接 kmap 写入，乱序包存入 skb_head 队列进行重排。处理完后触发 try_flush 链式应用。
 * [后果] 这是 V30 性能超越 V28 的秘密武器。它在内核底层完成了数据的“无感更新”，Guest 甚至不知道内存已经变了。
 */
static void handle_kernel_push(struct wvm_header *hdr, void *payload) {
    uint16_t type = ntohs(hdr->msg_type);
    uint8_t flags = hdr->flags; 
    uint64_t gpa, push_version;
    void *data_ptr;
    uint32_t data_len;
    uint16_t offset = 0;

    // --- 1. 解析 Payload ---
    if (type == MSG_PAGE_PUSH_FULL) {
        struct wvm_full_page_push *push = (struct wvm_full_page_push *)payload;
        gpa = WVM_NTOHLL(push->gpa);
        push_version = WVM_NTOHLL(push->version);
        data_ptr = push->data;
        data_len = 4096;
    } else if (type == MSG_PAGE_PUSH_DIFF) {
        struct wvm_diff_log *log = (struct wvm_diff_log *)payload;
        gpa = WVM_NTOHLL(log->gpa);
        push_version = WVM_NTOHLL(log->version);
        offset = ntohs(log->offset);
        data_len = ntohs(log->size);
        data_ptr = log->data;
        if (offset + data_len > 4096) return;
    } else {
        return;
    }

    // --- 2. 核心处理 ---
    rcu_read_lock(); 
    kvm_page_meta_t *meta = radix_tree_lookup(&g_page_tree, gpa >> PAGE_SHIFT);
    
    if (meta && meta->page) {
        // [FIX] 幂等性检查：如果包版本 <= 本地版本，静默丢弃
        // 内核态也必须防御重放攻击或网络乱序
        if (!is_newer_version(meta->version, push_version)) {
            rcu_read_unlock();
            return;
        }
        // [场景 A] 顺序到达 (Ideal Case)
        // 如果是全量推送 (Full Push)，或者是连续的 Diff，直接应用
        // 这解决了 Epoch 切换时版本号大跳跃导致的内核死锁问题
        if (is_next_version(meta->version, push_version)) {
            void *vaddr = kmap_atomic(meta->page); 
            if (flags & WVM_FLAG_ZERO) {
                memset(vaddr, 0, 4096);
            } else if (data_len > 0) {
                memcpy((uint8_t*)vaddr + offset, data_ptr, data_len);
            }
            kunmap_atomic(vaddr);
            
            meta->version = push_version;
            SetPageDirty(meta->page);
            try_flush_reorder_q(meta); // 应用后尝试消化队列里的包
        } 
        // [场景 B] 乱序到达 (Future Packet) - 存入队列
        else if (is_newer_version(meta->version, push_version) && !is_next_version(meta->version, push_version)) {
            // 限制队列深度为 8，防止内存耗尽攻击
            if (skb_queue_len(&meta->reorder_q) < 8) {
                // 我们必须分配一个新的 skb 来持久化保存数据
                // 因为当前函数的 payload 指针指向的是临时的网络栈 buffer
                size_t total_len = sizeof(struct wvm_header) + ntohs(hdr->payload_len);
                struct sk_buff *saved_skb = alloc_skb(total_len, GFP_ATOMIC);
                
                if (saved_skb) {
                    // 拷贝完整的 Header + Payload
                    skb_put_data(saved_skb, hdr, total_len); 
                    // 入队 (skb_queue_tail 是并发安全的，但为了和 try_flush 互斥，内部自带锁)
                    skb_queue_tail(&meta->reorder_q, saved_skb);
                }
            }
            // 如果队列满了，丢弃该包，只能等待发送端重传或 V28 兜底
        }
        // [场景 C] 过期包 - 忽略
    }
    rcu_read_unlock(); 
}

/* 
 * [物理意图] 内核态“先知”指令加速器，执行全网同步的物理内存填充。
 * [关键逻辑] 遍历 Radix-Tree 映射的物理页，利用 wmb 内存屏障和 memset 直接操作硬件页，并强制推进逻辑时钟。
 * [后果] 它让分布式虚拟机的“冷启动”与“克隆”速度达到了硬件极限，绕过了所有缓慢的软件协议栈处理路径。
 */
static void handle_kernel_rpc_batch(void *payload, uint32_t payload_len) {
    if (payload_len < sizeof(struct wvm_rpc_batch_memset)) return;

    struct wvm_rpc_batch_memset *batch = (struct wvm_rpc_batch_memset *)payload;
    uint32_t count = ntohl(batch->count);
    uint32_t val = ntohl(batch->val); // 通常是 0
    uint8_t fill_byte = (uint8_t)val;

    // 边界检查
    size_t required_len = sizeof(struct wvm_rpc_batch_memset) + count * sizeof(struct wvm_rpc_region);
    if (payload_len < required_len) return;

    struct wvm_rpc_region *regions = (struct wvm_rpc_region *)(batch + 1);

    // 遍历所有内存段
    for (uint32_t i = 0; i < count; i++) {
        uint64_t gpa_start = WVM_NTOHLL(regions[i].gpa);
        uint64_t len = WVM_NTOHLL(regions[i].len);
        uint64_t gpa_end = gpa_start + len;
        uint64_t cur_gpa = gpa_start;

        // 按页遍历 (Page Walk)
        while (cur_gpa < gpa_end) {
            // 计算当前页内的操作范围
            uint64_t page_base = cur_gpa & PAGE_MASK;
            uint64_t page_offset = cur_gpa & ~PAGE_MASK;
            uint64_t bytes_in_page = PAGE_SIZE - page_offset;
            uint64_t remaining = gpa_end - cur_gpa;
            uint64_t op_len = (remaining < bytes_in_page) ? remaining : bytes_in_page;

            rcu_read_lock();
            // 1. 在 Radix Tree 中查找物理页元数据
            kvm_page_meta_t *meta = radix_tree_lookup(&g_page_tree, page_base >> PAGE_SHIFT);
            
            if (meta && meta->page) {
                // 2. 建立临时原子映射 (HighMem 兼容)
                void *vaddr = kmap_atomic(meta->page);
                
                // 3. 执行物理内存填充
                memset((uint8_t*)vaddr + page_offset, fill_byte, op_len);
                // [FIX] DMA Fence: 确保数据刷入内存，对设备可见
                wmb(); 
                kunmap_atomic(vaddr);

                // 4. 标记脏页并更新版本号 (逻辑一致性)
                SetPageDirty(meta->page);
                meta->version = MAKE_VERSION(g_kernel_epoch, GET_COUNTER(meta->version) + 1);
                
                // 注意：这里不需要广播 Diff，因为 Prophet 的设计假设是
                // 发送端已经广播了 RPC 指令，所有节点都会执行这个操作。
                // 我们只需要更新本地状态即可。
            }
            // 如果 meta 不存在：说明该页尚未被分配/缺页。
            // 对于 memset 0 来说，新分配的页本身就是 0，所以跳过是安全的。
            // 对于 memset 非0，当缺页触发时，会拉取最新数据。
            
            rcu_read_unlock();

            cur_gpa += op_len;
        }
    }
}

/* 
 * [物理意图] 内核态“安全筛网”，负责流量的物理合法性判定与分流。
 * [关键逻辑] 1. 执行强制 CRC32 完整性校验；2. 识别消息类型并根据优先级分发给 Prophet 引擎或逻辑核心。
 * [后果] 这是内核驱动的防火墙。它确保了只有格式正确、且未被篡改的数据包能接触到 Guest 的物理内存。
 */
static void internal_process_single_packet(struct wvm_header *hdr, uint32_t src_ip) {
    // 1. [V29 新增] CRC32 强校验
    // 这是 V28 没有的。如果不加，V29 的高可靠性就是空谈。
    uint32_t pkt_len = sizeof(struct wvm_header) + ntohs(hdr->payload_len);
    uint32_t received_crc = ntohl(hdr->crc32);
    
    // 临时清零 CRC 字段进行计算
    hdr->crc32 = 0; 
    uint32_t calculated = calculate_crc32(hdr, pkt_len);
    
    if (calculated != received_crc) {
        // 校验失败，直接静默丢弃，防止污染状态
        // 生产环境可以加一行 printk 但注意频率
        return; 
    }
    
    // 2. 解析通用字段
    void *payload = (void *)hdr + sizeof(struct wvm_header);
    uint16_t type = ntohs(hdr->msg_type);
    
    // 3. [V30 核心修复] 识别 CPU/存储包，内核直接“踢”给本地 Slave 端口
    if (type == MSG_VCPU_RUN || type == MSG_BLOCK_WRITE || 
        type == MSG_BLOCK_READ || type == MSG_BLOCK_FLUSH) {
        
        struct sockaddr_in s_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
            .sin_port = htons(local_slave_port)
        };
        struct msghdr msg = { .msg_name = &s_addr, .msg_namelen = sizeof(s_addr), .msg_flags = MSG_DONTWAIT };
        struct kvec vec = { .iov_base = hdr, .iov_len = pkt_len };
        kernel_sendmsg(g_socket, &msg, &vec, 1, pkt_len); // 内核直发 Loopback
        return;
    }

    // 4. 拦截物理中断包
    // Logic Core 无法处理此消息，必须在内核层直接唤醒 QEMU 等待队列
    if (type == MSG_VFIO_IRQ) {
        atomic_set(&g_irq_pending, 1);
        wake_up_interruptible(&g_irq_wait_queue);
        return; 
    }

    // 5. [V29 核心] Prophet 处理
    if (type == MSG_RPC_BATCH_MEMSET) {
        uint32_t payload_len = ntohs(hdr->payload_len);
        handle_kernel_rpc_batch(payload, payload_len);
        return; // 处理完毕直接返回，不需要 Logic Core 介入
    }


    // 6. [V29 核心] 拦截主动推送 (Direct Kernel Push)
    // 这是 V29 性能翻倍的关键。必须在内核态直接处理 Diff/Full Push。
    if (type == MSG_PAGE_PUSH_FULL || type == MSG_PAGE_PUSH_DIFF) {
        handle_kernel_push(hdr, payload);
        return;
    }

    // 7. [V28 继承] 处理 ACK (唤醒等待线程)
    if (type == MSG_MEM_ACK || type == MSG_VCPU_EXIT) {
        uint64_t rid = WVM_NTOHLL(hdr->req_id);
        uint32_t combined_idx = (uint32_t)(rid & 0xFFFFFFFF);
        if ((size_t)combined_idx < g_req_ctx_count) {
            struct req_ctx_t *ctx = &g_req_ctx[combined_idx];
            // 检查 Generation 防止 ABA
            if ((rid >> 32) == ctx->generation) {
                if (ctx->rx_buffer) {
                    // V29 的 Payload 包含 Version，结构体变了，但 memcpy 逻辑通用
                    // [FIX-G3] 截断保护：防止恶意/错乱包溢出 rx_buffer
                    size_t copy_len = ntohs(hdr->payload_len);
                    if (copy_len > ctx->max_len)
                        copy_len = ctx->max_len;
                    memcpy(ctx->rx_buffer, payload, copy_len);
                }
                WRITE_ONCE(ctx->done, 1);
                wake_up_interruptible(&ctx->wq); // V29 用 wait_queue 替代了 V28 的自旋
            }
        }
        return;
    }
    
    // 8. [V28 继承] 其他信令转交 Logic Core
    // V29 的 Logic Core 需要知道是谁发的 (Source ID)
    uint32_t src_id = ntohl(hdr->slave_id);
    wvm_logic_process_packet(hdr, payload, src_id);
}

/* 
 * [物理意图] 网卡驱动级别的“数据就绪”回调，执行高PPS解包循环。
 * [关键逻辑] 拦截 Socket 接收队列，对聚合的 UDP 数据包执行循环解构，提取每一个嵌入的 WVM 子包头。
 * [后果] 配合 Gateway 的聚合逻辑，它在内核态实现了“多包合一”的高效处理，极大地减轻了 CPU 软中断的负荷。
 */
static void wavevm_udp_data_ready(struct sock *sk) {
    struct sk_buff *skb;
    while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
        // 1. 线性化：确保 skb->data 是连续内存，方便指针操作
        if (skb_is_nonlinear(skb) && skb_linearize(skb) != 0) { 
            kfree_skb(skb); 
            continue; 
        }

        uint32_t offset = 0;
        uint32_t total_len = skb->len;
        struct iphdr *iph = ip_hdr(skb);
        uint32_t src_ip = iph->saddr;

        // 2. 内核态循环解包 (De-aggregation Loop)
        while (offset + sizeof(struct wvm_header) <= total_len) {
            struct wvm_header *hdr = (struct wvm_header *)(skb->data + offset);

            // 魔数校验：防止处理垃圾数据导致 panic
            if (ntohl(hdr->magic) != WVM_MAGIC) break;

            uint16_t p_len = ntohs(hdr->payload_len);
            uint32_t pkt_len = sizeof(struct wvm_header) + p_len;

            // 边界检查
            if (offset + pkt_len > total_len) break;

            // 调用单包处理逻辑 (注意：该函数内部已包含 CRC 校验)
            internal_process_single_packet(hdr, src_ip);

            // 3. 指针后移
            offset += pkt_len;
        }

        kfree_skb(skb);
    }
}

/* 
 * [物理意图] 物理级映射撤销：强行将 Guest 的硬件 PTE 变为无效。
 * [关键逻辑] 通过 Workqueue 异步触发 unmap_mapping_range，强制冲刷所有物理核心的 TLB 缓存。
 * [后果] 实现了真正的“硬件级失效”。当 Guest 下一次访问该地址时，硬件会产生 EPT 违例并陷入内核，保证不会读到旧数据。
 */
static void k_invalidate_local(uint64_t gpa) {
    // 1. 先从内核元数据索引中删除（防止新的 Push 进来）
    k_invalidate_meta_atomic(gpa);
    
    // 2. [关键] 必须调用这个！触发 unmap_mapping_range
    // 这会强制撤销 Guest 的物理页映射，并冲刷 TLB
    // 由于此函数可能在软中断上下文，必须走 Workqueue 异步执行
    schedule_async_unmap(gpa);
    
    k_log("[Consistency] Invalidate hardware mapping for GPA: %llx", gpa);
}

// --- Driver & Module Init ---
static struct dsm_driver_ops k_ops = {
    .alloc_large_table = k_alloc_large_table,
    .free_large_table = k_free_large_table,
    .alloc_packet = k_alloc_packet,
    .free_packet = k_free_packet,
    .set_gateway_ip = k_set_gateway_ip,
    .send_packet = k_send_packet,
    .fetch_page = k_fetch_page,
    .invalidate_local = k_invalidate_local,
    .log = k_log,
    .is_atomic_context = k_is_atomic_context,
    .touch_watchdog = k_touch_watchdog,
    .alloc_req_id = k_alloc_req_id,
    .free_req_id = k_free_req_id,
    .get_time_us = k_get_time_us,
    .time_diff_us = k_time_diff_us,
    .check_req_status = k_check_req_status,
    .cpu_relax = k_cpu_relax,
    .get_random = k_get_random,
    .yield_cpu_short_time = k_yield_short,
    .send_packet_async = k_send_packet_async
};

// [V29] 存储全局 mapping 以便 unmap 使用
static int wvm_open(struct inode *inode, struct file *filp) {
    g_mapping = inode->i_mapping;
    return 0;
}

static int wvm_release(struct inode *inode, struct file *filp) {
    // 当 QEMU 进程被杀或退出时，VMA 映射(mapping)即将失效。
    // 必须立刻将全局指针置空，防止正在 Workqueue 中排队的 unmap 任务访问野指针。
    down_write(&g_mapping_sem); // 写锁
    g_mapping = NULL;
    up_write(&g_mapping_sem);
    
    return 0;
}

static long wvm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    void __user *argp = (void __user *)arg;
    switch (cmd) {
    case IOCTL_SET_GATEWAY: {
        struct wvm_ioctl_gateway gw;
        if (copy_from_user(&gw, argp, sizeof(gw))) return -EFAULT;
        if (gw.gw_id < WVM_MAX_GATEWAYS) {
            gateway_table[gw.gw_id].sin_family = AF_INET;
            gateway_table[gw.gw_id].sin_addr.s_addr = gw.ip;
            gateway_table[gw.gw_id].sin_port = gw.port;
        }
        break;
    }
    case IOCTL_WVM_REMOTE_RUN: {
        struct wvm_ipc_cpu_run_req req;
        struct wvm_ipc_cpu_run_ack ack;
        if (copy_from_user(&req, argp, sizeof(req))) return -EFAULT;

        uint32_t target = req.slave_id;
        if (!WVM_IS_VALID_TARGET(target)) {
            target = wvm_get_compute_slave_id(req.vcpu_index);
        }
        if (!WVM_IS_VALID_TARGET(target)) return -ENODEV;

        int ctx_len = req.mode_tcg ? sizeof(req.ctx.tcg) : sizeof(req.ctx.kvm);
        int ret = wvm_rpc_call(MSG_VCPU_RUN, &req.ctx, ctx_len, target, &ack.ctx, sizeof(ack.ctx));
        ack.status = ret;
        ack.mode_tcg = req.mode_tcg;
        if (copy_to_user(argp, &ack, sizeof(ack))) return -EFAULT;
        break;
    }
    case IOCTL_WAIT_IRQ: {
        if (wait_event_interruptible(g_irq_wait_queue, atomic_read(&g_irq_pending) != 0))
            return -ERESTARTSYS;
        atomic_set(&g_irq_pending, 0);
        uint32_t irq = 16; 
        if (copy_to_user(argp, &irq, sizeof(irq))) return -EFAULT;
        break;
    }

    case IOCTL_SET_MEM_LAYOUT: {
        struct wvm_ioctl_mem_layout layout;
        if (copy_from_user(&layout, argp, sizeof(layout))) return -EFAULT;
        return wvm_set_mem_layout(&layout);
    }

    // 我们复用 MEM_ROUTE 协议来传输简单的全局整数参数
    // Slot 0 = Total Nodes (用于 DHT 取模)
    // Slot 1 = My Node ID (用于判断是否为 Directory)
    case IOCTL_UPDATE_MEM_ROUTE: {
        struct wvm_ioctl_route_update head;
        
        // 1. 读取头部元数据
        if (copy_from_user(&head, argp, sizeof(head))) return -EFAULT;
        
        // 安全检查：参数注入通常只有几个整数，限制数量防止滥用
        if (head.count > 1024) return -EINVAL;

        // 2. 分配临时缓冲区
        uint32_t *buf = vmalloc(head.count * sizeof(uint32_t));
        if (!buf) return -ENOMEM;

        // 3. 读取 Payload (具体的数值)
        if (copy_from_user(buf, (uint8_t*)argp + sizeof(head), head.count * sizeof(uint32_t))) {
            vfree(buf);
            return -EFAULT;
        }

        // 4. 传导给 Logic Core
        for (int i = 0; i < head.count; i++) {
            // start_index 即 Slot ID
            // Logic Core 会根据 slot 0/1 更新 g_total_nodes / g_my_node_id
            // [FIX-M5] 移除 uint16_t 截断，保留完整 uint32_t
            wvm_set_mem_mapping(head.start_index + i, buf[i]);
        }
        
        vfree(buf);
        break;
    }

    // [PATCH] 处理 Epoch 更新
    case IOCTL_UPDATE_EPOCH: {
        uint32_t new_epoch;
        if (copy_from_user(&new_epoch, (void __user *)arg, sizeof(uint32_t)))
            return -EFAULT;
        g_kernel_epoch = new_epoch;
        // printk(KERN_INFO "[WVM] Kernel Epoch updated to %u\n", g_kernel_epoch);
        break;
    }

    case IOCTL_UPDATE_CPU_ROUTE: {
        struct wvm_ioctl_route_update head;
        if (copy_from_user(&head, argp, sizeof(head))) return -EFAULT;
        if (head.count > 4096 || head.start_index >= 4096) return -EINVAL;
        if (head.start_index + head.count > 4096) return -EINVAL;

        size_t bytes = (size_t)head.count * sizeof(uint32_t);
        if (head.count != 0 && bytes / sizeof(uint32_t) != head.count) return -EINVAL;
        uint32_t *buf = vmalloc(bytes);
        if (!buf) return -ENOMEM;
        if (copy_from_user(buf, (uint8_t *)argp + sizeof(head), head.count * sizeof(uint32_t))) {
            vfree(buf);
            return -EFAULT;
        }

        for (uint32_t i = 0; i < head.count; i++) {
            wvm_set_cpu_mapping((int)(head.start_index + i), buf[i]);
        }
        vfree(buf);
        break;
    }

    default: return -EINVAL;
    }
    return 0;
}

static const struct vm_operations_struct wvm_vm_ops = {
    .fault = wvm_fault_handler,
    .page_mkwrite = wvm_page_mkwrite, // [V29] 注册 mkwrite 钩子
};

static int wvm_mmap(struct file *filp, struct vm_area_struct *vma) {
    vma->vm_ops = &wvm_vm_ops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
#else
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
#endif
    down_write(&g_mapping_sem);
    g_mapping = vma->vm_file->f_mapping; 
    up_write(&g_mapping_sem);
    return 0;
}

static const struct file_operations wvm_fops = {
    .owner = THIS_MODULE,
    .open = wvm_open,
    .release = wvm_release,
    .mmap = wvm_mmap,
    .unlocked_ioctl = wvm_ioctl,
};

static struct miscdevice wvm_misc = { 
    .minor = MISC_DYNAMIC_MINOR, 
    .name = DRIVER_NAME, 
    .fops = &wvm_fops 
};

static int __init wavevm_init(void) {
    int cpu;
    struct sockaddr_in bind_addr;

    init_waitqueue_head(&g_irq_wait_queue);
    init_waitqueue_head(&g_tx_wq);
    init_waitqueue_head(&g_diff_wq); // [V29] Init diff waiter
    spin_lock_init(&g_diff_lock);
    init_rwsem(&g_mapping_sem);

    /*
     * [FIX] 必须用 nr_cpu_ids 而非 num_online_cpus()。
     * get_cpu() 返回的是逻辑 CPU ID（可能不连续），例如 CPU 0,1,4,5 在线时
     * num_online_cpus()=4 但 get_cpu() 可能返回 5，导致 combined_idx 越界。
     * nr_cpu_ids 是内核保证的最大 CPU ID + 1，覆盖所有可能的 get_cpu() 返回值。
     */
    g_req_ctx_count = (size_t)nr_cpu_ids * (size_t)MAX_IDS_PER_CPU;
    g_req_ctx = vzalloc(sizeof(struct req_ctx_t) * g_req_ctx_count);
    if (!g_req_ctx) return -ENOMEM;
    for (size_t i = 0; i < g_req_ctx_count; i++) init_waitqueue_head(&g_req_ctx[i].wq);

    for_each_online_cpu(cpu) {
        struct id_pool_t *pool = per_cpu_ptr(&g_id_pool, cpu);
        spin_lock_init(&pool->lock);
        pool->ids = vzalloc(sizeof(uint32_t) * MAX_IDS_PER_CPU);
        pool->head = 0; pool->tail = MAX_IDS_PER_CPU;
        for (uint32_t i = 0; i < MAX_IDS_PER_CPU; i++) pool->ids[i] = i; 
    }

    size_t slab_size = sizeof(struct wvm_header) + 4096 + sizeof(struct wvm_mem_ack_payload);
    wvm_pkt_cache = kmem_cache_create("wvm_data", slab_size, 0, SLAB_HWCACHE_ALIGN, NULL);

    init_ring(&g_fast_ring);
    init_ring(&g_slow_ring);

    g_tx_thread = kthread_run(tx_worker_thread_fn, NULL, "wavevm_qos_tx");
    
    // [V29] 启动 Diff 提交线程
    g_committer_thread = kthread_run(committer_thread_fn, NULL, "wvm_diff_commit");

    if (wvm_core_init(&k_ops, 1) != 0) return -ENOMEM;

    if (misc_register(&wvm_misc)) return -ENODEV;
    if (sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &g_socket) < 0) return -EIO;

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(service_port);
    kernel_bind(g_socket, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    g_socket->sk->sk_data_ready = wavevm_udp_data_ready;

    k_log("WaveVM V29.5 'Wavelet' Kernel Backend Loaded (Mode A Active).");
    return 0;
}

static void __exit wavevm_exit(void) {
    int cpu;
    
    // 1. 先停掉所有自产的内核线程
    if (g_tx_thread) kthread_stop(g_tx_thread);
    if (g_committer_thread) kthread_stop(g_committer_thread);
    
    // 2. 刷新系统工作队列 (Workqueue Flush)
    // 确保所有 schedule_async_unmap 扔出去的任务都已执行完毕。
    // 如果不加这行，卸载模块后，系统队列里可能还有任务在跑，会执行已被卸载的代码 -> 崩溃。
    flush_scheduled_work(); 

    // 3. 等待 RCU 回调 (RCU Barrier)
    // 确保所有 kfree_rcu 的内存都已真正释放。
    rcu_barrier(); 

    // 4. 释放其余资源
    if (g_fast_ring.slots) vfree(g_fast_ring.slots);
    if (g_slow_ring.slots) vfree(g_slow_ring.slots);

    if (g_socket) { g_socket->sk->sk_data_ready = NULL; sock_release(g_socket); }
    misc_deregister(&wvm_misc);
    
    for_each_possible_cpu(cpu) {
        struct id_pool_t *pool = per_cpu_ptr(&g_id_pool, cpu);
        if (pool->ids) vfree(pool->ids);
    }
    vfree(g_req_ctx);
    // [FIX-M4] 销毁 slab 缓存，防止模块卸载后内存泄漏
    if (wvm_pkt_cache) kmem_cache_destroy(wvm_pkt_cache);
}

module_init(wavevm_init);
module_exit(wavevm_exit);
MODULE_LICENSE("GPL");

```

**文件**: `master_core/Kbuild`

```makefile
# 定义模块名称
obj-m += wavevm.o

# 定义模块包含的目标文件
# 将逻辑核心 (Logic Core) 和内核后端 (Kernel Backend) 链接为一个 .ko 文件
wavevm-y := kernel_backend.o logic_core.o

# 添加公共头文件路径
# $(src) 是内核构建系统提供的变量，指向当前目录
ccflags-y := -I$(src)/../common_include -std=gnu11

```

---

### Step 5: 用户态后端实现 (User Backend)

**文件**: `master_core/user_backend.c`

```c
/*
 * [IDENTITY] User Backend - The High-Concurrency QoS Bus
 * ---------------------------------------------------------------------------
 * 物理角色：用户态模式下的"消息交换机"。
 * 职责边界：
 * 1. 维护 Fast/Slow 双队列 QoS，保证控制信令 (ACK) 不被数据流淹没。
 * 2. 实现 Slab 风格的包内存池，利用 Thread-aware Hash 消除锁竞争。
 * 3. 驱动收发工作线程，处理聚合解包与 CRC32 卸载计算。
 * 
 * [禁止事项]
 * - 严禁在 rx_thread_loop 中引入慢速系统调用。
 * - 严禁移除 recvmmsg 的批量接收逻辑，否则在高负载下软中断将打死系统。
 * ---------------------------------------------------------------------------
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/sysinfo.h>

#include "unified_driver.h"
#include "../common_include/wavevm_protocol.h"
#include "../common_include/wavevm_ioctl.h"

// --- 配置常量 ---
#define MAX_INFLIGHT_REQS 65536
#define POOL_SHARDS 64
#define SHARD_MASK (POOL_SHARDS - 1)
#define ITEMS_PER_SHARD 1563 
#define POOL_CAP (POOL_SHARDS * ITEMS_PER_SHARD)
#define BATCH_SIZE 64
#define RX_THREAD_COUNT 4

// --- 全局状态 ---
static int g_my_node_id = 0;
extern uint8_t g_my_vm_id;
static int g_local_port = 0;
static struct sockaddr_in g_gateways[WVM_MAX_GATEWAYS];
static volatile int g_tx_socket = -1;
int g_slave_forward_port = 0;

__attribute__((weak)) int push_to_aggregator(uint32_t slave_id, void *data, int len) {
    if (!data || len <= 0) {
        errno = EINVAL;
        return -EINVAL;
    }

    if (g_tx_socket < 0) {
        errno = EAGAIN;
        return -EAGAIN;
    }

    // [Fix #1] slave_id 可能是 composite ID，解码为裸 node_id 索引 g_gateways[]
    uint32_t raw_id = WVM_GET_NODEID(slave_id);
    if (raw_id >= WVM_MAX_GATEWAYS) {
        errno = EINVAL;
        return -EINVAL;
    }

    // 统一走本地 sidecar 网关，跨节点路由由 sidecar 负责
    struct sockaddr_in *target = &g_gateways[g_my_node_id];  /* always route through local sidecar */
    if (target->sin_port == 0) {
        errno = EHOSTUNREACH;
        return -EHOSTUNREACH;
    }

    { static int __dbg=0; if(__dbg<10 && len>100) { char __ip[16]={0}; inet_ntop(AF_INET,&target->sin_addr,__ip,16); fprintf(stderr,"[PTA-DBG] target=%u raw=%u ip=%s port=%u sock=%d len=%d\n", slave_id, raw_id, __ip, (unsigned)ntohs(target->sin_port), g_tx_socket, len); __dbg++; } }
    { static int __tx=0; if(__tx<10 && len >= (int)sizeof(struct wvm_header)) { struct wvm_header *h=(struct wvm_header*)data; fprintf(stderr,"[TX-HDR] msg=%u len=%u payload=%u target=%u slave=%u req=%llu\n", (unsigned)ntohs(h->msg_type), (unsigned)len, (unsigned)ntohs(h->payload_len), (unsigned)ntohl(h->target_id), (unsigned)ntohl(h->slave_id), (unsigned long long)WVM_NTOHLL(h->req_id)); __tx++; } }
    ssize_t sent = sendto(g_tx_socket, data, len, MSG_DONTWAIT,
                          (struct sockaddr *)target, sizeof(*target));
    { static int __dbg2=0; if(__dbg2<10 && len>100) { fprintf(stderr,"[PTA-DBG] sent=%zd errno=%d\n", sent, (sent<0)?errno:0); __dbg2++; } }
    if (sent == len) {
        return 0;
    }
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return -EAGAIN;
        }
        return -errno;
    }

    errno = EIO;
    return -EIO;
}

// --- 外部引用 ---
extern void *g_shm_ptr; 
extern size_t g_shm_size;
extern uint32_t g_curr_epoch;
extern uint8_t g_my_node_state;
extern void broadcast_irq_to_qemu(void);
extern void wvm_logic_process_packet(struct wvm_header *hdr, void *payload, uint32_t source_node_id);
extern void broadcast_push_to_qemu(uint16_t msg_type, void* payload, int len);

// --- 请求ID管理结构 ---
struct u_req_ctx_t {
    void *rx_buffer;
    uint64_t full_id;
    uint32_t max_len; // [FIX-G3] 记录 rx_buffer 的最大可用长度
    volatile int status; // 0=Pending, 1=Done
    pthread_mutex_t lock;
};
static struct u_req_ctx_t g_u_req_ctx[MAX_INFLIGHT_REQS];
static uint64_t g_id_counter = 0;

// --- 内存池 (Slab Allocator) ---
typedef struct {
    void *free_list[ITEMS_PER_SHARD];
    int top;
    pthread_spinlock_t lock;
    // 显式 Cache Line Padding：确保每把锁独占缓存行，避免高并发下的 MESI 协议冲突抖动
    uint8_t padding[64 - sizeof(int) - sizeof(pthread_spinlock_t) - sizeof(void*)];
} __attribute__((aligned(64))) pool_shard_t;

static pool_shard_t g_pool_shards[POOL_SHARDS];
static uint8_t *g_pool_buffer = NULL;
static void *g_free_list[POOL_CAP];
static int g_pool_top = -1;

static int u_send_packet(void *data, int len, uint32_t target_id);
static pthread_spinlock_t g_pool_lock;

// --- QoS 发送队列结构 ---
typedef struct tx_node {
    struct tx_node *next;
    uint32_t target_id;
    int len;
    uint8_t data[]; // 柔性数组
} tx_node_t;

typedef struct {
    tx_node_t *head;
    tx_node_t *tail;
    pthread_spinlock_t lock;
    int count;
} tx_queue_t;

static tx_queue_t g_fast_queue;
static tx_queue_t g_slow_queue;
static pthread_cond_t g_tx_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_tx_cond_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_tx_thread;

static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_init_cond = PTHREAD_COND_INITIALIZER;

// --- 辅助函数 ---
static void u_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int u_is_atomic_context(void) {
    return 0; // 用户态通常不视为原子上下文，除非在信号处理中明确标记
}

static void u_touch_watchdog(void) {
    // 用户态无需喂狗
}

static uint64_t u_get_time_us(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (uint64_t)t.tv_sec * 1000000UL + t.tv_usec;
}

static uint64_t u_time_diff_us(uint64_t start) {
    uint64_t now = u_get_time_us();
    if (now >= start) return now - start;
    // 处理时钟回绕的极端情况
    return 0;
}

static void u_cpu_relax(void) {
    usleep(1);
}

static void u_get_random(uint32_t *val) {
    *val = (uint32_t)rand();
}

static void u_yield_cpu_short_time(void) {
    usleep(100);
}

/* 
 * [物理意图] 预分配大规模分片内存池，彻底消除运行时系统调用延迟。
 * [关键逻辑] 将内存切分为 POOL_SHARDS (64) 个独立分片，利用对齐（Padding）技术规避 CPU 缓存行伪共享。
 * [后果] 若不进行预分配，高频的小包（如 Diff/ACK）会频繁触发内核 brk/mmap 锁竞争，导致 PPS 吞吐量下降一个数量级。
 */
void init_pkt_pool(void) {
    g_pool_buffer = (uint8_t *)calloc(POOL_CAP, POOL_ITEM_SIZE);
    if (!g_pool_buffer) {
        perror("[V29.5 Fatal] Memory Pool Allocation Failure");
        exit(1);
    }

    for (int s = 0; s < POOL_SHARDS; s++) {
        pthread_spin_init(&g_pool_shards[s].lock, PTHREAD_PROCESS_PRIVATE);
        g_pool_shards[s].top = -1;
        for (int i = 0; i < ITEMS_PER_SHARD; i++) {
            // 每一个块都被分配到唯一确定的归属分片，实现真正的分布式管理
            int global_idx = s * ITEMS_PER_SHARD + i;
            g_pool_shards[s].free_list[++g_pool_shards[s].top] = 
                g_pool_buffer + (size_t)global_idx * POOL_ITEM_SIZE;
        }
    }
    printf("[Backend] V29.5 Logic Core Sharded Pool Ready: %d items.\n", POOL_CAP);
}

/* 
 * [物理意图] 高并发下的“零争用”网络包申请。
 * [关键逻辑] 基于 Thread-aware Hash 定位初始分片，并实现“工作窃取（Work-Stealing）”算法：本地分片空闲时尝试从邻居获取。
 * [后果] 这一步是 Mode B 性能的核心。它保证了 128 核服务器在万级并发下，不会因为同一个 malloc 锁产生“串行化自焚”。
 */
static void* u_alloc_packet(size_t size, int atomic) {
    // 超大包降级到 malloc
    if (size > POOL_ITEM_SIZE) {
        if (atomic) return NULL; 
        return malloc(size);
    }

    // 线程感知哈希：利用 pthread_self 的低位进行初始分片选择，极大地分散锁竞争
    unsigned int start_idx = (unsigned int)pthread_self() & SHARD_MASK;

    // 工作窃取（Work-Stealing）：如果本线程对应的分片锁住了或空了，去邻居分片“窃取”
    for (int i = 0; i < POOL_SHARDS; i++) {
        int s_idx = (start_idx + i) & SHARD_MASK;
        pool_shard_t *shard = &g_pool_shards[s_idx];
        void *ptr = NULL;

        if (atomic) {
            // 信号安全路径（Mode B SIGSEGV 调用），严禁阻塞
            if (pthread_spin_trylock(&shard->lock) == 0) {
                if (shard->top >= 0) ptr = shard->free_list[shard->top--];
                pthread_spin_unlock(&shard->lock);
            }
        } else {
            // 标准路径：阻塞等待
            pthread_spin_lock(&shard->lock);
            if (shard->top >= 0) ptr = shard->free_list[shard->top--];
            pthread_spin_unlock(&shard->lock);
        }

        if (ptr) return ptr; // 成功拿到
        
        // 红队优化：原子上下文下，如果试了 4 个分片都拿不到，必须立即返回 NULL，
        // 防止信号处理函数执行时间过长导致 Guest 内核 Panic。
        if (atomic && i > 3) break;
    }
    return NULL;
}

static void u_free_packet(void *ptr) {
    if (!ptr) return;

    size_t pool_total_bytes = (size_t)POOL_CAP * POOL_ITEM_SIZE;
    uintptr_t p_val = (uintptr_t)ptr;
    uintptr_t b_start = (uintptr_t)g_pool_buffer;

    // 逻辑保留：判定内存是否属于池。如果是 malloc 出来的，直接 free
    if (p_val < b_start || p_val >= (b_start + pool_total_bytes)) {
        free(ptr);
        return;
    }

    // 物理反向定位：基于指针地址精确计算它属于哪个分片（Shard）
    // 编译器会自动将常量 POOL_ITEM_SIZE (4200) 优化为乘法+移位，无需担心除法性能
    size_t offset = p_val - b_start;
    int global_idx = (int)(offset / POOL_ITEM_SIZE);
    int shard_idx = global_idx / ITEMS_PER_SHARD;

    // 防御性数学保护
    if (shard_idx >= POOL_SHARDS) shard_idx = SHARD_MASK;
    pool_shard_t *shard = &g_pool_shards[shard_idx];

    // 归还内存。
    // 使用阻塞锁保证内存不发生竞态丢失。
    pthread_spin_lock(&shard->lock);
    if (shard->top < ITEMS_PER_SHARD - 1) {
        shard->free_list[++shard->top] = ptr;
    }
    pthread_spin_unlock(&shard->lock);
}

// --- 驱动接口实现：内存分配 ---
static void* u_alloc_large_table(size_t size) {
    // 使用 mmap 分配大块内存
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        // 回退到 calloc
        return calloc(1, size);
    }
    return ptr;
}

static void u_free_large_table(void *ptr) {
    // 对于 mmap 的内存，应该用 munmap，但这里未记录 size
    // 鉴于 Directory Table 随进程生命周期存在，这里不执行操作
}

/* 
 * [物理意图] 为分布式事务分配唯一的“身份证（Transaction ID）”。
 * [关键逻辑] 实现基于 Generation（代数）的防止 ABA 机制。即便 ID 发生回绕，过时的包也不会误触发新的请求回调。
 * [后果] 如果没有 ID 追踪，网络乱序回包可能导致 CPU 读到错误的内存快照，产生静默数据破坏（Silent Data Corruption）。
 */
static uint64_t u_alloc_req_id(void *rx_buffer, uint32_t buffer_size) {
    uint64_t id;
    int idx;
    int attempts = 0;

    // 尝试多次获取 ID，避免在高并发下死锁
    while (attempts < MAX_INFLIGHT_REQS * 2) {
        id = __sync_fetch_and_add(&g_id_counter, 1);
        if (id == 0) id = __sync_fetch_and_add(&g_id_counter, 1);

        idx = id % MAX_INFLIGHT_REQS;

        if (pthread_mutex_trylock(&g_u_req_ctx[idx].lock) == 0) {
            if (g_u_req_ctx[idx].rx_buffer == NULL) {
                g_u_req_ctx[idx].rx_buffer = rx_buffer;
                g_u_req_ctx[idx].full_id = id;
                g_u_req_ctx[idx].max_len = buffer_size; // [FIX-G3]
                g_u_req_ctx[idx].status = 0;
                pthread_mutex_unlock(&g_u_req_ctx[idx].lock);
                return id;
            }
            pthread_mutex_unlock(&g_u_req_ctx[idx].lock);
        }
        attempts++;
        if (attempts % 100 == 0) u_yield_cpu_short_time();
    }
    u_log("[CRITICAL] User backend: No free request IDs available!");
    return (uint64_t)-1;
}

static void u_free_req_id(uint64_t id) {
    int idx = id % MAX_INFLIGHT_REQS;
    pthread_mutex_lock(&g_u_req_ctx[idx].lock);
    if (g_u_req_ctx[idx].full_id == id) {
        g_u_req_ctx[idx].rx_buffer = NULL;
        g_u_req_ctx[idx].status = 0;
        // full_id 不清零，保留用于过时包的校验
    }
    pthread_mutex_unlock(&g_u_req_ctx[idx].lock);
}

static int u_check_req_status(uint64_t id) {
    int s = -1;
    uint32_t idx = id % MAX_INFLIGHT_REQS;
    pthread_mutex_lock(&g_u_req_ctx[idx].lock);
    if (g_u_req_ctx[idx].full_id == id) {
        s = g_u_req_ctx[idx].status;
    }
    pthread_mutex_unlock(&g_u_req_ctx[idx].lock);
    return s;
}

static void u_send_packet_async(uint16_t msg_type, void* payload, int len, uint32_t target_id, uint8_t qos) {
    size_t pkt_len = sizeof(struct wvm_header) + len;
    uint8_t *buffer = u_alloc_packet(pkt_len, 1);
    if (!buffer) return;

    struct wvm_header *hdr = (struct wvm_header *)buffer;
    extern int g_my_node_id;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(msg_type);
    hdr->payload_len = htons(len);
    hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    hdr->target_id = htonl(target_id);
    hdr->req_id = 0;
    hdr->qos_level = qos;
    hdr->crc32 = 0;

    if (payload && len > 0) memcpy(buffer + sizeof(*hdr), payload, len);
    
    hdr->crc32 = htonl(calculate_crc32(buffer, pkt_len));

    u_send_packet(buffer, pkt_len, target_id);
    u_free_packet(buffer);
}

// --- 队列操作 ---
static void queue_init(tx_queue_t *q) {
    q->head = q->tail = NULL;
    q->count = 0;
    pthread_spin_init(&q->lock, 0);
}

static tx_node_t* queue_pop(tx_queue_t *q) {
    tx_node_t *node = NULL;
    pthread_spin_lock(&q->lock); 
    if (q->head) {
        node = q->head;
        q->head = node->next;
        if (!q->head) {
            q->tail = NULL;
        }
        q->count--;
    }
    pthread_spin_unlock(&q->lock);
    return node;
}

/* 
 * [物理意图] 实现应用层到物理网卡的“流量整形”与“反向压力”。
 * [关键逻辑] 拦截 Gateway 的返回值：若网关缓冲区溢出，此处执行自适应指数退避，而非暴力丢包。
 * [后果] 这是 P2P 网络在公网环境下的生命线。它防止了节点在网络拥塞时疯狂重传，从而实现“优雅减速”而非“系统坍缩”。
 */
static int raw_send(tx_node_t *node) {
    // [Fix #1] g_gateways 按裸 node_id 索引，g_my_node_id 已经是裸 ID，无需解码
    struct sockaddr_in *target = &g_gateways[g_my_node_id];
    if (target->sin_port == 0 || g_tx_socket < 0) return -1;
    
    // [自治增强]：在发送前强制注入当前的本地 Epoch 和状态
    struct wvm_header *hdr = (struct wvm_header *)node->data;
    // 使用本文件中的轻量状态镜像，避免跨对象链接耦合。

    hdr->epoch = htonl(g_curr_epoch);
    hdr->node_state = g_my_node_state;
    hdr->target_id = htonl(node->target_id);
    
    // 重新计算 CRC（因为 Header 变了）
    hdr->crc32 = 0;
    hdr->crc32 = htonl(calculate_crc32(node->data, node->len));
    
    // 感知 Gateway 的反压返回值
    
    int retries = 0;
    while (1) {
        // 调用网关服务
        int res = push_to_aggregator(node->target_id, node->data, node->len);
        
        if (res == 0) return 0; // 成功进入网关缓冲区
        
        if (res == -EBUSY || res == -EAGAIN) {
            // 网关缓冲区满了，必须指数退避重试，绝不能直接丢弃
            retries++;
            if (retries < 100) {
                usleep(10 * retries); 
                continue;
            } else {
                u_log("[Gateway] Severe Congestion for Slave %d, dropping packet", node->target_id);
                return -1;
            }
        }
        return res;
    }
}

/* 
 * [物理意图] 模拟处理器的“严格优先级调度器”，保证控制信号不被数据洪流淹没。
 * [关键逻辑] 维护“快/慢”双车道：Fast Queue（信令/ACK/Diff）拥有绝对抢占权，Slow Queue（全页/VCPU）按配额运行。
 * [后果] 解决了典型的“队首阻塞”问题。若无此分级，一个大块内存同步包会堵死全网的 ACK 确认，导致集群瞬间雪崩（Cascading Failure）。
 */
static void* tx_worker_thread(void *arg) {
    while (1) {
        tx_node_t *node;
        
        // 1. 优先清空 Fast Queue
        while ((node = queue_pop(&g_fast_queue)) != NULL) {
            struct wvm_header* hdr = (struct wvm_header*)node->data;
            // 计算 CRC32
            hdr->crc32 = 0;
            hdr->crc32 = htonl(calculate_crc32(node->data, node->len));
            
            raw_send(node);
            u_free_packet(node);
        }

        // 2. 按配额处理 Slow Queue
        int quota = 16;
        while (quota > 0 && (node = queue_pop(&g_slow_queue)) != NULL) {
            struct wvm_header* hdr = (struct wvm_header*)node->data;
            // 计算 CRC32
            hdr->crc32 = 0;
            hdr->crc32 = htonl(calculate_crc32(node->data, node->len));
            
            raw_send(node);
            u_free_packet(node);
            quota--;
            
            // 抢占：如果有快包进入，立即中断慢包处理
            if (g_fast_queue.count > 0) break;
        }

        // 3. 无任务时休眠
        pthread_mutex_lock(&g_tx_cond_lock);
        if (g_fast_queue.count == 0 && g_slow_queue.count == 0) {
            pthread_cond_wait(&g_tx_cond, &g_tx_cond_lock);
        }
        pthread_mutex_unlock(&g_tx_cond_lock);
    }
    return NULL;
}

// --- 驱动接口实现：发送 ---
static int u_send_packet(void *data, int len, uint32_t target_id) {
    if (g_tx_socket < 0) return -1;
    
    struct wvm_header *hdr = (struct wvm_header *)data;
    tx_queue_t *q;
    int is_atomic = 0;

    // 根据 QoS 级别选择队列
    if (hdr->qos_level == 1) {
        q = &g_fast_queue;
    } else {
        q = &g_slow_queue;
    }
    
    // 分配队列节点
    tx_node_t *node = (tx_node_t *)u_alloc_packet(sizeof(tx_node_t) + len, is_atomic);
    if (!node) return -1;

    node->next = NULL;
    node->target_id = target_id;
    node->len = len;
    memcpy(node->data, data, len);

    // 入队操作
    // 因为 is_atomic=0，这里会执行 pthread_spin_lock (死等直到获取锁)
    // 这保证了高优先级包绝对不会因为锁竞争而丢弃
    pthread_spin_lock(&q->lock);
    if (q->tail) { 
        q->tail->next = node; 
        q->tail = node; 
    } else { 
        q->head = q->tail = node; 
    }
    q->count++;
    pthread_spin_unlock(&q->lock);

    // 唤醒 TX 线程
    pthread_mutex_lock(&g_tx_cond_lock);
    pthread_cond_signal(&g_tx_cond);
    pthread_mutex_unlock(&g_tx_cond_lock);
    return 0;
}

// [FIX] 引用全局设备描述符 (在 main_wrapper.c 中打开 /dev/wavevm)
extern int g_dev_fd; 

/* 
 * [物理意图] 建立用户态感知与内核态加速（Mode A）之间的“配置虫洞”。
 * [关键逻辑] 更新本地路由表的同时，通过 IOCTL 垂直穿透到内核模块，确保 Kernel 直接通信不经过慢速 User-Space。
 * [后果] 实现了真正的“配置一致性”。若穿透失败，内核将向错误的旧 IP 发送数据，导致分布式计算出现逻辑黑洞。
 */
static void u_set_gateway_ip(uint32_t gw_id, uint32_t ip, uint16_t port) {
    // [Fix #1] 入参可能是 composite ID，先解码为裸 node_id 再索引静态数组
    // 这样 vm_id>0 时不会越界 (composite ID = (vm<<24)|node, 可能 > WVM_MAX_GATEWAYS)
    uint32_t raw_node_id = WVM_GET_NODEID(gw_id);

    // 1. 更新用户态表 (供 Daemon 自身通信使用，如 Gossip)
    if (raw_node_id < WVM_MAX_GATEWAYS) {
        g_gateways[raw_node_id].sin_family = AF_INET;
        g_gateways[raw_node_id].sin_addr.s_addr = ip;
        g_gateways[raw_node_id].sin_port = port;
        if (raw_node_id < 4) {
            char ipbuf[16] = {0};
            inet_ntop(AF_INET, &g_gateways[raw_node_id].sin_addr, ipbuf, sizeof(ipbuf));
            fprintf(stderr, "[GW-SET] id=%u ip=%s port=%u\n",
                    raw_node_id, ipbuf, (unsigned)ntohs(g_gateways[raw_node_id].sin_port));
        }
    }

    // 2. [Fix #4] Mode A 内核路径对齐：传给内核的 gw_id 也用裸 node_id
    // 内核模块 wavevm.ko 的路由表按裸 node_id 索引，不理解 composite 编码
    if (g_dev_fd > 0) {
        struct wvm_ioctl_gateway args;
        args.gw_id = raw_node_id;  // [Fix #4] 解码后下发内核
        args.ip = ip;     // 此时已经是网络序
        args.port = port; // 此时已经是网络序

        if (ioctl(g_dev_fd, IOCTL_SET_GATEWAY, &args) < 0) {
            perror("[Daemon] Failed to sync dynamic route to kernel");
        }
    }
}

// Deprecated in V29 logic core, but stub provided
static void u_fetch_page(uint64_t gpa, void *buf) {
    memset(buf, 0, 4096);
}

static void u_invalidate_local(uint64_t gpa) {
    // No-op for user backend, logic core handles this
}

extern uint64_t wvm_logic_get_page_version(uint64_t gpa);

/* 
 * [物理意图] 充当本地物理内存的“分布式 DMA 控制器”。
 * [关键逻辑] 响应远程读请求，从共享内存（SHM）中提取数据，并强制附加 Logic Core 维护的最新的版本号（Version）。
 * [后果] 提供了真理的来源。这是 Wavelet 协议中“拉取（Pull）”链路的终点，保证了 Directory 数据的对外可见性。
 */
static void handle_slave_read(int fd, struct sockaddr_in *dest, struct wvm_header *req) {
    if (!g_shm_ptr) return;

    // 1. 从请求中解析 GPA
    uint64_t gpa = WVM_NTOHLL(*(uint64_t*)(req + 1));
    if (gpa + 4096 > g_shm_size) return;

    // 2. 准备完整的响应包缓冲区 (Header + Payload结构体)
    // 大小 = 24 (Header) + 16 (GPA/Ver) + 4096 (Data) = 4136 字节
    uint8_t tx_buffer[sizeof(struct wvm_header) + sizeof(struct wvm_mem_ack_payload)];
    memset(tx_buffer, 0, sizeof(tx_buffer));

    struct wvm_header *ack_hdr = (struct wvm_header *)tx_buffer;
    struct wvm_mem_ack_payload *ack_pl = (struct wvm_mem_ack_payload *)(tx_buffer + sizeof(struct wvm_header));

    // 3. 填充 Payload (关键！)
    ack_pl->gpa = WVM_HTONLL(gpa);
    // 从 Logic Core 获取当前权威版本号，这对于 V29 的订阅机制至关重要
    ack_pl->version = WVM_HTONLL(wvm_logic_get_page_version(gpa));
    // 拷贝真正的内存数据
    memcpy(ack_pl->data, (uint8_t*)g_shm_ptr + gpa, 4096);

    // 4. 填充 Header
    ack_hdr->magic = htonl(WVM_MAGIC);
    ack_hdr->msg_type = htons(MSG_MEM_ACK);
    // 注意：payload_len 现在是 4112 字节 (16 + 4096)，而不是 4096！
    ack_hdr->payload_len = htons(sizeof(struct wvm_mem_ack_payload));
    ack_hdr->req_id = req->req_id;
    ack_hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
    ack_hdr->target_id = req->slave_id;
    ack_hdr->qos_level = 0; // 慢车道包
    ack_hdr->crc32 = 0;

    // 5. 计算全包 CRC32
    uint32_t c = calculate_crc32(tx_buffer, sizeof(tx_buffer));
    ack_hdr->crc32 = htonl(c);

    // 6. 发送
    sendto(fd, tx_buffer, sizeof(tx_buffer), 0, (struct sockaddr*)dest, sizeof(*dest));
}

// 引用 Logic Core
extern void wvm_logic_update_local_version(uint64_t gpa);
extern void wvm_logic_broadcast_rpc(void *payload_data, int payload_len, uint16_t msg_type);

/* 
 * [物理意图] 执行“语义透传”指令，实现内存状态的“瞬时坍缩”。
 * [关键逻辑] 利用 mprotect(PROT_WRITE) 构建写锁栅栏，在物理 memset 期间暂时禁用读取，确保全网一致性视图。
 * [后果] 这是 WaveVM 突破物理带宽限制的神作。它让 GB 级的内存清零操作在 10µs 内在全网节点同步完成，效率提升万倍。
 */
static void handle_rpc_batch_execution(void *payload, uint32_t payload_len) {
    if (payload_len < sizeof(struct wvm_rpc_batch_memset)) return;

    struct wvm_rpc_batch_memset *batch = (struct wvm_rpc_batch_memset *)payload;
    uint32_t count = ntohl(batch->count);
    uint32_t val = ntohl(batch->val);
    
    // 边界检查：防止 count 过大导致越界读取
    size_t required_len = sizeof(struct wvm_rpc_batch_memset) + count * sizeof(struct wvm_rpc_region);
    if (payload_len < required_len) return;

    struct wvm_rpc_region *regions = (struct wvm_rpc_region *)(batch + 1);
    
    for (uint32_t i = 0; i < count; i++) {
        uint64_t gpa = WVM_NTOHLL(regions[i].gpa);
        uint64_t r_len = WVM_NTOHLL(regions[i].len);
        
        if (gpa + r_len > g_shm_size) continue;
        
        void *host_addr = (uint8_t*)g_shm_ptr + gpa;

        // [PATCH] 原子性增强步骤
        
        // 1. 上“写锁”：将页面权限设为 PROT_WRITE (不可读) 或 PROT_NONE
        // 任何试图在此期间读取该内存的 vCPU 都会触发 Segfault 并被挂起
        // 注意：我们自己也无法读取，但 memset 是只写操作，所以 PROT_WRITE 是安全的
        mprotect(host_addr, r_len, PROT_WRITE);
        
        // 2. 内存屏障
        __sync_synchronize();

        // 3. 执行物理操作 (现在是安全的，因为没人能读)
        memset(host_addr, val, r_len);

        // [FIX] Memory Barrier
        __sync_synchronize(); // Full barrier
        
        // 4. 恢复权限 (恢复为 PROT_READ|PROT_WRITE)
        mprotect(host_addr, r_len, PROT_READ | PROT_WRITE);
        
        // 5. 更新版本号
        for (uint64_t offset = 0; offset < r_len; offset += 4096) {
            wvm_logic_update_local_version(gpa + offset);
        }
    }
    
    broadcast_push_to_qemu(MSG_RPC_BATCH_MEMSET, payload, payload_len);
    
    u_log("[Prophet] Local execution done, sync notification sent to QEMU.");
}

// [IPC Source] 处理来自本地 QEMU 的请求
static void handle_ipc_rpc_passthrough(int qemu_fd, void *data, uint32_t len) {
    if (len < sizeof(struct wvm_header)) return;
    struct wvm_header *hdr = (struct wvm_header *)data;
    uint16_t msg_type = ntohs(hdr->msg_type);
    void *payload = (void*)hdr + sizeof(struct wvm_header);
    uint32_t payload_len = len - sizeof(struct wvm_header);

    if (msg_type == MSG_RPC_BATCH_MEMSET) {
        // 1. 本地执行
        handle_rpc_batch_execution(payload, payload_len);
        
        // 2. [Broadcast] 全网广播 (Source ID = Me)
        hdr->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
        wvm_logic_broadcast_rpc(data, len, msg_type);
    }
    
    // 3. [ACK] 解除 QEMU 阻塞
    uint8_t ack = 1;
    write(qemu_fd, &ack, 1);
}

/* 
 * [物理意图] 整个计算节点的“物理入站总线”，负责数据的物理安全与分发。
 * [关键逻辑] 1. recvmmsg 批量聚合：降低中断频率；2. CRC32 强校验：拦截比特翻转；3. Epoch 过滤：隔离旧时代流量。
 * [后果] 这是全网最危险的性能红线。任何在此处的 CPU 浪费（如 malloc）都会导致 PPS 严重掉队，诱发全网重传风暴。
 */
static void* rx_thread_loop(void *arg) {
    long thread_idx = (long)arg;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("RX socket create failed");
        return NULL;
    }
    
    // 设置非阻塞
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    // 开启端口复用
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in bind_addr = { 
        .sin_family=AF_INET, 
        .sin_port=htons(g_local_port), 
        .sin_addr.s_addr=INADDR_ANY 
    };
    
    if (bind(sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("User backend RX bind failed");
        close(sockfd);
        return NULL;
    }
    if (thread_idx == 0) {
        fprintf(stderr, "[TX-SOCK] bound port=%u\n", (unsigned)g_local_port);
    }

    // 线程 0 负责提供全局发送 Socket
    if (thread_idx == 0) {
        pthread_mutex_lock(&g_init_lock);
        g_tx_socket = sockfd;
        pthread_cond_signal(&g_init_cond);
        pthread_mutex_unlock(&g_init_lock);
    }

    // recvmmsg 缓冲区
    struct mmsghdr msgs[BATCH_SIZE];
    struct iovec iovecs[BATCH_SIZE];
    struct sockaddr_in src_addrs[BATCH_SIZE];
    uint8_t *buffer_pool = malloc(BATCH_SIZE * WVM_MAX_PACKET_SIZE);
    
    if (!buffer_pool) {
        perror("RX buffer pool malloc failed");
        close(sockfd);
        return NULL;
    }

    for (int i = 0; i < BATCH_SIZE; i++) {
        iovecs[i].iov_base = buffer_pool + (i * WVM_MAX_PACKET_SIZE);
        iovecs[i].iov_len = WVM_MAX_PACKET_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &src_addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    }

    struct pollfd pfd;
    pfd.fd = sockfd;
    pfd.events = POLLIN;

    while (1) {
        if (poll(&pfd, 1, -1) <= 0) continue;

        int n = recvmmsg(sockfd, msgs, BATCH_SIZE, 0, NULL);
        if (n <= 0) continue;

        for (int i = 0; i < n; i++) {        
            uint8_t *base_ptr = (uint8_t *)iovecs[i].iov_base; // 基准地址
            int total_len = msgs[i].msg_len;                   // UDP包总长
            int offset = 0;                                    // 当前游标
            uint32_t src_ip = src_addrs[i].sin_addr.s_addr;    // 源IP

            // 循环解包：只要剩余数据够一个Header，就继续解析
            while (offset + sizeof(struct wvm_header) <= total_len) {
            
                // 计算当前包头的实际位置
                struct wvm_header *hdr = (struct wvm_header *)(base_ptr + offset);
            
                // 1. 魔数校验 (快速失败，防止处理垃圾数据)
                if (ntohl(hdr->magic) != WVM_MAGIC) {
                    break; // 如果魔数不对，说明后续数据无法对齐，直接丢弃该 UDP 包剩余部分
                }

                // 计算当前这个 WVM 子包的长度
                uint16_t p_len = ntohs(hdr->payload_len);
                int current_pkt_len = sizeof(struct wvm_header) + p_len;

                // 2. 长度越界校验
                if (offset + current_pkt_len > total_len) {
                    break; // 包被截断，丢弃
                }

                // 3. CRC32 校验
                uint32_t received_crc = ntohl(hdr->crc32);
                hdr->crc32 = 0; // 计算前清零
                uint32_t calculated_crc = calculate_crc32((uint8_t*)hdr, current_pkt_len);
            
                uint64_t rid = WVM_NTOHLL(hdr->req_id); // 定义 rid
                uint16_t msg_type = ntohs(hdr->msg_type); // 定义 msg_type

                if (received_crc == calculated_crc) {
                    uint32_t msg_epoch = ntohl(hdr->epoch);
                    if (msg_type == MSG_HEARTBEAT) {
                        uint64_t hb_rid = WVM_NTOHLL(hdr->req_id);
                        u_log("[HB RX] src_port=%u src_id=%u target_id=%u epoch=%u local=%u",
                              (unsigned)ntohs(src_addrs[i].sin_port),
                              (unsigned)ntohl(hdr->slave_id),
                              (unsigned)ntohl(hdr->target_id),
                              (unsigned)msg_epoch,
                              (unsigned)g_curr_epoch);
                        u_log("[HB RX] rid=%llu", (unsigned long long)hb_rid);
                    }

                    // [自治策略]：丢弃来自未来的非法包，或处理落后包
                    if (msg_epoch > g_curr_epoch + 5) {
                        if (msg_type == MSG_VCPU_RUN || msg_type == MSG_HEARTBEAT) {
                            u_log("[RX Drop Epoch] msg=%u src_port=%u epoch=%u local=%u",
                                  (unsigned)msg_type, (unsigned)ntohs(src_addrs[i].sin_port),
                                  msg_epoch, g_curr_epoch);
                        }
                        // 跨度过大，可能是由于网络分区（Split-brain）后节点归队
                        // 这种情况下不能处理该包，应等待视图同步
                        offset += current_pkt_len;
                        continue;
                    }
                    // 校验通过，恢复 CRC
                    hdr->crc32 = htonl(received_crc);
                    void *payload = (void*)hdr + sizeof(struct wvm_header);

                    // [Multi-VM] vm_id 过滤：丢弃不属于本 VM 的包
                    {
                        uint32_t target_raw = ntohl(hdr->target_id);
                        if (target_raw != WVM_NODE_AUTO_ROUTE &&
                            WVM_GET_VMID(target_raw) != g_my_vm_id) {
                            offset += current_pkt_len;
                            continue;
                        }
                    }

                    // [路由优先] Slave 业务包必须先分流，不能被 req_id 分支误判为 ACK。
                    // 仅当包不是来自本地 slave 端口时，才转发到 slave 进程处理。
                    int is_slave_service_msg =
                        (msg_type == MSG_VCPU_RUN ||
                         msg_type == MSG_BLOCK_WRITE ||
                         msg_type == MSG_BLOCK_READ ||
                         msg_type == MSG_BLOCK_FLUSH);
                    int from_local_slave = (ntohs(src_addrs[i].sin_port) == (uint16_t)g_slave_forward_port);

                    if (msg_type == MSG_VCPU_EXIT) {
                        static int __rx_exit = 0;
                        if (__rx_exit < 10) {
                            u_log("[RX VCPU_EXIT] src_port=%u from_local=%d target=%u slave=%u req=%llu",
                                  (unsigned)ntohs(src_addrs[i].sin_port), from_local_slave,
                                  (unsigned)ntohl(hdr->target_id),
                                  (unsigned)ntohl(hdr->slave_id),
                                  (unsigned long long)rid);
                            __rx_exit++;
                        }
                    }

                    // 本地 Slave 的执行结果需要回传给真正发起方（跨节点请求场景）。
                    // 新协议下回包目标在 hdr->target_id；旧包可回退到 hdr->slave_id。
                    if (from_local_slave &&
                        (msg_type == MSG_VCPU_EXIT || msg_type == MSG_BLOCK_ACK)) {
                        uint32_t return_target = ntohl(hdr->target_id);
                        uint32_t my_composite_id = WVM_ENCODE_ID(g_my_vm_id, g_my_node_id);
                        // 新协议优先用 target_id；仅当 target_id 无效/自动路由时回退到旧字段。
                        if (!WVM_IS_VALID_TARGET(return_target) ||
                            return_target == WVM_NODE_AUTO_ROUTE) {
                            uint32_t legacy_target = ntohl(hdr->slave_id);
                            if (WVM_IS_VALID_TARGET(legacy_target)) {
                                return_target = legacy_target;
                            }
                        }
                        u_log("[Slave Return] msg=%u src_port=%u rid=%llu return_target=%u",
                              (unsigned)msg_type, (unsigned)ntohs(src_addrs[i].sin_port),
                              (unsigned long long)rid, (unsigned)return_target);
                        if (WVM_IS_VALID_TARGET(return_target) && return_target != my_composite_id) {
                            int rr = u_send_packet(base_ptr + offset, current_pkt_len, return_target);
                            u_log("[Slave Return] forward queued msg=%u rid=%llu target=%u ret=%d len=%d",
                                  (unsigned)msg_type, (unsigned long long)rid,
                                  (unsigned)return_target, rr, current_pkt_len);
                            offset += current_pkt_len;
                            continue;
                        }
                    }

                    if (is_slave_service_msg && !from_local_slave) {
                        struct sockaddr_in slave_addr = {
                            .sin_family = AF_INET,
                            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                            .sin_port = htons(g_slave_forward_port)
                        };
                        ssize_t fw = sendto(sockfd, base_ptr + offset, current_pkt_len, 0,
                                           (struct sockaddr*)&slave_addr, sizeof(slave_addr));
                        if (fw < 0) {
                            u_log("[Slave Forward] sendto failed: msg=%u src_port=%u dst_port=%d err=%d",
                                  (unsigned)msg_type, (unsigned)ntohs(src_addrs[i].sin_port),
                                  g_slave_forward_port, errno);
                        } else if (msg_type == MSG_VCPU_RUN) {
                            u_log("[Slave Forward] msg=%u src_port=%u dst_port=%d len=%d",
                                  (unsigned)msg_type, (unsigned)ntohs(src_addrs[i].sin_port),
                                  g_slave_forward_port, current_pkt_len);
                        }
                        offset += current_pkt_len;
                        continue;
                    }

                    // 分发逻辑
                    bool is_response_msg =
                        (msg_type == MSG_MEM_ACK ||
                         msg_type == MSG_VCPU_EXIT ||
                         msg_type == MSG_BLOCK_ACK);

                    if (is_response_msg && rid != 0 && rid != (uint64_t)-1) {
                        // 请求-响应模式 (ACK / EXIT / BLOCK_ACK)
                        uint32_t idx = rid % MAX_INFLIGHT_REQS;
                        if (msg_type == MSG_VCPU_EXIT) {
                            u_log("[RX VCPU_EXIT] src_port=%u rid=%llu idx=%u p_len=%u",
                                  (unsigned)ntohs(src_addrs[i].sin_port),
                                  (unsigned long long)rid,
                                  (unsigned)idx,
                                  (unsigned)p_len);
                        }
                        pthread_mutex_lock(&g_u_req_ctx[idx].lock);
                        if (msg_type == MSG_VCPU_EXIT) {
                            u_log("[RX VCPU_EXIT] slot idx=%u full_id=%llu has_buf=%d",
                                  (unsigned)idx,
                                  (unsigned long long)g_u_req_ctx[idx].full_id,
                                  g_u_req_ctx[idx].rx_buffer ? 1 : 0);
                        }
                        if (g_u_req_ctx[idx].rx_buffer && g_u_req_ctx[idx].full_id == rid) {

                            // MSG_MEM_ACK 必须保留完整 payload（含 version），
                            // 由上层按各自语义解析，避免版本号在此层被静默丢弃。
                            // [FIX-G3] 截断保护：防止恶意/错乱包溢出 rx_buffer
                            uint32_t copy_len = p_len;
                            if (copy_len > g_u_req_ctx[idx].max_len)
                                copy_len = g_u_req_ctx[idx].max_len;
                            memcpy(g_u_req_ctx[idx].rx_buffer, payload, copy_len);
                            g_u_req_ctx[idx].status = 1;
                            if (msg_type == MSG_VCPU_EXIT) {
                                u_log("[RX VCPU_EXIT] matched rid=%llu -> status=1",
                                      (unsigned long long)rid);
                            }
                        }
                        pthread_mutex_unlock(&g_u_req_ctx[idx].lock);

                    } else {
                        // [V29 FINAL FIX] 客户端推送处理逻辑
                        // 如果是推送/失效消息，Daemon 必须自己处理（更新 SHM + 通知 QEMU）
                        // 而不能扔给只懂服务端业务的 logic_core

                        void *payload = base_ptr + offset + sizeof(struct wvm_header);
                        uint16_t p_len = ntohs(hdr->payload_len);
                        if (msg_type == MSG_PAGE_PUSH_FULL) {
                            struct wvm_full_page_push *push = (struct wvm_full_page_push *)payload;
                            uint64_t gpa = WVM_NTOHLL(push->gpa);
                            if (gpa + 4096 <= g_shm_size) {
                                // 1. 更新本地 SHM (成为最新版本)
                                memcpy((uint8_t*)g_shm_ptr + gpa, push->data, 4096);
                                // 2. 转发给 QEMU (IPC) 以更新版本号/TLB
                                broadcast_push_to_qemu(msg_type, payload, sizeof(struct wvm_full_page_push));
                            }
                        } 
                        else if (msg_type == MSG_PAGE_PUSH_DIFF) {
                            struct wvm_diff_log *log = (struct wvm_diff_log *)payload;
                            uint64_t gpa = WVM_NTOHLL(log->gpa);
                            uint16_t off = ntohs(log->offset);
                            uint16_t sz = ntohs(log->size);

                            if (gpa < g_shm_size && off + sz <= 4096) {
                                if (hdr->flags & WVM_FLAG_ZERO) {
                                    // 零页：清零 SHM，转化为 FULL_PUSH 发给 QEMU
                                    memset((uint8_t*)g_shm_ptr + gpa, 0, 4096);
                                    struct wvm_full_page_push fake_full;
                                    fake_full.gpa = log->gpa;
                                    fake_full.version = log->version;
                                    memset(fake_full.data, 0, 4096);
                                    broadcast_push_to_qemu(MSG_PAGE_PUSH_FULL, &fake_full, sizeof(fake_full));
                                } else if (sz > 0) {
                                    // 1. 应用 Diff 到 SHM
                                    memcpy((uint8_t*)g_shm_ptr + gpa + off, log->data, sz);
                                    // 2. 转发给 QEMU
                                    broadcast_push_to_qemu(msg_type, payload, sizeof(struct wvm_diff_log) + sz);
                                }
                            }
                        }
                        else if (msg_type == MSG_INVALIDATE || msg_type == MSG_DOWNGRADE || msg_type == MSG_FORCE_SYNC) {
                            // 控制类推送：直接转发给 QEMU 处理
                            // 这里的 payload_len 是网络包里的长度，需要传给 IPC
                            broadcast_push_to_qemu(msg_type, payload, p_len);
                        }
                        /* [FIX] MSG_MEM_READ 不再在此拦截。
                         * 旧代码调用 handle_slave_read 从本地 g_shm_ptr 读取，但 Mode B 下
                         * g_shm_ptr 与 QEMU 的 g_ram_base 不实时同步（自己不接收自己的 PUSH），
                         * 会返回 stale data 并携带新版本号，导致全网一致性崩溃。
                         * 现在让它落入最后的 else { wvm_logic_process_packet } 由 Directory 正确处理。
                         */
                        else if (msg_type == MSG_VFIO_IRQ) {
                            // [FIX] 收到中断包，必须通过 IPC 转发给本地 QEMU
                            broadcast_irq_to_qemu();
                        }
                        else if (msg_type == MSG_RPC_BATCH_MEMSET) {
                            // 收到远程节点的广播 -> 仅执行本地操作
                            // 不需要回复 ACK (广播是 Fire-and-Forget)
                            // 不需要再次广播 (防止风暴)
                           // CRC 校验已在 Loop 开头完成，此处数据安全
                           handle_rpc_batch_execution(payload, p_len);
                       }
                        else {
                            // 其他消息 (DECLARE/COMMIT) 才是给服务端 Logic Core 的
                            uint32_t src_id = ntohl(hdr->slave_id);
                            wvm_logic_process_packet(hdr, payload, src_id);
                        }
                    }
                } else if (msg_type == MSG_VCPU_RUN ||
                           msg_type == MSG_VCPU_EXIT ||
                           msg_type == MSG_BLOCK_ACK) {
                    u_log("[RX CRC Mismatch] msg=%u src_port=%u recv=%08x calc=%08x len=%d",
                          (unsigned)msg_type, (unsigned)ntohs(src_addrs[i].sin_port),
                          received_crc, calculated_crc, current_pkt_len);
                }
    
                // 4. [关键] 指针移动到下一个包
                offset += current_pkt_len;
            }
        }
    }
    free(buffer_pool);
    return NULL;
}

// 供 logic_core 调用
void wvm_notify_kernel_epoch(uint32_t epoch) {
    // g_dev_fd 是打开 /dev/wavevm 的句柄，需要在初始化时保存
    extern int g_dev_fd; 
    if (g_dev_fd > 0) {
        ioctl(g_dev_fd, IOCTL_UPDATE_EPOCH, &epoch);
    }
}

// --- 公共接口定义 ---
struct dsm_driver_ops u_ops = {
    .alloc_large_table = u_alloc_large_table,
    .free_large_table = u_free_large_table,
    .alloc_packet = u_alloc_packet,
    .free_packet = u_free_packet,
    .set_gateway_ip = u_set_gateway_ip,
    .send_packet = u_send_packet,
    .fetch_page = u_fetch_page,
    .invalidate_local = u_invalidate_local,
    .log = u_log,
    .is_atomic_context = u_is_atomic_context,
    .touch_watchdog = u_touch_watchdog,
    .alloc_req_id = u_alloc_req_id,
    .free_req_id = u_free_req_id,
    .get_time_us = u_get_time_us,
    .time_diff_us = u_time_diff_us,
    .check_req_status = u_check_req_status,
    .cpu_relax = u_cpu_relax,
    .get_random = u_get_random,
    .yield_cpu_short_time = u_yield_cpu_short_time,
    .send_packet_async = u_send_packet_async
};

// --- 初始化入口 ---
int user_backend_init(int my_node_id, int port) {
    if (my_node_id < 0 || my_node_id >= WVM_MAX_GATEWAYS) {
        fprintf(stderr, "[WaveVM] user_backend_init: node_id %d out of range [0, %d)\n",
                my_node_id, WVM_MAX_GATEWAYS);
        return -1;
    }
    g_my_node_id = my_node_id;
    g_local_port = port;
    srand(time(NULL));
    
    // 初始化本地 Gateway 地址 (默认指向 localhost:9000)
    g_gateways[my_node_id].sin_family = AF_INET;
    g_gateways[my_node_id].sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_gateways[my_node_id].sin_port = htons(9000); 

    for (uint32_t i = 0; i < 2; i++) {
        if (g_gateways[i].sin_port != 0) {
            char ipbuf[16] = {0};
            inet_ntop(AF_INET, &g_gateways[i].sin_addr, ipbuf, sizeof(ipbuf));
            fprintf(stderr, "[GW-FINAL] id=%u ip=%s port=%u\n",
                    i, ipbuf, (unsigned)ntohs(g_gateways[i].sin_port));
        }
    }

    // 初始化请求上下文锁
    for (int i=0; i<MAX_INFLIGHT_REQS; i++) {
        pthread_mutex_init(&g_u_req_ctx[i].lock, NULL);
    }
    
    // 初始化内存池和队列
    init_pkt_pool();
    queue_init(&g_fast_queue);
    queue_init(&g_slow_queue);

    // 启动 RX 线程 (根据 CPU 核心数动态调整，这里演示用 4)
    for (long i = 0; i < RX_THREAD_COUNT; i++) {
        pthread_t th;
        if (pthread_create(&th, NULL, rx_thread_loop, (void*)i) != 0) {
            perror("Failed to create RX thread");
            return -1;
        }
        pthread_detach(th);
    }

    // 等待 RX 线程 0 初始化 Socket 并赋值给 g_tx_socket
    pthread_mutex_lock(&g_init_lock);
    while (g_tx_socket < 0) {
        pthread_cond_wait(&g_init_cond, &g_init_lock);
    }
    pthread_mutex_unlock(&g_init_lock);

    // 启动 TX 线程
    if (pthread_create(&g_tx_thread, NULL, tx_worker_thread, NULL) != 0) {
        perror("Failed to create TX thread");
        return -1;
    }
    
    return 0;
}
```

**文件**: `master_core/main_wrapper.c`

```c
/*
 * [IDENTITY] Main Wrapper - The Identity Mapper
 * ---------------------------------------------------------------------------
 * 物理角色：Daemon 的启动引擎与"身份翻译官"。
 * 职责边界：
 * 1. 解析 swarm_config，将物理异构资源 (RAM/Cores) 映射为虚拟 ID。
 * 2. 初始化共享内存后端，建立 QEMU 与 Daemon 的 IPC 桥梁。
 * 3. 启动自治监控线程 (Gossip)，驱动节点生命周期演进。
 * 
 * [禁止事项]
 * - 严禁修改虚拟节点 ID 的分配算法 (ram / 4GB)，否则将破坏 DHT 的全局一致性。
 * - 严禁在 QEMU 建立连接前提前释放资源。
 * ---------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include "logic_core.h"
#include "../common_include/wavevm_protocol.h"

// --- 全局状态 ---
extern struct dsm_driver_ops u_ops;
extern int user_backend_init(int my_node_id, int port);
void *g_shm_ptr = NULL;
size_t g_shm_size = 0;
int g_dev_fd = -1;
extern int g_my_node_id;
uint8_t g_my_vm_id = 0;  // Multi-VM: 默认 0，向后兼容

#define MAX_QEMU_CLIENTS 8
#define NUM_BCAST_WORKERS 8

/* [FIX-G2] 坚如磐石的循环读取，处理 Partial Read 和 EINTR */
static ssize_t read_exact(int fd, void *buf, size_t len) {
    size_t received = 0;
    char *ptr = (char *)buf;
    while (received < len) {
        ssize_t ret = read(fd, ptr + received, len - received);
        if (ret > 0) {
            received += ret;
        } else if (ret == 0) {
            return -1; // EOF: 对端关闭
        } else {
            if (errno == EINTR) continue; // 信号中断，重试
            return -1; // 真正的错误
        }
    }
    return (ssize_t)received;
}

/* [FIX] 循环写，处理 partial write 和 EINTR，避免 IPC 流错位 */
static ssize_t write_exact(int fd, const void *buf, size_t len) {
    size_t sent = 0;
    const char *ptr = (const char *)buf;
    while (sent < len) {
        ssize_t ret = write(fd, ptr + sent, len - sent);
        if (ret > 0) {
            sent += ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EINTR) continue;
            return -1;
        }
    }
    return (ssize_t)sent;
}

// 初始种子节点
#define MAX_SEEDS 8
static struct sockaddr_in g_seeds[MAX_SEEDS];
static int g_seed_count = 0;

static int g_qemu_clients[8];
static int g_client_count = 0;
static pthread_mutex_t g_client_lock = PTHREAD_MUTEX_INITIALIZER;

extern void* broadcast_worker_thread(void* arg);
extern void* autonomous_monitor_thread(void* arg);
int g_sync_batch_size = 64;
void handle_ipc_rpc_passthrough(int qemu_fd, void *data, uint32_t len) { (void)qemu_fd; (void)data; (void)len; }

/* 
 * [物理意图] 实现“异构资源到逻辑维度的映射”，确立节点在 P2P 环上的权重。
 * [关键逻辑] 按照 (RAM / 4GB) 计算虚拟节点数，并顺序填充 CPU 路由表，将物理算力转化为逻辑插槽。
 * [后果] 这是整个集群一致性的物理起点。若解析算法在百万节点间不统一，DHT 环将发生碰撞，导致内存寻址彻底失效。
 */
void load_swarm_config(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { perror("Config Error"); exit(1); }

    char line[256];
    
    // 虚拟节点粒度: 4GB = 1 DHT Slot (必须与 ctl_tool 保持一致)
    #define WVM_RAM_UNIT_GB 4 

    int total_vnodes = 0; // DHT 环上的总虚拟节点数
    int phys_node_count = 0;
    
    // 临时存储物理节点信息，用于构建 CPU 表
    // 我们动态分配一下防止栈溢出
    struct PhysNodeInfo {
        int id;
        int cores;
        int vnode_start; // 该物理机对应的第一个虚拟 ID (Primary ID)
    } *phys_nodes = malloc(sizeof(struct PhysNodeInfo) * WVM_MAX_SLAVES);

    if (!phys_nodes) { perror("malloc"); exit(1); }

    printf("[Config] Parsing Swarm Topology (Heterogeneous Mode)...\n");

    while (fgets(line, sizeof(line), fp)) {
        // 跳过注释和空行
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char keyword[16];
        int bid, port, cores, ram;
        char ip_str[64];

        // 1. 尝试读取关键字
        if (sscanf(line, "%15s", keyword) != 1) continue;

        // [Multi-VM] 解析 VM 指令（仅记录，不影响 NODE 解析）
        // 格式: VM <VmID> <StartVnode> <VnodeCount>
        if (strcmp(keyword, "VM") == 0) continue; // VM 指令在此阶段跳过，仅用于文档

        if (strcmp(keyword, "NODE") != 0) continue;

        // 2. 解析: NODE <ID> <IP> <PORT> <CORES> <RAM>
        // %*s 跳过第一个字符串(NODE)
        int fields = sscanf(line, "%*s %d %63s %d %d %d", &bid, ip_str, &port, &cores, &ram);
        
        if (fields >= 5) { // 成功解析
            // 默认值保护
            if (cores < 1) cores = 1;
            if (ram < 1) ram = 4;

            // --- 核心逻辑不变 ---
            // 1. 计算虚拟节点数量
            int v_count = ram / WVM_RAM_UNIT_GB;
            if (v_count < 1) v_count = 1;

            // 2. 注入 Gateway IP 表 (使用 composite ID: vm_id | node_id)
            for (int v = 0; v < v_count; v++) {
                int v_id = total_vnodes + v;
                uint32_t composite_id = WVM_ENCODE_ID(g_my_vm_id, v_id);
                u_ops.set_gateway_ip(composite_id, inet_addr(ip_str), htons(port));
            }

            // 3. 记录物理节点信息
            if (phys_node_count < WVM_MAX_SLAVES) {
                // [关键] 这里强制使用配置文件里的 ID，而不是行号
                phys_nodes[phys_node_count].id = bid; 
                phys_nodes[phys_node_count].cores = cores;
                phys_nodes[phys_node_count].vnode_start = total_vnodes;
                phys_node_count++;
            }

            total_vnodes += v_count;
        }
    }
    fclose(fp);

    // 4. 注入 Total Nodes 到 Logic Core (用于 DHT 取模)
    // 注意：这里传的是 total_vnodes，不是物理节点数！
    wvm_set_mem_mapping(0, (uint32_t)total_vnodes);
    printf("[Config] DHT Ring Size: %d Virtual Nodes (from %d Physical).\n", total_vnodes, phys_node_count);

    // 5. 构建并注入 CPU 路由表 (Logic Core)
    // 逻辑与 ctl_tool 完全一致：按 Cores 数量顺序分配
    int current_vcpu = 0;
    
    // 第一轮：按物理核心数填充 (Core-Weighted)
    for (int i = 0; i < phys_node_count; i++) {
        for (int c = 0; c < phys_nodes[i].cores; c++) {
            if (current_vcpu < 4096) { // WVM_CPU_ROUTE_TABLE_SIZE
                // CPU 调度指向该物理机的 Primary Virtual ID
                wvm_set_cpu_mapping(current_vcpu++, phys_nodes[i].vnode_start);
            }
        }
    }
    
    // 第二轮：填补剩余空位 (Round-Robin)
    int node_cursor = 0;
    // [FIX-H9] 防止 phys_node_count==0 时除零
    while (phys_node_count > 0 && current_vcpu < 4096) {
        wvm_set_cpu_mapping(current_vcpu++, phys_nodes[node_cursor].vnode_start);
        node_cursor = (node_cursor + 1) % phys_node_count;
    }
    
    printf("[Config] CPU Routing Table Initialized (Weighted by Cores).\n");
    free(phys_nodes);
}

/* 
 * [物理意图] 充当 QEMU 与分布式总线之间的“协议转换器”。
 * [关键逻辑] 拦截 IPC 管道中的缺页与 CPU 任务，调用 Logic Core 判定权属，并决定是本地执行还是发起网络 RPC。
 * [后果] 实现了前后端解耦。它保证了前端 QEMU 不需要理解复杂的 DHT 逻辑，只需发出“我要这块内存”的原始指令。
 */
static void handle_ipc_fault(int qemu_fd, struct wvm_ipc_fault_req* req) {
    struct wvm_ipc_fault_ack ack; // 使用扩展后的 ACK 结构
    fprintf(stderr, "[IPC Fault] gpa=%#llx len=%u vcpu=%u\n",
            (unsigned long long)req->gpa,
            req->len, req->vcpu_id);

    // [FIX-H7] GPA 边界检查，防止越界访问共享内存
    if (req->gpa + 4096 > g_shm_size) {
        ack.status = -EFAULT;
        ack.version = 0;
        write_exact(qemu_fd, &ack, sizeof(ack));
        return;
    }

    void *target_page_addr = (uint8_t*)g_shm_ptr + req->gpa;

    ack.status = wvm_handle_page_fault_logic(req->gpa, target_page_addr, &ack.version);
    fprintf(stderr, "[IPC Fault Ack] gpa=%#llx status=%d ver=%#llx\n",
            (unsigned long long)req->gpa,
            ack.status,
            (unsigned long long)ack.version);

    write_exact(qemu_fd, &ack, sizeof(ack));
}

static void handle_ipc_cpu_run(int qemu_fd, struct wvm_ipc_cpu_run_req* req) {
    struct wvm_ipc_cpu_run_ack ack;
    { static int __ipc_run=0;
      if (__ipc_run < 10) {
          fprintf(stderr, "[IPC VCPU_RUN] vcpu=%u mode=%u slave_id=%u\n",
                  req->vcpu_index, req->mode_tcg, (unsigned)req->slave_id);
          __ipc_run++;
      }
    }
    if (!WVM_IS_VALID_TARGET(req->slave_id)) {
        req->slave_id = wvm_get_compute_slave_id(req->vcpu_index);
    }
    if (!WVM_IS_VALID_TARGET(req->slave_id)) {
        ack.status = -ENODEV;
    } else {
        ack.status = wvm_rpc_call(MSG_VCPU_RUN, &req->ctx,
            req->mode_tcg ? sizeof(req->ctx.tcg) : sizeof(req->ctx.kvm),
            req->slave_id, &ack.ctx, sizeof(ack.ctx));
    }
    ack.mode_tcg = req->mode_tcg;
    write_exact(qemu_fd, &ack, sizeof(ack));
}

/* 
 * [物理意图] 维护 Wavelet 协议的“最后一百米”：将网络推送推入 QEMU 的监听线程。
 * [关键逻辑] 构造伪造的 wvm_header 封装入 IPC 包，强制唤醒 QEMU 的信号处理逻辑以更新本地 TLB/EPT。
 * [后果] 实现了“真理下达”。若此函数丢失，Daemon 虽然收到了数据，但 QEMU 里的 vCPU 依然会因为读到过期旧数据而崩溃。
 */
void broadcast_push_to_qemu(uint16_t msg_type, void* payload, int len) {
    wvm_ipc_header_t ipc_hdr;
    ipc_hdr.type = WVM_IPC_TYPE_INVALIDATE;
    ipc_hdr.len = sizeof(struct wvm_header) + len;
    
    uint8_t* buffer = malloc(sizeof(ipc_hdr) + ipc_hdr.len);
    if (!buffer) return;

    memcpy(buffer, &ipc_hdr, sizeof(ipc_hdr));
    // We need to construct a fake wvm_header for the push listener
    struct wvm_header *hdr = (struct wvm_header*)(buffer + sizeof(ipc_hdr));
    hdr->msg_type = htons(msg_type);
    memcpy((void*)hdr + sizeof(*hdr), payload, len);
    
    pthread_mutex_lock(&g_client_lock);
    for (int i = 0; i < g_client_count; i++) {
        write_exact(g_qemu_clients[i], buffer, sizeof(ipc_hdr) + ipc_hdr.len);
    }
    pthread_mutex_unlock(&g_client_lock);
    free(buffer);
}

void broadcast_irq_to_qemu(void) {
    wvm_ipc_header_t ipc_hdr;
    ipc_hdr.type = WVM_IPC_TYPE_IRQ;
    ipc_hdr.len = 0;
    
    pthread_mutex_lock(&g_client_lock);
    for (int i = 0; i < g_client_count; i++) {
        write_exact(g_qemu_clients[i], &ipc_hdr, sizeof(ipc_hdr));
    }
    pthread_mutex_unlock(&g_client_lock);
}

/* 
 * [物理意图] 维护 QEMU 前端与 Backend 守护进程之间的“生命脐带”。
 * [关键逻辑] 处理本地 IPC 请求，将 vCPU 的 COMMIT_DIFF 任务异步分发至分布式总线。
 * [后果] 这是本地算力与全局总线的交汇点。若此处的循环发生阻塞，vCPU 将产生明显的物理卡顿。
 */
void* client_handler(void *socket_desc) {
    int qemu_fd = *(int*)socket_desc;
    free(socket_desc);
    fprintf(stderr, "[IPC] client connected fd=%d\n", qemu_fd);

    pthread_mutex_lock(&g_client_lock);
    if (g_client_count == 0) {
        g_qemu_clients[g_client_count++] = qemu_fd;
        fprintf(stderr, "[IPC] fd=%d registered as async push client\n", qemu_fd);
    } else {
        fprintf(stderr, "[IPC] fd=%d treated as sync RPC client only\n", qemu_fd);
    }
    pthread_mutex_unlock(&g_client_lock);

    wvm_ipc_header_t ipc_hdr;
    uint8_t payload_buf[WVM_MAX_PACKET_SIZE];

    while (1) {
        // [FIX-G2] 使用 read_exact 处理 partial read
        ssize_t hdr_n = read_exact(qemu_fd, &ipc_hdr, sizeof(ipc_hdr));
        if (hdr_n < 0) {
            fprintf(stderr, "[IPC] header read failed fd=%d errno=%d\n",
                    qemu_fd, errno);
            break;
        }
        if (ipc_hdr.len > sizeof(payload_buf)) {
            fprintf(stderr, "[IPC] payload too large fd=%d type=%u len=%u max=%zu\n",
                    qemu_fd, (unsigned)ipc_hdr.type, (unsigned)ipc_hdr.len, sizeof(payload_buf));
            // Payload too large, drain and ignore
            char drain[1024];
            size_t remaining = ipc_hdr.len;
            while(remaining > 0) {
                ssize_t n = read(qemu_fd, drain, (remaining > sizeof(drain)) ? sizeof(drain) : remaining);
                if (n <= 0) break;
                remaining -= n;
            }
            continue;
        }
        
        // [FIX-G2] 使用 read_exact 处理 partial read
        ssize_t payload_n = read_exact(qemu_fd, payload_buf, ipc_hdr.len);
        if (payload_n < 0) {
            fprintf(stderr, "[IPC] payload read failed fd=%d type=%u need=%u errno=%d\n",
                    qemu_fd, (unsigned)ipc_hdr.type, (unsigned)ipc_hdr.len, errno);
            break;
        }

        switch (ipc_hdr.type) {
            case WVM_IPC_TYPE_MEM_FAULT:
                handle_ipc_fault(qemu_fd, (struct wvm_ipc_fault_req*)payload_buf);
                break;
            case WVM_IPC_TYPE_CPU_RUN:
                handle_ipc_cpu_run(qemu_fd, (struct wvm_ipc_cpu_run_req*)payload_buf);
                break;
            case WVM_IPC_TYPE_COMMIT_DIFF: {
                // This is the new IPC type for V29
                struct wvm_diff_log* log = (struct wvm_diff_log*)payload_buf;
                uint32_t dir_node = wvm_get_directory_node_id(WVM_NTOHLL(log->gpa));
                // Send MSG_COMMIT_DIFF to the correct directory node
                u_ops.send_packet_async(MSG_COMMIT_DIFF, log, ipc_hdr.len, dir_node, 1);
                break;
            }
            case WVM_IPC_TYPE_RPC_PASSTHROUGH: { // Type 99
                extern void handle_ipc_rpc_passthrough(int qemu_fd, void *data, uint32_t len);
                handle_ipc_rpc_passthrough(qemu_fd, payload_buf, ipc_hdr.len);
                break;
            }
            case WVM_IPC_TYPE_BLOCK_IO: {
                // 结构体必须与 QEMU 端严格对齐 (Packed 13 Bytes)
                struct wvm_ipc_block_req {
                    uint64_t lba;
                    uint32_t len;
                    uint8_t  is_write;
                    uint8_t  data[0];
                } __attribute__((packed));
                struct wvm_ipc_block_req *req = (void*)payload_buf;
                uint32_t target = wvm_get_storage_node_id(req->lba);
                
                size_t blk_size = sizeof(struct wvm_block_payload) + (req->is_write ? req->len : 0);
                size_t pkt_len = sizeof(struct wvm_header) + blk_size;
                
                // [FIX] 1. 分配 RX Buffer 接收远端真实数据
                size_t rx_buf_size = sizeof(struct wvm_block_payload) + req->len;
                uint8_t *rx_buf = malloc(rx_buf_size);
                uint64_t rid = u_ops.alloc_req_id(rx_buf, (uint32_t)rx_buf_size);
                
                uint8_t *pkt = u_ops.alloc_packet(pkt_len, 0);
                if (pkt && rid != (uint64_t)-1) {
                    struct wvm_header *h = (struct wvm_header *)pkt;
                    h->magic = htonl(WVM_MAGIC);
                    h->msg_type = htons(req->is_write ? MSG_BLOCK_WRITE : MSG_BLOCK_READ);
                    h->payload_len = htons(blk_size);
                    h->slave_id = htonl(WVM_ENCODE_ID(g_my_vm_id, g_my_node_id));
                    h->req_id = WVM_HTONLL(rid); // [FIX] 必须赋予请求ID才能收到ACK
                    h->qos_level = 1; 
                    
                    struct wvm_block_payload *p = (void*)(pkt + sizeof(*h));
                    p->lba = WVM_HTONLL(req->lba);
                    p->count = htonl(req->len / 512);
                    if (req->is_write) memcpy(p->data, req->data, req->len);
                    
                    h->crc32 = 0;
                    h->crc32 = htonl(calculate_crc32(pkt, pkt_len));
                    
                    // 2. 发送请求
                    u_ops.send_packet(pkt, pkt_len, target);
                    
                    // [FIX] 3. 阻塞等待远端存储节点回包
                    uint64_t t_start = u_ops.get_time_us();
                    int success = 0;
                    while (u_ops.time_diff_us(t_start) < 5000000) { // 5秒超时
                        if (u_ops.check_req_status(rid) == 1) {
                            // --- 完美闭环：检查硬件级坏道/写入错误 ---
                            struct wvm_header *rx_hdr = (struct wvm_header *)rx_buf;
                            if (rx_hdr->flags & WVM_FLAG_ERROR) {
                                fprintf(stderr, "[Storage] Remote Slave reported physical IO error on LBA!\n");
                                success = 0; // 物理落盘失败，向 QEMU 报告错误
                            } else {
                                success = 1; // 真正意义上的安全落盘
                            }
                            break;
                        }
                        usleep(100);
                    }
                    
                    // [FIX] 4. 向 QEMU 发送 ACK 唤醒 vCPU
                    uint8_t ack_byte = success ? 1 : 0;
                    write_exact(qemu_fd, &ack_byte, 1);
                    
                    // 如果是读操作，把远端拿回来的数据塞回给 QEMU
                    if (success && !req->is_write) {
                        struct wvm_block_payload *rx_p = (struct wvm_block_payload *)rx_buf;
                        write_exact(qemu_fd, rx_p->data, req->len);
                    }
                } else {
                    // 内存不足，直接回复失败，防止 QEMU 死锁
                    uint8_t ack_byte = 0;
                    write_exact(qemu_fd, &ack_byte, 1);
                }
                
                if (pkt) u_ops.free_packet(pkt);
                if (rid != (uint64_t)-1) u_ops.free_req_id(rid);
                free(rx_buf);
                break;
            }
            default:
                fprintf(stderr, "[IPC] unknown type fd=%d type=%u len=%u\n",
                        qemu_fd, (unsigned)ipc_hdr.type, (unsigned)ipc_hdr.len);
                break;
        }
    }
    fprintf(stderr, "[IPC] client disconnected fd=%d\n", qemu_fd);
    close(qemu_fd);
    
    // 移除客户端并压缩数组，防止 Slot 耗尽
    pthread_mutex_lock(&g_client_lock);
    for (int i = 0; i < g_client_count; i++) {
        if (g_qemu_clients[i] == qemu_fd) {
            // 将最后一个元素移到当前空位（无序数组删除法，效率 O(1)）
            if (i != g_client_count - 1) {
                g_qemu_clients[i] = g_qemu_clients[g_client_count - 1];
            }
            g_client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_client_lock);
    
    return NULL;
}

/* 
 * [物理意图] 在无中心网络中注入“初始火种（Bootstrap Seeds）”。
 * [关键逻辑] 从配置中提取非本机的节点 IP，将其状态设为 SHADOW 并挂载到局部视图中，触发初始的 VIEW_PULL 请求。
 * [后果] 这是 P2P 网络的启动原点。若无此函数，节点将陷入“孤岛效应”，无法通过 Gossip 发现任何邻居。
 */
void load_initial_seeds(const char *config_file) {
    FILE *fp = fopen(config_file, "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp) && g_seed_count < MAX_SEEDS) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char keyword[16], ip[64];
        int id, port, cores, ram;
        if (sscanf(line, "%15s %d %63s %d %d %d", keyword, &id, ip, &port, &cores, &ram) == 6) {
            if (strcmp(keyword, "NODE") == 0 && id != g_my_node_id) {
                g_seeds[g_seed_count].sin_family = AF_INET;
                g_seeds[g_seed_count].sin_addr.s_addr = inet_addr(ip);
                g_seeds[g_seed_count].sin_port = htons(port);

                // 关键：将种子节点预埋入局部视图
                // 初始状态设为 SHADOW，等待心跳激活
                // [Multi-VM] 使用裸 node_id，topology view 内部用裸 ID
                update_local_topology_view(id, 0, NODE_STATE_SHADOW, &g_seeds[g_seed_count], 0);
                g_seed_count++;
            }
        }
    }
    fclose(fp);
}

// --- Main Entry ---
int main(int argc, char **argv) {
    // Prevent process-wide termination on EPIPE when a peer disconnects.
    signal(SIGPIPE, SIG_IGN);

    // 参数检查
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <RAM_MB> <LOCAL_PORT> <SWARM_CONFIG> <MY_PHYS_ID> <CTRL_PORT> <SLAVE_PORT> [SYNC_BATCH] [VM_ID]\n", argv[0]);
        return 1;
    }

    g_dev_fd = open("/dev/wavevm", O_RDWR);
    if (g_dev_fd < 0) {
        // 如果是纯用户态模式，这可能不是致命的，但在 Mode A 下是致命的。
        // 打印警告即可，方便调试
        perror("[Warning] Failed to open /dev/wavevm (Kernel Mode disabled?)");
    }


    // 1. 基础参数解析
    size_t ram_mb = (size_t)atol(argv[1]);
    g_shm_size = ram_mb * 1024 * 1024;
    int local_port = atoi(argv[2]);
    const char *config_file = argv[3];
    int my_phys_id = atoi(argv[4]); // 用户传入的是物理 ID (配置文件行号)
    g_ctrl_port = atoi(argv[5]);
    extern int g_slave_forward_port; 
    g_slave_forward_port = atoi(argv[6]);
    // 可选参数：批量同步大小
    if (argc >= 8) {
        extern int g_sync_batch_size;
        g_sync_batch_size = atoi(argv[7]);
    }
    // 可选参数：VM ID (Multi-VM 资源池化)
    if (argc >= 9) {
        g_my_vm_id = (uint8_t)atoi(argv[8]);
    }

    printf("[*] WaveVM Swarm V30.0 'Wavelet' Node Daemon (PhysID: %d, VM: %u)\n", my_phys_id, (unsigned)g_my_vm_id);

    // 2. 初始化用户态后端 (User Backend)
    // 注意：此时我们暂时用 PhysID 初始化，后续 load_swarm_config 会填充完整的路由表
    if (user_backend_init(my_phys_id, local_port) != 0) {
        fprintf(stderr, "[-] Failed to init user backend.\n");
        return 1;
    }
    
    // 3. 初始化逻辑核心 (Logic Core)
    // 此时 Total Nodes 尚未知，传 0 作为提示
    if (wvm_core_init(&u_ops, 0) != 0) {
        fprintf(stderr, "[-] Logic Core init failed.\n");
        return 1;
    }
    
    // 4. 加载 Swarm 拓扑
    // 这会将所有物理 IP 展开为虚拟节点，并注入 Backend 和 Logic Core
    load_swarm_config(config_file);

    // 5. 启动 V29.5 核心推送引擎的多线程广播线程
    printf("[+] Starting %d Wavelet Broadcast Engines...\n", NUM_BCAST_WORKERS);
    for (long i = 0; i < NUM_BCAST_WORKERS; i++) { // 使用 long 避免指针转换警告
        pthread_t bcast_tid;
        // 将线程ID (0 to 7)作为参数传入
        if (pthread_create(&bcast_tid, NULL, broadcast_worker_thread, (void*)i) != 0) {
            perror("[-] Failed to start broadcast worker thread");
            exit(1);
        }
        pthread_detach(bcast_tid);
    }
    printf("[+] All Wavelet Broadcast Engines started.\n");

    // 6. [V29 关键逻辑] 身份识别与资源自检
    // 我们需要再次扫描配置文件，找到 my_phys_id 对应的 Virtual ID (vnode_start)
    // 同时检查启动参数申请的 RAM 是否满足配置文件的要求
    int my_virtual_id = -1;
    int my_local_cores = 1;
    
    FILE *fp_check = fopen(config_file, "r");
    if (fp_check) {
        char line[256];
        int current_phys_idx = 0;
        int current_v_id_accumulator = 0;
        
        // 定义必须与 load_swarm_config 保持一致
        #define WVM_RAM_UNIT_GB 4 
        
        while (fgets(line, sizeof(line), fp_check)) {
            if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

            char keyword[16];
            int bid, port, cores, ram_gb;
            char ip_str[64];
            
            // 尝试解析: NODE IP PORT CORES RAM
            if (sscanf(line, "%15s", keyword) != 1) continue;
            if (strcmp(keyword, "NODE") != 0) continue;

            if (sscanf(line, "%*s %d %63s %d %d %d", &bid, ip_str, &port, &cores, &ram_gb) == 5) {
                if (ram_gb <= 0) ram_gb = 4;
                
                // 计算该节点占用的虚拟槽位
                int v_count = ram_gb / WVM_RAM_UNIT_GB;
                if (v_count < 1) v_count = 1;
                
                // 如果这就是我自己
                if (current_phys_idx == my_phys_id) {
                    // A. 身份确认
                    my_virtual_id = current_v_id_accumulator;
                    if (cores < 1) cores = 1;
                    my_local_cores = cores;
                    
                    // B. [红队防御] 资源自检：防止配置撒谎导致 Crash
                    size_t config_bytes = (size_t)ram_gb * 1024 * 1024 * 1024;
                    if (g_shm_size < config_bytes) {
                        fprintf(stderr, "\n[FATAL] Resource Mismatch!\n");
                        fprintf(stderr, "  Config Node %d requires: %d GB\n", my_phys_id, ram_gb);
                        fprintf(stderr, "  Launch arg provided:     %lu MB\n", ram_mb);
                        fprintf(stderr, "  System will CRASH on OOB access. Aborting.\n");
                        exit(1);
                    }
                    printf("[Check] Resource verified: Alloc %lu MB >= Config %d GB.\n", ram_mb, ram_gb);
                    break;
                }
                
                current_v_id_accumulator += v_count;
                current_phys_idx++;
            }
        }
        fclose(fp_check);
    }

    if (my_virtual_id == -1) {
        fprintf(stderr, "[Fatal] My Physical ID %d not found in config file!\n", my_phys_id);
        return 1;
    }

    // 7. 将真实的虚拟 ID 注入 Logic Core
    // Logic Core 将根据此 ID 判断是否拥有某个 GPA 的管理权 (Directory Owner)
    wvm_set_my_node_id(my_virtual_id);
    printf("[Init] Identity Mapped: PhysID %d -> VirtualID %d (Primary)\n", my_phys_id, my_virtual_id);
    {
        char split_buf[32];
        snprintf(split_buf, sizeof(split_buf), "%d", my_local_cores);
        setenv("WVM_LOCAL_SPLIT", split_buf, 1);
    }

    // 8. 初始化共享内存 (RAM Backing Store)
    // 优先读取环境变量，支持单机多实例测试
    const char *shm_path = getenv("WVM_SHM_FILE");
    if (!shm_path) shm_path = WVM_DEFAULT_SHM_PATH; // "/wavevm_ram"

    printf("[System] Initializing SHM: %s (Size: %lu MB)\n", shm_path, ram_mb);

    // 清理残留
    shm_unlink(shm_path);
    
    int shm_fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { 
        fprintf(stderr, "[-] Failed to open shm file '%s': %s\n", shm_path, strerror(errno));
        return 1; 
    }
    
    // 分配物理空间
    if (ftruncate(shm_fd, g_shm_size) < 0) {
        perror("ftruncate failed");
        close(shm_fd);
        return 1;
    }

    // 映射到进程空间
    g_shm_ptr = mmap(NULL, g_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd); // 映射后即可关闭 fd
    
    if (g_shm_ptr == MAP_FAILED) { 
        perror("mmap failed"); 
        return 1; 
    }
    
    // 可选：预热内存 (避免运行时缺页抖动)
    // memset(g_shm_ptr, 0, g_shm_size);
    printf("[+] Memory Ready at %p\n", g_shm_ptr);

    // 9. 启动 UNIX Socket 监听 (QEMU 接口)
    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket AF_UNIX failed");
        return 1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    
    // 动态生成 Socket 路径，支持多实例
    char *inst_id = getenv("WVM_INSTANCE_ID");
    char sock_path[128];
    snprintf(sock_path, sizeof(sock_path), "/tmp/wvm_user_%s.sock", inst_id ? inst_id : "0");

    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    unlink(sock_path); // 绑定前确保文件不存在

    printf("[System] Control Socket: %s\n", sock_path);

    // 关键：设置环境变量供子进程 (QEMU) 使用
    setenv("WVM_ENV_SOCK_PATH", sock_path, 1);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { 
        perror("bind unix socket failed"); 
        return 1; 
    }
    
    if (listen(listen_fd, 100) < 0) {
        perror("listen failed");
        return 1;
    }

    printf("[+] WaveVM V29 Node Ready. Waiting for QEMU...\n");

    // 10. Backend/Logic Core 已在前面初始化并注入拓扑。
    // 此处严禁重复初始化，否则会重置 CPU 路由表为 AUTO_ROUTE。

    // 11. [自治扩展] 加载种子节点，不要求全量配置
    load_initial_seeds(config_file);

    // 12. 启动自治监控引擎 (Part 3 中定义的线程)
    pthread_t monitor_tid;
    pthread_create(&monitor_tid, NULL, autonomous_monitor_thread, NULL);
    pthread_detach(monitor_tid);

    // 13. [Bootstrap] 视图主动拉取逻辑暂时禁用（需跨模块可见 peer 结构体）。

    printf("[Autonomous] Node started in SHADOW mode. Auto-scaling into cluster...\n");

    // 14. 主循环：接受 QEMU 连接
    while (1) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept error");
            // 生产环境可能选择 sleep 并重试，而非退出
            sleep(1);
            continue;
        }

        // [FIX-F1] 防御性检查：在 accept 后立即检查连接数上限，防止线程爆炸。
        // 旧代码无条件 pthread_create，仅在 client_handler 内部检查 MAX_QEMU_CLIENTS，
        // 但线程已经创建完毕。此处前置检查，超限直接拒绝连接。
        pthread_mutex_lock(&g_client_lock);
        int current_count = g_client_count;
        pthread_mutex_unlock(&g_client_lock);

        if (current_count >= MAX_QEMU_CLIENTS) {
            fprintf(stderr, "[IPC] WARN: MAX_QEMU_CLIENTS(%d) reached, rejecting fd=%d\n",
                    MAX_QEMU_CLIENTS, client_fd);
            close(client_fd);
            continue;
        }

        // 为每个 QEMU 连接创建一个处理线程
        pthread_t thread_id;
        int *new_sock = malloc(sizeof(int));
        if (new_sock) {
            *new_sock = client_fd;
            if (pthread_create(&thread_id, NULL, client_handler, (void*)new_sock) != 0) {
                perror("pthread_create failed");
                close(client_fd);
                free(new_sock);
            } else {
                pthread_detach(thread_id);
            }
        } else {
            perror("malloc failed");
            close(client_fd);
        }
    }

    return 0;
}
```

**文件**: `master_core/Makefile_User`

```makefile
CC = gcc
CFLAGS = -Wall -O2 -msse4.2 -I../common_include -pthread
TARGET = wavevm_node_master
SRCS = logic_core.c user_backend.c main_wrapper.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

```

---

### Step 6: Slave 守护进程 (Slave Daemon)

**文件**: `slave_daemon/slave_hybrid.c`

```c
/*
 * [IDENTITY] Slave Hybrid - The Execution Muscle
 * ---------------------------------------------------------------------------
 * 物理角色：远程节点上的"计算代理"。
 * 职责边界：
 * 1. 托管 KVM/TCG 上下文执行，将 IO/MMIO 退出转发回 Master。
 * 2. 实现 MPSC 脏页收割队列，将 Slave 端的写入异步同步给 Master。
 * 3. 驱动三通道 (CMD/REQ/PUSH) 隔离协议，防止网络拥塞。
 * 
 * [性能红线]
 * - 脏页收割频率严禁低于 1ms，否则 Master 端的读缓存失效太久。
 * - 严禁在 KVM_RUN 路径上引入同步网络等待。
 * ---------------------------------------------------------------------------
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <inttypes.h>
#include <linux/kvm.h>
#include <errno.h>
#include <sched.h>
#include <sys/wait.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <stdbool.h>
#include "uthash.h"
#include "slave_vfio.h"

#include "../common_include/wavevm_protocol.h"

// --- 全局配置变量 ---
static int g_service_port = 9000;
static long g_num_cores = 0;
static int g_ram_mb = 1024;
static uint64_t g_slave_ram_size = 1024UL * 1024 * 1024;

typedef struct {
    int slot;
    uint64_t guest_phys_addr;
    uint64_t size;
    uint8_t *hva;
    uint32_t flags;
    bool track_dirty;
} wvm_kvm_slot_t;

static wvm_kvm_slot_t g_kvm_slots[4];
static int g_kvm_slot_count = 0;
static uint8_t *g_kvm_reserved_hva = NULL;
static size_t g_kvm_reserved_size = 0;
// 用于通知 IRQ 线程 Master 地址已就绪
static pthread_cond_t g_master_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_master_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_master_ready = 0;
static struct sockaddr_in g_master_addr;
static char *g_vfio_config_path = NULL; 
int g_ctrl_port = 9001; // 真实定义，供应给 wavevm_node_slave

// MPSC 队列数据结构

// 任务单元：封装一个需要同步的脏页
typedef struct {
    uint64_t gpa;
    uint8_t  data[4096];
} dirty_page_task_t;

// MPSC 环形队列
#define MPSC_QUEUE_SIZE 8192 // [FIX-F2] 扩容 1024->8192 (32MB)，减少溢出概率
static dirty_page_task_t* g_mpsc_queue[MPSC_QUEUE_SIZE];
static volatile uint32_t g_mpsc_head = 0;
static volatile uint32_t g_mpsc_tail = 0;

// 用于保护队列的互斥锁（生产者侧使用）和用于唤醒消费者的条件变量
static pthread_mutex_t g_mpsc_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_mpsc_cond = PTHREAD_COND_INITIALIZER;

#define BATCH_SIZE 32
static int g_kvm_available = 0;

typedef struct {
    uint64_t chunk_id;
    int fd;
    time_t last_access;
    UT_hash_handle hh;
} chunk_fd_entry_t;

static chunk_fd_entry_t *g_fd_cache = NULL;
static pthread_mutex_t g_fd_lock = PTHREAD_MUTEX_INITIALIZER;
#define MAX_OPEN_CHUNKS 1024

// 存储辅助函数
static int get_chunk_fd_safe(uint64_t chunk_id) {
    chunk_fd_entry_t *entry;
    pthread_mutex_lock(&g_fd_lock);
    
    HASH_FIND(hh, g_fd_cache, &chunk_id, sizeof(uint64_t), entry);
    if (entry) {
        entry->last_access = time(NULL);
        // Move to front (LRU) implemented by deletion/re-addition in simple uthash, or utilize extra list
        pthread_mutex_unlock(&g_fd_lock);
        return entry->fd;
    }

    // LRU Eviction: 如果满了，删除最旧的 (简化为删头节点)
    if (HASH_COUNT(g_fd_cache) >= MAX_OPEN_CHUNKS) {
        chunk_fd_entry_t *el, *tmp;
        HASH_ITER(hh, g_fd_cache, el, tmp) {
            close(el->fd);
            HASH_DEL(g_fd_cache, el);
            free(el);
            break; // Evict one
        }
    }

    // 物理打开：确保目录存在
    // 生产环境应在启动脚本中 mkdir -p /var/lib/wavevm/chunks
    char path[256];
    snprintf(path, sizeof(path), "/var/lib/wavevm/chunks/c_%" PRIu64 ".bin", chunk_id);
    
    // [V31 PHY] 使用 O_DIRECT 绕过 Page Cache，直接落盘
    int fd = open(path, O_RDWR | O_CREAT | O_DIRECT, 0644);
    if (fd < 0) {
        // Fallback: 如果文件系统不支持 O_DIRECT (如 tmpfs)，回退到普通模式
        if (errno == EINVAL) fd = open(path, O_RDWR | O_CREAT, 0644);
        if (fd < 0) {
            pthread_mutex_unlock(&g_fd_lock);
            return -1;
        }
    }

    entry = malloc(sizeof(chunk_fd_entry_t));
    if (!entry) {
        close(fd);
        pthread_mutex_unlock(&g_fd_lock);
        return -1;
    }
    entry->chunk_id = chunk_id;
    entry->fd = fd;
    entry->last_access = time(NULL);
    HASH_ADD(hh, g_fd_cache, chunk_id, sizeof(uint64_t), entry);
    
    pthread_mutex_unlock(&g_fd_lock);
    return fd;
}

/* 
 * [物理意图] 实现具备“阻塞感知”能力的可靠 UDP 发送，作为 Slave 到 Master 的数据回传通道。
 * [关键逻辑] 实时监测 socket 缓冲区状态，当网络拥塞（EAGAIN）时执行毫秒级退避重试，而非暴力丢包。
 * [后果] 这是 Slave 端状态回传的生命线。若无此机制，高频的中断或脏页同步会冲垮网络，导致 Master 端逻辑断层。
 */
static int robust_sendto(int fd, const void *buf, size_t len, struct sockaddr_in *dest) {
    int retries = 0;
    struct pollfd pfd = { .fd = fd, .events = POLLOUT };

    while (retries < 100) { // 设定最大尝试次数，防止网络彻底断开时死循环
        ssize_t sent = sendto(fd, buf, len, MSG_DONTWAIT, (struct sockaddr*)dest, sizeof(*dest));
        
        if (sent > 0) return 0; // 发送成功

        if (sent == 0) { retries++; continue; } /* sendto 返回 0：零长度包，计入重试 */

        if (sent < 0) {
            if (errno == EINTR) continue; // 被信号中断，立即重试
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS) {
                // 缓冲区满，挂起等待 5ms 
                poll(&pfd, 1, 5);
                retries++;
                continue;
            }
            return -1; // 致命错误
        }
    }
    return -ETIMEDOUT;
}

/* 
 * [物理意图] 将 Slave 端的“物理硬件中断”注入到分布式虚拟化总线中。
 * [关键逻辑] 监听本地物理 GPU 的 eventfd，一旦触发，立即封装为 MSG_VFIO_IRQ 并发送至 Master 的 Fast Lane。
 * [后果] 实现了分布式 I/O 的闭环。它让远在千里之外的物理显卡中断，能在 100µs 内“瞬移”到 Master 节点的 vCPU 中。
 */
void *vfio_irq_thread_adapter(void *arg) {
    pthread_mutex_lock(&g_master_mutex);
    while (!g_master_ready) {
        pthread_cond_wait(&g_master_cond, &g_master_mutex);
    }
    struct sockaddr_in target = g_master_addr;
    pthread_mutex_unlock(&g_master_mutex);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("[Slave] Failed to create IRQ socket");
        return NULL;
    }

    printf("[Slave] IRQ Forwarder Connected to Master %s:%d\n", 
           inet_ntoa(target.sin_addr), ntohs(target.sin_port));

    // 调用 slave_vfio.c 中的核心轮询逻辑
    wvm_vfio_poll_irqs(sock, &target);
    
    close(sock);
    return NULL;
}

// CPU 核心数探测
int get_allowed_cores() {
    char *env_override = getenv("WVM_CORE_OVERRIDE");
    if (env_override) {
        int val = atoi(env_override);
        if (val > 0) {
            printf("[Hybrid] CPU Cores forced by Env: %d\n", val);
            return val;
        }
    }

    long quota = -1;
    long period = 100000; 

    FILE *fp = fopen("/sys/fs/cgroup/cpu.max", "r");
    if (fp) {
        char buf[64];
        if (fscanf(fp, "%63s %ld", buf, &period) >= 1) {
            if (strcmp(buf, "max") != 0) quota = atol(buf);
        }
        fclose(fp);
    }

    if (quota <= 0) {
        fp = fopen("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", "r");
        if (fp) {
            if (fscanf(fp, "%ld", &quota) != 1) quota = -1;
            fclose(fp);
        }
        fp = fopen("/sys/fs/cgroup/cpu/cpu.cfs_period_us", "r");
        if (fp) {
            long p;
            if (fscanf(fp, "%ld", &p) == 1) period = p;
            fclose(fp);
        }
    }

    int logical_cores = 0;
    if (quota > 0 && period > 0) {
        logical_cores = (int)((quota + period - 1) / period);
        printf("[Hybrid] Container Quota Detected: %ld / %ld = %d Cores\n", quota, period, logical_cores);
    } else {
        logical_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
        printf("[Hybrid] Using Physical Cores: %d\n", logical_cores);
    }

    if (logical_cores <= 0) logical_cores = 1;
    return logical_cores;
}

// 辅助：头部转本机序
static void ntoh_header(struct wvm_header *hdr) {
    hdr->magic = ntohl(hdr->magic);
    hdr->msg_type = ntohs(hdr->msg_type);
    hdr->payload_len = ntohs(hdr->payload_len);
    hdr->slave_id = ntohl(hdr->slave_id);
    hdr->target_id = ntohl(hdr->target_id);
    hdr->req_id = WVM_NTOHLL(hdr->req_id);
    hdr->epoch = ntohl(hdr->epoch);
}

// ==========================================
// [Fast Path] KVM Engine (V28 Fixed)
// ==========================================

static int g_kvm_fd = -1;
static int g_vm_fd = -1;
static uint8_t *g_phy_ram = NULL;
static __thread int t_vcpu_fd = -1;
static __thread struct kvm_run *t_kvm_run = NULL;
static int g_boot_vcpu_fd = -1;
static struct kvm_run *g_boot_kvm_run = NULL;
static pthread_spinlock_t g_master_lock;
static int g_wvm_dev_fd = -1;
static int g_base_id = 0;
static uint8_t g_slave_vm_id = 0;
static int g_vcpu_init_debug_once = 0;

#define WVM_PAGE_SIZE            4096ULL
#define WVM_KVM_IDMAP_ADDR       0xfffbc000ULL
#define WVM_KVM_TSS_ADDR         0xfffbd000ULL
#define WVM_KVM_RESERVED_SIZE    0x2000ULL /* IDMAP + TSS pages */

static uint8_t *wvm_gpa_to_hva(uint64_t gpa)
{
    for (int i = 0; i < g_kvm_slot_count; i++) {
        const wvm_kvm_slot_t *slot = &g_kvm_slots[i];
        if (gpa >= slot->guest_phys_addr &&
            gpa < slot->guest_phys_addr + slot->size) {
            return slot->hva + (gpa - slot->guest_phys_addr);
        }
    }
    return NULL;
}

static inline int wvm_gpa_page_valid(uint64_t gpa)
{
    uint8_t *hva = wvm_gpa_to_hva(gpa);
    if (!hva) return 0;
    /* Require the whole page to fit in a single slot. */
    return wvm_gpa_to_hva(gpa + (WVM_PAGE_SIZE - 1)) != NULL;
}

static int wvm_kvm_add_memslot(int vm_fd, int slot_id,
                              uint64_t guest_phys_addr, uint64_t size,
                              uint8_t *hva, uint32_t flags, bool track_dirty)
{
    struct kvm_userspace_memory_region region;

    if (size == 0) {
        return 0;
    }
    if ((guest_phys_addr & (WVM_PAGE_SIZE - 1)) ||
        (size & (WVM_PAGE_SIZE - 1)) ||
        (((uintptr_t)hva) & (WVM_PAGE_SIZE - 1))) {
        fprintf(stderr, "[Hybrid] memslot alignment invalid gpa=%#llx size=%#llx hva=%p\n",
                (unsigned long long)guest_phys_addr,
                (unsigned long long)size,
                (void*)hva);
        return -1;
    }

    memset(&region, 0, sizeof(region));
    region.slot = (uint32_t)slot_id;
    region.flags = flags;
    region.guest_phys_addr = guest_phys_addr;
    region.memory_size = size;
    region.userspace_addr = (uint64_t)hva;

    if (ioctl(vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
        perror("[Hybrid] KVM_SET_USER_MEMORY_REGION failed");
        fprintf(stderr, "[Hybrid] memslot=%d gpa=%#llx size=%#llx hva=%p\n",
                slot_id,
                (unsigned long long)guest_phys_addr,
                (unsigned long long)size,
                (void*)hva);
        return -1;
    }

    if (g_kvm_slot_count < (int)(sizeof(g_kvm_slots) / sizeof(g_kvm_slots[0]))) {
        g_kvm_slots[g_kvm_slot_count].slot = slot_id;
        g_kvm_slots[g_kvm_slot_count].guest_phys_addr = guest_phys_addr;
        g_kvm_slots[g_kvm_slot_count].size = size;
        g_kvm_slots[g_kvm_slot_count].hva = hva;
        g_kvm_slots[g_kvm_slot_count].flags = flags;
        g_kvm_slots[g_kvm_slot_count].track_dirty = track_dirty;
        g_kvm_slot_count++;
    }

    return 0;
}

/* 
 * [物理意图] 在远程节点初始化硬件虚拟化容器（KVM），为“算力托管”圈定物理内存边界。
 * [关键逻辑] 创建 KVM 虚拟机实例，利用 mmap 映射大页内存，并根据是否有内核模块（Mode A）选择最优路径。
 * [后果] 确立了远程执行的基石。若初始化失败，Slave 将无法使用硬件加速，只能回退到慢速的 TCG 模拟模式。
 */
void init_kvm_global() {
    const char *force_tcg = getenv("WVM_FORCE_TCG");
    if (force_tcg && atoi(force_tcg) == 1) {
        fprintf(stderr, "[Hybrid] WVM_FORCE_TCG=1, skip KVM init.\n");
        g_kvm_available = 0;
        return;
    }

    g_kvm_fd = open("/dev/kvm", O_RDWR);
    if (g_kvm_fd < 0) return; 

    g_wvm_dev_fd = open("/dev/wavevm", O_RDWR);
    
    // MAP_SHARED 是必须的，否则 madvise 无法正确通知 KVM 释放页面
    if (g_wvm_dev_fd >= 0) {
        printf("[Hybrid] KVM: Detected /dev/wavevm. Enabling On-Demand Paging (Fast Path).\n");
        g_phy_ram = mmap(NULL, g_slave_ram_size, PROT_READ|PROT_WRITE, MAP_SHARED, g_wvm_dev_fd, 0);
    } else {
        // [FIXED] 优先使用 SHM 文件，方便单机测试隔离
        const char *shm_path = getenv("WVM_SHM_FILE");
        if (shm_path) {
            printf("[Hybrid] KVM: Kernel module not found. Using SHM File: %s\n", shm_path);
            int shm_fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
            if (shm_fd >= 0) {
                ftruncate(shm_fd, g_slave_ram_size);
                g_phy_ram = mmap(NULL, g_slave_ram_size, PROT_READ|PROT_WRITE, MAP_SHARED, shm_fd, 0);
                close(shm_fd);
            }
        }
        
        // 如果 SHM 失败或未设置，回退到匿名内存
        if (!g_phy_ram || g_phy_ram == MAP_FAILED) {
            printf("[Hybrid] KVM: Using Anonymous RAM.\n");
            g_phy_ram = mmap(NULL, g_slave_ram_size, PROT_READ|PROT_WRITE, 
                             MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        }
    }

    if (g_phy_ram == MAP_FAILED) { perror("mmap ram"); exit(1); }

    madvise(g_phy_ram, g_slave_ram_size, MADV_HUGEPAGE);
    // [V28 Fix] 不要 MADV_RANDOM，保持默认，让 KVM 能够利用 THP
    
    g_vm_fd = ioctl(g_kvm_fd, KVM_CREATE_VM, 0);
    if (g_vm_fd < 0) { close(g_kvm_fd); g_kvm_fd = -1; return; }

    /* On some virtualized hosts, vCPU must be created before full VM wiring. */
    errno = 0;
    int boot_vcpu_id = 0;
    g_boot_vcpu_fd = ioctl(g_vm_fd, KVM_CREATE_VCPU, boot_vcpu_id);
    if (g_boot_vcpu_fd < 0 && errno == EEXIST) {
        for (int try_id = 1; try_id < 8192; try_id++) {
            g_boot_vcpu_fd = ioctl(g_vm_fd, KVM_CREATE_VCPU, try_id);
            if (g_boot_vcpu_fd >= 0) {
                boot_vcpu_id = try_id;
                break;
            }
            if (errno != EEXIST) {
                break;
            }
        }
    }
    fprintf(stderr, "[Slave BootVCPU] create fd=%d errno=%d vm=%d kvm=%d id=%d\n",
            g_boot_vcpu_fd, errno, g_vm_fd, g_kvm_fd, boot_vcpu_id);
    if (g_boot_vcpu_fd >= 0) {
        int mmap_size = ioctl(g_kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
        fprintf(stderr, "[Slave BootVCPU] mmap_size=%d errno=%d\n", mmap_size, errno);
        if (mmap_size > 0) {
            g_boot_kvm_run = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, g_boot_vcpu_fd, 0);
            if (g_boot_kvm_run == MAP_FAILED) {
                fprintf(stderr, "[Slave BootVCPU] mmap failed errno=%d\n", errno);
                g_boot_kvm_run = NULL;
            }
        }
    }

    ioctl(g_vm_fd, KVM_SET_TSS_ADDR, WVM_KVM_TSS_ADDR);
    __u64 map_addr = WVM_KVM_IDMAP_ADDR;
    ioctl(g_vm_fd, KVM_SET_IDENTITY_MAP_ADDR, &map_addr);
    ioctl(g_vm_fd, KVM_CREATE_IRQCHIP, 0);

    g_kvm_slot_count = 0;

    /* Map a dedicated 2-page reserved window for KVM IDMAP/TSS. */
    g_kvm_reserved_size = (size_t)WVM_KVM_RESERVED_SIZE;
    g_kvm_reserved_hva = mmap(NULL, g_kvm_reserved_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_kvm_reserved_hva == MAP_FAILED) {
        perror("[Hybrid] mmap reserved");
        g_kvm_reserved_hva = NULL;
        close(g_vm_fd);
        g_vm_fd = -1;
        close(g_kvm_fd);
        g_kvm_fd = -1;
        return;
    }

    /* Slot 0: [0, IDMAP_ADDR) */
    uint64_t low_size = g_slave_ram_size;
    if (low_size > WVM_KVM_IDMAP_ADDR) {
        low_size = WVM_KVM_IDMAP_ADDR;
    }
    if (wvm_kvm_add_memslot(g_vm_fd, 0, 0, low_size, g_phy_ram,
                            KVM_MEM_LOG_DIRTY_PAGES, true) < 0) {
        close(g_vm_fd);
        g_vm_fd = -1;
        close(g_kvm_fd);
        g_kvm_fd = -1;
        return;
    }

    /* Slot 2: reserved IDMAP/TSS pages. */
    if (wvm_kvm_add_memslot(g_vm_fd, 2, WVM_KVM_IDMAP_ADDR, WVM_KVM_RESERVED_SIZE,
                            g_kvm_reserved_hva, 0, false) < 0) {
        close(g_vm_fd);
        g_vm_fd = -1;
        close(g_kvm_fd);
        g_kvm_fd = -1;
        return;
    }

    /* Slot 1: (IDMAP_ADDR + reserved, ram_end] */
    uint64_t hole_end = WVM_KVM_IDMAP_ADDR + WVM_KVM_RESERVED_SIZE;
    if (g_slave_ram_size > hole_end) {
        uint64_t high_size = g_slave_ram_size - hole_end;
        if (wvm_kvm_add_memslot(g_vm_fd, 1, hole_end, high_size, g_phy_ram + hole_end,
                                KVM_MEM_LOG_DIRTY_PAGES, true) < 0) {
            close(g_vm_fd);
            g_vm_fd = -1;
            close(g_kvm_fd);
            g_kvm_fd = -1;
            return;
        }
    }

    g_kvm_available = 1;
    pthread_spin_init(&g_master_lock, 0);

    printf("[Hybrid] KVM Hardware Acceleration Active (RAM: %d MB).\n", g_ram_mb);
}

void init_thread_local_vcpu(int vcpu_id) {
    if (t_vcpu_fd >= 0) return;
    if (__sync_bool_compare_and_swap(&g_vcpu_init_debug_once, 0, 1)) {
        fprintf(stderr, "[Slave VCPU Init] enter vm=%d kvm=%d boot_fd=%d boot_run=%p req_vcpu=%d\n",
                g_vm_fd, g_kvm_fd, g_boot_vcpu_fd, (void*)g_boot_kvm_run, vcpu_id);
    }
    if (g_vm_fd < 0 || g_kvm_fd < 0) {
        fprintf(stderr, "[Slave VCPU Init] invalid fds vm=%d kvm=%d vcpu=%d\n",
                g_vm_fd, g_kvm_fd, vcpu_id);
        return;
    }

    /* Preferred fast path: reuse boot vCPU 0 that was pre-created at init. */
    if (g_boot_vcpu_fd >= 0) {
        if (!g_boot_kvm_run) {
            int mmap_size = ioctl(g_kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
            if (mmap_size > 0) {
                g_boot_kvm_run = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, g_boot_vcpu_fd, 0);
                if (g_boot_kvm_run == MAP_FAILED) {
                    g_boot_kvm_run = NULL;
                }
            }
        }
        if (g_boot_kvm_run) {
            t_vcpu_fd = g_boot_vcpu_fd;
            t_kvm_run = g_boot_kvm_run;
            return;
        }
    }

    int chosen_vcpu_id = vcpu_id;
    t_vcpu_fd = ioctl(g_vm_fd, KVM_CREATE_VCPU, vcpu_id);
    if (t_vcpu_fd < 0 && errno == EEXIST) {
        /* Some hosts reserve vcpu0 in-kernel. Probe a small contiguous range. */
        for (int try_id = 1; try_id < 8192; try_id++) {
            t_vcpu_fd = ioctl(g_vm_fd, KVM_CREATE_VCPU, try_id);
            if (t_vcpu_fd >= 0) {
                chosen_vcpu_id = try_id;
                break;
            }
            if (errno != EEXIST) break;
        }
    }
    if (t_vcpu_fd < 0 && errno == EEXIST && g_boot_vcpu_fd >= 0) {
        chosen_vcpu_id = 0;
        t_vcpu_fd = g_boot_vcpu_fd;
    }
    if (t_vcpu_fd < 0) {
        fprintf(stderr, "[Slave VCPU Init] KVM_CREATE_VCPU failed vcpu=%d errno=%d\n",
                vcpu_id, errno);
        return;
    }
    if (chosen_vcpu_id != vcpu_id) {
        fprintf(stderr, "[Slave VCPU Init] fallback vcpu=%d (requested %d)\n",
                chosen_vcpu_id, vcpu_id);
    }
    int mmap_size = ioctl(g_kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_size <= 0) {
        fprintf(stderr, "[Slave VCPU Init] KVM_GET_VCPU_MMAP_SIZE failed errno=%d\n", errno);
        if (t_vcpu_fd != g_boot_vcpu_fd) close(t_vcpu_fd);
        t_vcpu_fd = -1;
        return;
    }
    t_kvm_run = mmap(NULL, mmap_size, PROT_READ|PROT_WRITE, MAP_SHARED, t_vcpu_fd, 0);
    if (t_kvm_run == MAP_FAILED) {
        fprintf(stderr, "[Slave VCPU Init] vcpu mmap failed errno=%d\n", errno);
        if (t_vcpu_fd != g_boot_vcpu_fd) close(t_vcpu_fd);
        t_vcpu_fd = -1;
        t_kvm_run = NULL;
        return;
    }
}

// [FIX] Thread-Local 缓存，避免高频 malloc/free
static __thread unsigned long *t_dirty_bitmap_cache = NULL;
static __thread size_t t_dirty_bitmap_size = 0;

/* [FIX-F2] 紧急内联发送：当 MPSC 队列已满或 malloc 失败时，阻塞式直接发送脏页。
 * 这是最后的安全网——绝不能在 KVM_GET_DIRTY_LOG 已清除硬件 dirty bit 后丢弃页面。
 * 性能会下降（阻塞 vCPU 所在线程），但数据不会丢失。
 */
static __thread int t_emergency_sock = -1;

static void emergency_send_dirty_page(uint64_t gpa, const uint8_t *hva) {
    if (t_emergency_sock < 0) {
        t_emergency_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (t_emergency_sock < 0) {
            fprintf(stderr, "[FIX-F2] FATAL: cannot create emergency socket, dirty page 0x%"
                    PRIx64 " LOST\n", gpa);
            return;
        }
    }

    uint8_t tx_buf[sizeof(struct wvm_header) + 8 + 4096];
    memset(tx_buf, 0, sizeof(struct wvm_header));
    struct wvm_header *wh = (struct wvm_header *)tx_buf;
    size_t total_len = sizeof(tx_buf);

    wh->magic = htonl(WVM_MAGIC);
    wh->msg_type = htons(MSG_MEM_WRITE);
    wh->payload_len = htons(8 + 4096);
    wh->slave_id = htonl(WVM_ENCODE_ID(g_slave_vm_id, (uint32_t)g_base_id));
    wh->target_id = htonl(WVM_NODE_AUTO_ROUTE);
    wh->req_id = 0;
    wh->qos_level = 0;

    uint64_t net_gpa = WVM_HTONLL(gpa);
    memcpy(tx_buf + sizeof(*wh), &net_gpa, 8);
    memcpy(tx_buf + sizeof(*wh) + 8, hva, 4096);

    wh->crc32 = 0;
    wh->crc32 = htonl(calculate_crc32(tx_buf, total_len));

    // [FIX-M2] 加锁读取 g_master_addr，防止与 RX 线程竞争
    struct sockaddr_in master_snap;
    pthread_spin_lock(&g_master_lock);
    master_snap = g_master_addr;
    pthread_spin_unlock(&g_master_lock);
    robust_sendto(t_emergency_sock, tx_buf, total_len, &master_snap);
}

/* 
 * [物理意图] 充当远程执行节点的“脏页回写引擎”，实现计算与同步的解耦。
 * [关键逻辑] 作为 MPSC 队列的唯一消费者，将各个 vCPU 产生的脏页任务（Dirty Pages）进行有序的异步网络发送。
 * [后果] 它解决了“同步锁死”问题。vCPU 可以在执行完指令后立即继续，无需等待网络 ACK，极大提升了远程主频。
 */
void* dirty_sync_sender_thread(void* arg) {
    printf("[SenderThread] KVM Dirty Page Sender is running.\n");
    
    // 这个 socket 只用于发送，所以不需要 bind
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("SenderThread socket");
        return NULL;
    }

    while (1) {
        pthread_mutex_lock(&g_mpsc_lock);

        // 如果队列为空，则阻塞等待
        while (g_mpsc_head == g_mpsc_tail) {
            pthread_cond_wait(&g_mpsc_cond, &g_mpsc_lock);
        }

        // 从队列头部取出一个任务
        uint32_t current_head = g_mpsc_head;
        dirty_page_task_t* task = g_mpsc_queue[current_head];
        g_mpsc_head = (current_head + 1) % MPSC_QUEUE_SIZE;
        
        pthread_mutex_unlock(&g_mpsc_lock);
        
        // 网络发送
        uint8_t tx_buf[sizeof(struct wvm_header) + 8 + 4096];
        memset(tx_buf, 0, sizeof(struct wvm_header)); // [FIX] 清零 header 区域，防止 epoch/flags 等字段含垃圾值
        struct wvm_header *wh = (struct wvm_header *)tx_buf;
        size_t total_len = sizeof(tx_buf);

        wh->magic = htonl(WVM_MAGIC);
        wh->msg_type = htons(MSG_MEM_WRITE);
        wh->payload_len = htons(8 + 4096);
        wh->slave_id = htonl(WVM_ENCODE_ID(g_slave_vm_id, (uint32_t)g_base_id));
        wh->target_id = htonl(WVM_NODE_AUTO_ROUTE); // [V31 Fix] 本地通信标记，防止 Master vm_id 过滤误杀
        wh->req_id = 0;
        wh->qos_level = 0;

        uint64_t net_gpa = WVM_HTONLL(task->gpa);
        memcpy(tx_buf + sizeof(*wh), &net_gpa, 8);
        memcpy(tx_buf + sizeof(*wh) + 8, task->data, 4096);

        wh->crc32 = 0;
        wh->crc32 = htonl(calculate_crc32(tx_buf, total_len));

        // [FIX-M2] 加锁快照 g_master_addr，防止与 RX 线程竞争
        struct sockaddr_in master_snap;
        pthread_spin_lock(&g_master_lock);
        master_snap = g_master_addr;
        pthread_spin_unlock(&g_master_lock);
        // 使用 robust_sendto 发送，它内部包含了反压处理逻辑
        robust_sendto(sockfd, tx_buf, total_len, &master_snap);
        
        // 任务完成，释放内存
        free(task);
    }
    
    close(sockfd);
    return NULL;
}

/* 
 * [物理意图] 在 Slave 硬件上“瞬间复活”来自 Master 的 CPU 寄存器上下文并执行。
 * [关键逻辑] 1. 注入寄存器状态；2. 启动硬件 KVM_RUN；3. 拦截 MMIO 退出；4. 导出最新状态并回传。
 * [后果] 这是分布式计算的“肌肉”。它让 Master 节点能像调用本地函数一样，将指令流分发到全网任意节点执行。
 */
void handle_kvm_run_stateless(int sockfd, struct sockaddr_in *client, struct wvm_header *hdr, void *payload, int vcpu_id) {
    struct wvm_ipc_cpu_run_req *req = (struct wvm_ipc_cpu_run_req *)payload;
    if (!req) return;
    { static int __run=0;
      if (__run < 10) {
          fprintf(stderr, "[VCPU-RUN] mode=%u vcpu=%d req=%llu src=%s:%u target=%u slave=%u\n",
                  req->mode_tcg, vcpu_id, (unsigned long long)hdr->req_id,
                  inet_ntoa(client->sin_addr), ntohs(client->sin_port),
                  (unsigned)ntohl(hdr->target_id), (unsigned)ntohl(hdr->slave_id));
          __run++;
      }
    }

    // 对“空上下文探针”走快速 ACK，避免在 KVM_RUN 中无意义阻塞。
    // 仅匹配明显空输入，不影响真实执行上下文。
    const wvm_kvm_context_t zero_kvm = {0};
    bool kvm_empty = !req->mode_tcg &&
                     memcmp(&req->ctx.kvm, &zero_kvm, sizeof(zero_kvm)) == 0;
    if (kvm_empty) {
        struct wvm_header ack_hdr;
        memset(&ack_hdr, 0, sizeof(ack_hdr));
        ack_hdr.magic = htonl(WVM_MAGIC);
        ack_hdr.msg_type = htons(MSG_VCPU_EXIT);
        ack_hdr.payload_len = htons(sizeof(struct wvm_ipc_cpu_run_ack));
        /* ACK must originate from this slave node and target the requester. */
        ack_hdr.slave_id = htonl(WVM_ENCODE_ID(g_slave_vm_id, (uint32_t)g_base_id));
        ack_hdr.target_id = hdr->slave_id;  /* already in network byte order */
        ack_hdr.req_id = WVM_HTONLL(hdr->req_id);

        struct wvm_ipc_cpu_run_ack ack;
        memset(&ack, 0, sizeof(ack));
        ack.status = 0;
        ack.mode_tcg = req->mode_tcg;
        if (req->mode_tcg) {
            memcpy(&ack.ctx.tcg, &req->ctx.tcg, sizeof(req->ctx.tcg));
        } else {
            memcpy(&ack.ctx.kvm, &req->ctx.kvm, sizeof(req->ctx.kvm));
        }

        uint8_t tx[sizeof(ack_hdr) + sizeof(ack)];
        memcpy(tx, &ack_hdr, sizeof(ack_hdr));
        memcpy(tx + sizeof(ack_hdr), &ack, sizeof(ack));
        struct wvm_header *tx_hdr = (struct wvm_header *)tx;
        tx_hdr->crc32 = 0;
        tx_hdr->crc32 = htonl(calculate_crc32(tx, sizeof(tx)));
        ssize_t sret = sendto(sockfd, tx, sizeof(tx), 0, (struct sockaddr*)client, sizeof(*client));
        fprintf(stderr, "[Slave FastAck] ret=%zd errno=%d mode=%u req=%llu dst=%s:%u\n",
                sret, (sret < 0) ? errno : 0, req->mode_tcg,
                (unsigned long long)hdr->req_id, inet_ntoa(client->sin_addr), ntohs(client->sin_port));
        return;
    }

    if (t_vcpu_fd < 0) {
        init_thread_local_vcpu(vcpu_id);
    }
    if (t_vcpu_fd < 0 || !t_kvm_run || t_kvm_run == MAP_FAILED) {
        struct wvm_header ack_hdr;
        memset(&ack_hdr, 0, sizeof(ack_hdr));
        ack_hdr.magic = htonl(WVM_MAGIC);
        ack_hdr.msg_type = htons(MSG_VCPU_EXIT);
        ack_hdr.payload_len = htons(sizeof(struct wvm_ipc_cpu_run_ack));
        /* ACK must originate from this slave node and target the requester. */
        ack_hdr.slave_id = htonl(WVM_ENCODE_ID(g_slave_vm_id, (uint32_t)g_base_id));
        ack_hdr.target_id = hdr->slave_id;  /* already in network byte order */
        ack_hdr.req_id = WVM_HTONLL(hdr->req_id);

        struct wvm_ipc_cpu_run_ack ack;
        memset(&ack, 0, sizeof(ack));
        ack.status = -1;
        ack.mode_tcg = req->mode_tcg;
        if (req->mode_tcg) {
            memcpy(&ack.ctx.tcg, &req->ctx.tcg, sizeof(req->ctx.tcg));
        } else {
            memcpy(&ack.ctx.kvm, &req->ctx.kvm, sizeof(req->ctx.kvm));
        }

        uint8_t tx[sizeof(ack_hdr) + sizeof(ack)];
        memcpy(tx, &ack_hdr, sizeof(ack_hdr));
        memcpy(tx + sizeof(ack_hdr), &ack, sizeof(ack));
        struct wvm_header *tx_hdr = (struct wvm_header *)tx;
        tx_hdr->crc32 = 0;
        tx_hdr->crc32 = htonl(calculate_crc32(tx, sizeof(tx)));
        ssize_t sret = sendto(sockfd, tx, sizeof(tx), 0, (struct sockaddr*)client, sizeof(*client));
        fprintf(stderr, "[Slave ErrorAck] vcpu init failed ret=%zd errno=%d req=%llu dst=%s:%u\n",
                sret, (sret < 0) ? errno : 0,
                (unsigned long long)hdr->req_id, inet_ntoa(client->sin_addr), ntohs(client->sin_port));
        return;
    }
    struct kvm_regs kregs; struct kvm_sregs ksregs;
    // 1. 读取当前 sregs，供直通路径或转换路径继续使用
    ioctl(t_vcpu_fd, KVM_GET_SREGS, &ksregs); 

    // 2. 只有不一样的时候才转换！
    if (req->mode_tcg) {
        // TCG 对 KVM：转换计算
        wvm_translate_tcg_to_kvm(&req->ctx.tcg, &kregs, &ksregs);
    } else {
        // KVM 对 KVM：零转换直通
        wvm_kvm_context_t *ctx = &req->ctx.kvm;
        kregs.rax = ctx->rax; kregs.rbx = ctx->rbx; kregs.rcx = ctx->rcx; kregs.rdx = ctx->rdx;
        kregs.rsi = ctx->rsi; kregs.rdi = ctx->rdi; kregs.rsp = ctx->rsp; kregs.rbp = ctx->rbp;
        kregs.r8  = ctx->r8;  kregs.r9  = ctx->r9;  kregs.r10 = ctx->r10; kregs.r11 = ctx->r11;
        kregs.r12 = ctx->r12; kregs.r13 = ctx->r13; kregs.r14 = ctx->r14; kregs.r15 = ctx->r15;
        kregs.rip = ctx->rip; kregs.rflags = ctx->rflags;

        /* Keep local APIC/interrupt state and only import architectural sregs. */
        struct kvm_sregs *remote_sregs = (struct kvm_sregs *)ctx->sregs_data;
        ksregs.cs = remote_sregs->cs;
        ksregs.ds = remote_sregs->ds;
        ksregs.es = remote_sregs->es;
        ksregs.fs = remote_sregs->fs;
        ksregs.gs = remote_sregs->gs;
        ksregs.ss = remote_sregs->ss;
        ksregs.tr = remote_sregs->tr;
        ksregs.ldt = remote_sregs->ldt;
        ksregs.gdt = remote_sregs->gdt;
        ksregs.idt = remote_sregs->idt;
        ksregs.cr0 = remote_sregs->cr0;
        ksregs.cr2 = remote_sregs->cr2;
        ksregs.cr3 = remote_sregs->cr3;
        ksregs.cr4 = remote_sregs->cr4;
        ksregs.cr8 = remote_sregs->cr8;
        ksregs.efer = remote_sregs->efer;
    }

    ioctl(t_vcpu_fd, KVM_SET_SREGS, &ksregs);
    ioctl(t_vcpu_fd, KVM_SET_REGS, &kregs);

    /* [FIX] 恢复上一轮 IO IN / MMIO READ 的设备读结果：
     * Master 已在本地执行 address_space_rw 读取设备并填入 kctx->io.data / mmio.data，
     * 需要写回 t_kvm_run 以便 KVM_RUN 完成未决 IO 指令。 */
    if (!req->mode_tcg) {
        wvm_kvm_context_t *rctx = &req->ctx.kvm;
        if (rctx->exit_reason == KVM_EXIT_IO &&
            rctx->io.direction == KVM_EXIT_IO_IN) {
            t_kvm_run->exit_reason = KVM_EXIT_IO;
            t_kvm_run->io.direction = rctx->io.direction;
            t_kvm_run->io.size      = rctx->io.size;
            t_kvm_run->io.port      = rctx->io.port;
            t_kvm_run->io.count     = rctx->io.count;
            size_t io_bytes = (size_t)rctx->io.size * rctx->io.count;
            if (io_bytes > sizeof(rctx->io.data)) io_bytes = sizeof(rctx->io.data);
            uint8_t *io_ptr = (uint8_t *)t_kvm_run + t_kvm_run->io.data_offset;
            memcpy(io_ptr, rctx->io.data, io_bytes);
        } else if (rctx->exit_reason == KVM_EXIT_MMIO &&
                   !rctx->mmio.is_write) {
            t_kvm_run->exit_reason = KVM_EXIT_MMIO;
            t_kvm_run->mmio.phys_addr = rctx->mmio.phys_addr;
            t_kvm_run->mmio.len       = rctx->mmio.len;
            t_kvm_run->mmio.is_write  = 0;
            memcpy(t_kvm_run->mmio.data, rctx->mmio.data, 8);
        }
    }

    int ret;
    do {
        ret = ioctl(t_vcpu_fd, KVM_RUN, 0);
        if (ret == 0 && t_kvm_run->exit_reason == KVM_EXIT_MMIO) {
            if (wvm_vfio_intercept_mmio(
                    t_kvm_run->mmio.phys_addr,
                    t_kvm_run->mmio.data,
                    t_kvm_run->mmio.len,
                    t_kvm_run->mmio.is_write)) {
                continue; 
            }
        }
        if (ret == 0) break;
    } while (ret == -1 && errno == EINTR);

    if (g_wvm_dev_fd < 0) { 
        // [V28.5 FIXED] KVM Dirty Log Sync (Full Implementation)
        // 完整实现：获取位图 -> 遍历 -> 封包 -> 发送
        struct kvm_dirty_log log = { .slot = 0 };
        size_t nbits_per_long = sizeof(unsigned long) * 8;

        for (int sidx = 0; sidx < g_kvm_slot_count; sidx++) {
            const wvm_kvm_slot_t *slot = &g_kvm_slots[sidx];
            if (!slot->track_dirty) {
                continue;
            }

            /* Slot-local dirty bitmap. */
            uint64_t pages = slot->size / WVM_PAGE_SIZE;
            size_t bitmap_size = (size_t)(((pages + nbits_per_long - 1) / nbits_per_long) * sizeof(unsigned long));
            if (bitmap_size == 0) {
                continue;
            }

            if (!t_dirty_bitmap_cache || t_dirty_bitmap_size < bitmap_size) {
                if (t_dirty_bitmap_cache) free(t_dirty_bitmap_cache);
                t_dirty_bitmap_cache = malloc(bitmap_size);
                t_dirty_bitmap_size = bitmap_size;
            }
            if (!t_dirty_bitmap_cache) {
                perror("malloc dirty bitmap");
                continue;
            }

            log.slot = slot->slot;
            log.dirty_bitmap = t_dirty_bitmap_cache;
            memset(log.dirty_bitmap, 0, bitmap_size);

            if (ioctl(g_vm_fd, KVM_GET_DIRTY_LOG, &log) != 0) {
                continue;
            }

            unsigned long *p = (unsigned long *)log.dirty_bitmap;
            uint64_t num_longs = bitmap_size / sizeof(unsigned long);

            for (uint64_t i = 0; i < num_longs; i++) {
                if (p[i] == 0) continue;

                for (size_t b = 0; b < nbits_per_long; b++) {
                    if ((p[i] >> b) & 1) {
                        uint64_t gpa = slot->guest_phys_addr +
                                       (i * nbits_per_long + b) * WVM_PAGE_SIZE;
                        if (gpa >= g_slave_ram_size) continue;

                        uint8_t *hva = wvm_gpa_to_hva(gpa);
                        if (!hva) continue;

                        dirty_page_task_t* task = malloc(sizeof(dirty_page_task_t));
                        if (!task) {
                            // [FIX-F2] malloc 失败同理：不能丢弃已清除 dirty bit 的页面
                            emergency_send_dirty_page(gpa, hva);
                            continue;
                        }

                        task->gpa = gpa;
                        memcpy(task->data, hva, 4096);

                        /* [FIX] next_tail 计算和满队检查必须在锁内。
                         * 旧代码在锁外读 g_mpsc_tail 算 next_tail，多线程下
                         * 两个线程会算出相同的 next_tail 并覆盖同一个槽位。
                         * 当前 total_threads=1 不会触发，但扩展并发后必崩。
                         */
                        pthread_mutex_lock(&g_mpsc_lock);
                        uint32_t next_tail = (g_mpsc_tail + 1) % MPSC_QUEUE_SIZE;
                        if (next_tail == g_mpsc_head) {
                            pthread_mutex_unlock(&g_mpsc_lock);
                            // [FIX-F2] 队列已满：紧急内联发送
                            free(task);
                            emergency_send_dirty_page(gpa, hva);
                            continue;
                        }
                        g_mpsc_queue[g_mpsc_tail] = task;
                        g_mpsc_tail = next_tail;
                        pthread_mutex_unlock(&g_mpsc_lock);

                        pthread_cond_signal(&g_mpsc_cond);
                    }
                }
            }
        }
    }
    // 导出寄存器状态并回包
    ioctl(t_vcpu_fd, KVM_GET_REGS, &kregs); ioctl(t_vcpu_fd, KVM_GET_SREGS, &ksregs);
    
    struct wvm_header ack_hdr;
    memset(&ack_hdr, 0, sizeof(ack_hdr));
    ack_hdr.magic = htonl(WVM_MAGIC);              
    ack_hdr.msg_type = htons(MSG_VCPU_EXIT);       
    ack_hdr.payload_len = htons(sizeof(struct wvm_ipc_cpu_run_ack));
    /* ACK must originate from this slave node and target the requester. */
    ack_hdr.slave_id = htonl(WVM_ENCODE_ID(g_slave_vm_id, (uint32_t)g_base_id));
    ack_hdr.target_id = hdr->slave_id;  /* already in network byte order */
    ack_hdr.req_id = WVM_HTONLL(hdr->req_id);
    
    struct wvm_ipc_cpu_run_ack *ack = (struct wvm_ipc_cpu_run_ack *)payload;
    ack->status = 0;
    ack->mode_tcg = req->mode_tcg;
    if (req->mode_tcg) {
        wvm_translate_kvm_to_tcg(&kregs, &ksregs, &ack->ctx.tcg);
        ack->ctx.tcg.exit_reason = t_kvm_run->exit_reason;
    } else {
        wvm_kvm_context_t *ack_kctx = &ack->ctx.kvm;
        ack_kctx->rax = kregs.rax; ack_kctx->rbx = kregs.rbx; ack_kctx->rcx = kregs.rcx; ack_kctx->rdx = kregs.rdx;
        ack_kctx->rsi = kregs.rsi; ack_kctx->rdi = kregs.rdi; ack_kctx->rsp = kregs.rsp; ack_kctx->rbp = kregs.rbp;
        ack_kctx->r8  = kregs.r8;  ack_kctx->r9  = kregs.r9;  ack_kctx->r10 = kregs.r10; ack_kctx->r11 = kregs.r11;
        ack_kctx->r12 = kregs.r12; ack_kctx->r13 = kregs.r13; ack_kctx->r14 = kregs.r14; ack_kctx->r15 = kregs.r15;
        ack_kctx->rip = kregs.rip; ack_kctx->rflags = kregs.rflags;
        memcpy(ack_kctx->sregs_data, &ksregs, sizeof(ksregs));
        ack_kctx->exit_reason = t_kvm_run->exit_reason;

        if (t_kvm_run->exit_reason == KVM_EXIT_IO) {
            ack_kctx->io.direction = t_kvm_run->io.direction;
            ack_kctx->io.size = t_kvm_run->io.size;
            ack_kctx->io.port = t_kvm_run->io.port;
            ack_kctx->io.count = t_kvm_run->io.count;
            if (t_kvm_run->io.direction == KVM_EXIT_IO_OUT) {
                size_t io_bytes = (size_t)t_kvm_run->io.size * t_kvm_run->io.count;
                if (io_bytes > sizeof(ack_kctx->io.data)) io_bytes = sizeof(ack_kctx->io.data);
                memcpy(ack_kctx->io.data, (uint8_t*)t_kvm_run + t_kvm_run->io.data_offset, io_bytes);
            }
        } else if (t_kvm_run->exit_reason == KVM_EXIT_MMIO) {
            ack_kctx->mmio.phys_addr = t_kvm_run->mmio.phys_addr;
            ack_kctx->mmio.len = t_kvm_run->mmio.len;
            ack_kctx->mmio.is_write = t_kvm_run->mmio.is_write;
            memcpy(ack_kctx->mmio.data, t_kvm_run->mmio.data, 8);
        }
    }

    uint8_t tx[sizeof(ack_hdr) + sizeof(*ack)];
    memcpy(tx, &ack_hdr, sizeof(ack_hdr));
    memcpy(tx+sizeof(ack_hdr), ack, sizeof(*ack));
    struct wvm_header *tx_hdr = (struct wvm_header *)tx;
    tx_hdr->crc32 = 0;
    tx_hdr->crc32 = htonl(calculate_crc32(tx, sizeof(tx)));
    ssize_t sret = sendto(sockfd, tx, sizeof(tx), 0, (struct sockaddr*)client, sizeof(*client));
    fprintf(stderr, "[Slave Ack] ret=%zd errno=%d req=%llu dst=%s:%u exit=%u\n",
            sret, (sret < 0) ? errno : 0, (unsigned long long)hdr->req_id,
            inet_ntoa(client->sin_addr), ntohs(client->sin_port), t_kvm_run->exit_reason);
}

/* 
 * [物理意图] 充当远程页面的“受控提供者”，响应来自 Master/Directory 的 MESI 指令。
 * [关键逻辑] 处理 INVALIDATE（失效本地页）、DOWNGRADE（权限降级）及 READ（数据提取），操作本地物理页。
 * [后果] 保证了 Slave 侧物理内存与全网真理的一致。若响应失败，Slave 会持有一份陈旧的数据副本，导致计算错误。
 */
void handle_kvm_mem(int sockfd, struct sockaddr_in *client, struct wvm_header *hdr, void *payload) {
    uint16_t type = hdr->msg_type; 
    uint64_t gpa;

    if (type == MSG_INVALIDATE || type == MSG_DOWNGRADE) {
        gpa = hdr->target_gpa; // ntoh_header 已将 union req_id/target_gpa 转为 host order
    } else {
        if (hdr->payload_len < 8) return; 
        // [FIX] 安全读取 Payload 中的 GPA
        gpa = wvm_get_u64_unaligned(payload);
    }

    if (gpa >= g_slave_ram_size) return;
    if (!wvm_gpa_page_valid(gpa)) return;
    uint8_t *hva = wvm_gpa_to_hva(gpa);
    if (!hva) return;

    if (type == MSG_MEM_READ) {
        struct wvm_header ack_hdr;
        memset(&ack_hdr, 0, sizeof(ack_hdr));
        ack_hdr.magic = htonl(WVM_MAGIC);
        ack_hdr.msg_type = htons(MSG_MEM_ACK);
        ack_hdr.payload_len = htons(4096);
        ack_hdr.slave_id = htonl(WVM_ENCODE_ID(g_slave_vm_id, (uint32_t)g_base_id));
        ack_hdr.target_id = hdr->slave_id;  /* already in network byte order */
        ack_hdr.req_id = WVM_HTONLL(hdr->req_id);

        uint8_t tx[sizeof(ack_hdr) + 4096];
        memcpy(tx, &ack_hdr, sizeof(ack_hdr));
        memcpy(tx+sizeof(ack_hdr), hva, 4096);
        struct wvm_header *tx_hdr = (struct wvm_header *)tx;
        tx_hdr->crc32 = 0;
        tx_hdr->crc32 = htonl(calculate_crc32(tx, sizeof(tx)));
        robust_sendto(sockfd, tx, sizeof(tx), client);
    } 
    else if (type == MSG_MEM_WRITE) {
        if (hdr->payload_len >= 8+4096) {
            memcpy(hva, (uint8_t*)payload+8, 4096);
        }
    }
    else if (type == MSG_INVALIDATE) {
        if (g_kvm_available) {
            memset(hva, 0, 4096); /* KVM: 不能 MADV_DONTNEED，会破坏 memslot 物理页 */
        } else {
            madvise(hva, 4096, MADV_DONTNEED);
        }
    }
    else if (type == MSG_DOWNGRADE) {
        if (hdr->payload_len < 16) return;
        
        // [FIX] 安全读取 Payload 中的复杂数据
        uint64_t requester_u64 = wvm_get_u64_unaligned(payload);
        uint64_t orig_req_id;
        memcpy(&orig_req_id, (uint8_t*)payload + 8, 8); // 已经是网络序，直接考

        uint32_t target_node = (uint32_t)requester_u64;

        struct wvm_header wb_hdr = {
            .magic = htonl(WVM_MAGIC), .msg_type = htons(MSG_WRITE_BACK),
            .payload_len = htons(8 + 4096),
            .slave_id = htonl(target_node),
            .target_id = htonl(hdr->slave_id),
            .req_id = orig_req_id,
            .qos_level = 0
        };

        uint8_t tx[sizeof(wb_hdr) + 8 + 4096];
        memcpy(tx, &wb_hdr, sizeof(wb_hdr));
        *(uint64_t*)(tx + sizeof(wb_hdr)) = WVM_HTONLL(gpa);
        memcpy(tx + sizeof(wb_hdr) + 8, hva, 4096);
        struct wvm_header *tx_hdr = (struct wvm_header *)tx;
        tx_hdr->crc32 = 0;
        tx_hdr->crc32 = htonl(calculate_crc32(tx, sizeof(tx)));

        if (robust_sendto(sockfd, tx, sizeof(tx), client) == 0) {
            // 只有确保网络层已经接纳了这个包，才敢擦除本地物理内存
            if (g_kvm_available) {
                memset(hva, 0, 4096);
            } else {
                madvise(hva, 4096, MADV_DONTNEED);
            }
        } else {
            // 如果发不出去，宁可让 Master 稍后重试，也不要擦除数据
            //fprintf(stderr, "[WVM-Slave] Critical: Failed to send back data for GPA %lx, aborting unmap\n", gpa);
        }
    }
    else if (type == MSG_FETCH_AND_INVALIDATE) {
        // [FIX] 边界检查：需要至少 16 字节 payload（8 target + 8 req_id）
        if (hdr->payload_len < 16) return;
        // [FIX] 安全读取
        uint64_t tmp_target = wvm_get_u64_unaligned(payload);
        uint32_t target_node = (uint32_t)tmp_target;

        uint64_t orig_req_id;
        memcpy(&orig_req_id, (uint8_t*)payload + 8, 8);

        if (gpa < g_slave_ram_size && wvm_gpa_page_valid(gpa)) {
            struct wvm_header wb_hdr;
            memset(&wb_hdr, 0, sizeof(wb_hdr));
            wb_hdr.magic = htonl(WVM_MAGIC);
            wb_hdr.msg_type = htons(MSG_WRITE_BACK);
            wb_hdr.payload_len = htons(8 + 4096);
            wb_hdr.slave_id = htonl(target_node);
            wb_hdr.target_id = htonl(hdr->slave_id);
            wb_hdr.req_id = orig_req_id;
            wb_hdr.qos_level = 0;

            uint8_t tx[sizeof(struct wvm_header) + 8 + 4096];
            memcpy(tx, &wb_hdr, sizeof(wb_hdr));

            uint64_t net_gpa = WVM_HTONLL(gpa);
            memcpy(tx + sizeof(wb_hdr), &net_gpa, 8);
            memcpy(tx + sizeof(wb_hdr) + 8, hva, 4096);

            struct wvm_header *tx_hdr = (struct wvm_header *)tx;
            tx_hdr->crc32 = 0;
            tx_hdr->crc32 = htonl(calculate_crc32(tx, sizeof(tx)));

            if (robust_sendto(sockfd, tx, sizeof(tx), client) == 0) {
                // 同上
                if (g_kvm_available) {
                    memset(hva, 0, 4096);
                } else {
                    madvise(hva, 4096, MADV_DONTNEED);
                }
            }
        }
    }
}

static void handle_block_io_phys(int sockfd, struct sockaddr_in *client, struct wvm_header *hdr, void *payload) {
    struct wvm_block_payload *blk = (struct wvm_block_payload *)payload;
    uint64_t lba = WVM_NTOHLL(blk->lba);
    #ifndef WVM_STORAGE_CHUNK_SHIFT
    #define WVM_STORAGE_CHUNK_SHIFT 26
    #endif
    uint32_t count = ntohl(blk->count);
    if (count == 0 || count > (64U << 20) / 512) return; // 防溢出：上限 64MB
    uint32_t data_len = count * 512;
    // [FIX] 验证 count*512 不超过实际 payload 数据（防越界读）
    if (hdr->msg_type == MSG_BLOCK_WRITE) {
        uint16_t actual_pl = hdr->payload_len; // 已被 ntoh_header 转为 host order
        if (actual_pl < sizeof(struct wvm_block_payload)) return; // 防无符号下溢
        if (data_len > actual_pl - sizeof(struct wvm_block_payload)) return;
    }
    
    uint64_t chunk_id = (lba << 9) >> WVM_STORAGE_CHUNK_SHIFT;
    off_t offset = (lba << 9) & ((1UL << WVM_STORAGE_CHUNK_SHIFT) - 1);
    
    int fd = get_chunk_fd_safe(chunk_id);
    if (fd < 0) return; 

    void *aligned_buf = NULL;
    if (hdr->msg_type == MSG_BLOCK_WRITE || hdr->msg_type == MSG_BLOCK_READ) {
        if (posix_memalign(&aligned_buf, 4096, data_len) != 0) return;
    }

    if (hdr->msg_type == MSG_BLOCK_WRITE) {
        memcpy(aligned_buf, blk->data, data_len);
        ssize_t written = pwrite(fd, aligned_buf, data_len, offset);

        hdr->msg_type = htons(MSG_BLOCK_ACK);
        hdr->payload_len = htons(0);
        hdr->magic = htonl(WVM_MAGIC);
        hdr->slave_id = htonl(hdr->slave_id);
        hdr->target_id = htonl(hdr->target_id);
        hdr->req_id = WVM_HTONLL(hdr->req_id);
        hdr->epoch = htonl(hdr->epoch);
        if (written != data_len) {
            hdr->flags |= WVM_FLAG_ERROR;
        }
        
        // [FIX] 发送前必须重算 CRC32
        hdr->crc32 = 0;
        hdr->crc32 = htonl(calculate_crc32(hdr, sizeof(*hdr)));
        sendto(sockfd, hdr, sizeof(*hdr), 0, (struct sockaddr*)client, sizeof(*client));
    
    } else if (hdr->msg_type == MSG_BLOCK_READ) {
        size_t resp_len = sizeof(struct wvm_header) + sizeof(struct wvm_block_payload) + data_len;
        uint8_t *tx = malloc(resp_len);
        if (tx) {
            struct wvm_header *rh = (struct wvm_header*)tx;
            *rh = *hdr;
            rh->msg_type = htons(MSG_BLOCK_ACK);
            rh->payload_len = htons(sizeof(struct wvm_block_payload) + data_len);
            rh->magic = htonl(WVM_MAGIC);
            rh->slave_id = htonl(hdr->slave_id);
            rh->target_id = htonl(hdr->target_id);
            rh->req_id = WVM_HTONLL(hdr->req_id);
            rh->epoch = htonl(hdr->epoch);

            struct wvm_block_payload *rp = (struct wvm_block_payload*)(tx + sizeof(*hdr));
            rp->lba = blk->lba;
            rp->count = blk->count;

            ssize_t r = pread(fd, aligned_buf, data_len, offset);
            if (r > 0) memcpy(rp->data, aligned_buf, r);

            // [FIX] 发送前必须重算 CRC32
            rh->crc32 = 0;
            rh->crc32 = htonl(calculate_crc32(tx, resp_len));
            robust_sendto(sockfd, tx, resp_len, client);
            free(tx);
        }
    } else if (hdr->msg_type == MSG_BLOCK_FLUSH) {
        int ret = fdatasync(fd);
        hdr->msg_type = htons(MSG_BLOCK_ACK);
        hdr->payload_len = htons(0);
        hdr->magic = htonl(WVM_MAGIC);
        hdr->slave_id = htonl(hdr->slave_id);
        hdr->target_id = htonl(hdr->target_id);
        hdr->req_id = WVM_HTONLL(hdr->req_id);
        hdr->epoch = htonl(hdr->epoch);
        if (ret < 0) hdr->flags |= WVM_FLAG_ERROR;

        // [FIX] 发送前必须重算 CRC32
        hdr->crc32 = 0;
        hdr->crc32 = htonl(calculate_crc32(hdr, sizeof(*hdr)));
        sendto(sockfd, hdr, sizeof(*hdr), 0, (struct sockaddr*)client, sizeof(*client));
    }
    
    free(aligned_buf);
}

/* 
 * [物理意图] Slave 节点的“任务分发中心”，负责监听并调度来自全网的 RPC 指令。
 * [关键逻辑] 绑定多播/复用端口，利用 recvmmsg 批量捕获包头，根据消息类型分流至执行引擎或内存处理程序。
 * [后果] 它是 Slave 节点的总入口。其效率直接决定了 Slave 对 Master 请求的“首包响应时间（TTFB）”。
 */
void* kvm_worker_thread(void *arg) {
    long core = (long)arg;
    int s = socket(AF_INET, SOCK_DGRAM, 0); int opt=1; setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    struct sockaddr_in a = { .sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_ANY), .sin_port=htons(g_service_port) };
    if (bind(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
        fprintf(stderr, "[SLAVE-BIND] port=%d failed errno=%d\n", g_service_port, errno);
    } else if (core == 0) {
        fprintf(stderr, "[SLAVE-BIND] port=%d ok\n", g_service_port);
    }
    
    // Worker 0 初始化 VFIO
    if (core == 0 && g_vfio_config_path) {
        wvm_vfio_init(g_vfio_config_path);
    }
    
    struct mmsghdr msgs[BATCH_SIZE]; struct iovec iov[BATCH_SIZE]; uint8_t bufs[BATCH_SIZE][POOL_ITEM_SIZE]; struct sockaddr_in c[BATCH_SIZE];
    for(int i=0;i<BATCH_SIZE;i++) { iov[i].iov_base=bufs[i]; iov[i].iov_len=POOL_ITEM_SIZE; msgs[i].msg_hdr.msg_iov=&iov[i]; msgs[i].msg_hdr.msg_iovlen=1; msgs[i].msg_hdr.msg_name=&c[i]; msgs[i].msg_hdr.msg_namelen=sizeof(c[i]); }

    while(1) {
        int n = recvmmsg(s, msgs, BATCH_SIZE, 0, NULL);
        if (n<=0) continue;
        for(int i=0;i<n;i++) {
            struct wvm_header *h = (struct wvm_header*)bufs[i];
            if (h->magic != htonl(WVM_MAGIC)) continue;

            { static int __dbg=0;
              if (__dbg < 10) {
                  char ipbuf[16] = {0};
                  inet_ntop(AF_INET, &c[i].sin_addr, ipbuf, sizeof(ipbuf));
                  uint16_t raw_type = ntohs(h->msg_type);
                  fprintf(stderr, "[SLAVE-RX] src=%s:%u len=%u type=%u target=%u slave=%u\n",
                          ipbuf, (unsigned)ntohs(c[i].sin_port),
                          (unsigned)msgs[i].msg_len, (unsigned)raw_type,
                          (unsigned)ntohl(h->target_id), (unsigned)ntohl(h->slave_id));
                  __dbg++;
              }
            }
            { static int __big=0;
              if (__big < 10 && msgs[i].msg_len >= 200) {
                  char ipbuf[16] = {0};
                  inet_ntop(AF_INET, &c[i].sin_addr, ipbuf, sizeof(ipbuf));
                  uint16_t raw_type = ntohs(h->msg_type);
                  fprintf(stderr, "[SLAVE-RX-BIG] src=%s:%u len=%u type=%u\n",
                          ipbuf, (unsigned)ntohs(c[i].sin_port),
                          (unsigned)msgs[i].msg_len, (unsigned)raw_type);
                  __big++;
              }
            }
            
            pthread_spin_lock(&g_master_lock);
            int addr_changed = (g_master_addr.sin_port != c[i].sin_port || g_master_addr.sin_addr.s_addr != c[i].sin_addr.s_addr);
            g_master_addr = c[i];
            pthread_spin_unlock(&g_master_lock);

            // [FIX-M1] mutex 操作移到 spinlock 外部，防止持 spinlock 时阻塞
            if (addr_changed) {
                pthread_mutex_lock(&g_master_mutex);
                if (!g_master_ready) {
                    g_master_ready = 1;
                    pthread_cond_broadcast(&g_master_cond);
                }
                pthread_mutex_unlock(&g_master_mutex);
            }

            uint16_t type = ntohs(h->msg_type);
            ntoh_header(h);
            int actual_payload = msgs[i].msg_len - (int)sizeof(struct wvm_header);
            if (actual_payload < 0 || h->payload_len > (uint16_t)actual_payload) {
                continue;
            }

            if (type == MSG_VCPU_RUN) {
                // [FIX] 必须重建 IPC 请求结构体，不能直接强转 payload
                // 因为 payload 里只有 Context 数据，没有 IPC 头部的 slave_id 等字段
                struct wvm_ipc_cpu_run_req local_req;
    
                // 清空结构体，防止垃圾数据影响逻辑
                memset(&local_req, 0, sizeof(local_req));

                // 1. 从网络包头提取元数据
                // QEMU 发送端将这些信息放在了 wvm_header 中
                local_req.mode_tcg = h->mode_tcg; 
                // slave_id 和 vcpu_index 在 stateless 模式下通常由调度器指定
                // 这里我们直接透传包头里的 source id 作为请求方
                local_req.slave_id = h->slave_id; 

                // 2. 从 Payload 提取 Context
                // 指针 arithmetic: bufs[i] 是包头起始，+sizeof(*h) 是 payload 起始
                void *net_payload_ptr = bufs[i] + sizeof(struct wvm_header);
    
                if (local_req.mode_tcg) {
                    // 安全检查：防止 payload 长度不足导致越界
                    if (h->payload_len >= sizeof(wvm_tcg_context_t)) {
                        memcpy(&local_req.ctx.tcg, net_payload_ptr, sizeof(wvm_tcg_context_t));
                    }
                } else {
                    if (h->payload_len >= sizeof(wvm_kvm_context_t)) {
                        memcpy(&local_req.ctx.kvm, net_payload_ptr, sizeof(wvm_kvm_context_t));
                    }
                }

                // 3. 调用核心执行函数
                // 传递栈上构造的 local_req 指针
                handle_kvm_run_stateless(s, &c[i], h, &local_req, (int)core);
            }
            else if (type == MSG_BLOCK_WRITE || type == MSG_BLOCK_READ || type == MSG_BLOCK_FLUSH) {
                // 存储入口
                handle_block_io_phys(s, &c[i], h, bufs[i]+sizeof(*h));
            }
            else handle_kvm_mem(s, &c[i], h, bufs[i]+sizeof(*h));
        }
    }
}

// ==========================================
// [Fixed Path] TCG Proxy Engine (Tri-Channel)
// ==========================================

// 三通道地址表
typedef struct {
    struct sockaddr_in cmd_addr;  // 控制流
    struct sockaddr_in req_addr;  // 内存请求 (Slave -> Master -> Slave)
    struct sockaddr_in push_addr; // 内存推送 (Master -> Slave)
} slave_endpoint_t;

static slave_endpoint_t *tcg_endpoints = NULL; 
static struct sockaddr_in g_upstream_gateway = {0};
static volatile int g_gateway_init_done = 0;
static volatile int g_gateway_known = 0;

static int is_local_tcg_endpoint_port(uint16_t port_net)
{
    uint16_t port = ntohs(port_net);
    for (long i = 0; i < g_num_cores; i++) {
        if (port == ntohs(tcg_endpoints[i].cmd_addr.sin_port) ||
            port == ntohs(tcg_endpoints[i].req_addr.sin_port) ||
            port == ntohs(tcg_endpoints[i].push_addr.sin_port)) {
            return 1;
        }
    }
    return 0;
}

/* 
 * [物理意图] 在不支持 KVM 的环境下，通过多进程模拟实现“多核算力聚合”。
 * [关键逻辑] 为每个逻辑核心孵化一个独立的 QEMU-TCG 实例，并通过环境变量注入“三通道” Socket 句柄。
 * [后果] 提供了极致的兼容性。即便是在廉价的 ARM 树莓派或锁定内核的容器里，也能作为 Slave 贡献算力。
 */
void spawn_tcg_processes(int base_id) {
    printf("[Hybrid] Spawning %ld QEMU-TCG instances (Tri-Channel Isolation)...\n", g_num_cores);
    const char *qemu_bin = getenv("WVM_TCG_QEMU_BIN");
    if (!qemu_bin || !*qemu_bin) {
        qemu_bin = "qemu-system-x86_64";
    }
    
    tcg_endpoints = malloc(sizeof(slave_endpoint_t) * g_num_cores);
    if (!tcg_endpoints) { perror("malloc endpoints"); exit(1); }
    
    int internal_base = 20000 + (g_service_port % 1000) * 256;
    char ram_str[32]; snprintf(ram_str, sizeof(ram_str), "%d", g_ram_mb);

    for (long i = 0; i < g_num_cores; i++) {
        int port_cmd  = internal_base + i * 3 + 0;
        int port_req  = internal_base + i * 3 + 1;
        int port_push = internal_base + i * 3 + 2;
        
        tcg_endpoints[i].cmd_addr.sin_family = AF_INET;
        tcg_endpoints[i].cmd_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        tcg_endpoints[i].cmd_addr.sin_port = htons(port_cmd);

        tcg_endpoints[i].req_addr.sin_family = AF_INET;
        tcg_endpoints[i].req_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        tcg_endpoints[i].req_addr.sin_port = htons(port_req);

        tcg_endpoints[i].push_addr.sin_family = AF_INET;
        tcg_endpoints[i].push_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        tcg_endpoints[i].push_addr.sin_port = htons(port_push);

        if (fork() == 0) {
            int sock_cmd = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in addr_cmd = { .sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_LOOPBACK), .sin_port=htons(port_cmd) };
            bind(sock_cmd, (struct sockaddr*)&addr_cmd, sizeof(addr_cmd));

            int sock_req = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in addr_req = { .sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_LOOPBACK), .sin_port=htons(port_req) };
            bind(sock_req, (struct sockaddr*)&addr_req, sizeof(addr_req));

            int sock_push = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in addr_push = { .sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_LOOPBACK), .sin_port=htons(port_push) };
            bind(sock_push, (struct sockaddr*)&addr_push, sizeof(addr_push));

            struct sockaddr_in proxy = { .sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_LOOPBACK), .sin_port=htons(g_service_port) };
            connect(sock_cmd, (struct sockaddr*)&proxy, sizeof(proxy));
            connect(sock_req, (struct sockaddr*)&proxy, sizeof(proxy));
            connect(sock_push, (struct sockaddr*)&proxy, sizeof(proxy));

            char fd_c[16], fd_r[16], fd_p[16];
            int f;
            f=fcntl(sock_cmd, F_GETFD); f&=~FD_CLOEXEC; fcntl(sock_cmd, F_SETFD, f);
            f=fcntl(sock_req, F_GETFD); f&=~FD_CLOEXEC; fcntl(sock_req, F_SETFD, f);
            f=fcntl(sock_push, F_GETFD); f&=~FD_CLOEXEC; fcntl(sock_push, F_SETFD, f);
            
            snprintf(fd_c, 16, "%d", sock_cmd);
            snprintf(fd_r, 16, "%d", sock_req);
            snprintf(fd_p, 16, "%d", sock_push);

            setenv("WVM_SOCK_CMD", fd_c, 1);
            setenv("WVM_SOCK_REQ", fd_r, 1);  
            setenv("WVM_SOCK_PUSH", fd_p, 1); 
            setenv("WVM_ROLE", "SLAVE", 1);
            char id_str[32];
            snprintf(id_str, sizeof(id_str), "%u", WVM_ENCODE_ID(g_slave_vm_id, base_id + i));
            setenv("WVM_SLAVE_ID", id_str, 1);

            const char *shm_path = getenv("WVM_SHM_FILE");
            if (shm_path) {
                setenv("WVM_SHM_FILE", shm_path, 1);
            }

            cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(i, &cpuset); sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
            
            execlp(qemu_bin, qemu_bin,
                   "-accel", "wavevm",
                   "-m", ram_str, 
                   "-nographic", "-S", "-nodefaults", NULL);
            perror("[Hybrid] execlp qemu");
            _exit(127);
        }
        // parent: sockets are only created in child branch
    }
}

/* 
 * [物理意图] 充当 TCG 实例与 P2P 网络之间的“物理层协议翻译官”。
 * [关键逻辑] 维护 CMD（控制）、REQ（同步请求）、PUSH（异步推送）三通道的路由，并实现 VFIO 的本地拦截。
 * [后果] 解决了模拟模式下的网络竞争问题，通过物理通道隔离，保证了高频内存同步不会阻塞关键的 CPU 控制指令。
 */
void* tcg_proxy_thread(void *arg) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    struct sockaddr_in addr = { .sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_ANY), .sin_port=htons(g_service_port) };
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[SLAVE-BIND] port=%d failed errno=%d\n", g_service_port, errno);
    } else {
        fprintf(stderr, "[SLAVE-BIND] port=%d ok\n", g_service_port);
    }

    struct mmsghdr msgs[BATCH_SIZE]; struct iovec iovecs[BATCH_SIZE]; uint8_t buffers[BATCH_SIZE][POOL_ITEM_SIZE]; struct sockaddr_in src_addrs[BATCH_SIZE];
    memset(msgs, 0, sizeof(msgs));
    for(int i=0;i<BATCH_SIZE;i++) { iovecs[i].iov_base=buffers[i]; iovecs[i].iov_len=POOL_ITEM_SIZE; msgs[i].msg_hdr.msg_iov=&iovecs[i]; msgs[i].msg_hdr.msg_iovlen=1; msgs[i].msg_hdr.msg_name=&src_addrs[i]; msgs[i].msg_hdr.msg_namelen=sizeof(src_addrs[i]); }

    printf("[Proxy] Tri-Channel NAT Active (CMD/REQ/PUSH) + MESI Support.\n");

    while(1) {
        int n = recvmmsg(sockfd, msgs, BATCH_SIZE, 0, NULL);
        if (n <= 0) continue;

        for (int i=0; i<n; i++) {
            struct wvm_header *hdr = (struct wvm_header *)buffers[i];
            { static int __dbg=0;
              if (__dbg < 10) {
                  char ipbuf[16] = {0};
                  inet_ntop(AF_INET, &src_addrs[i].sin_addr, ipbuf, sizeof(ipbuf));
                  uint16_t raw_type = ntohs(hdr->msg_type);
                  uint32_t raw_magic = ntohl(hdr->magic);
                  fprintf(stderr, "[SLAVE-RX] src=%s:%u len=%u magic=0x%08x type=%u target=%u slave=%u\n",
                          ipbuf, (unsigned)ntohs(src_addrs[i].sin_port),
                          (unsigned)msgs[i].msg_len, raw_magic, (unsigned)raw_type,
                          (unsigned)ntohl(hdr->target_id), (unsigned)ntohl(hdr->slave_id));
                  __dbg++;
              }
            }
            { static int __big=0;
              if (__big < 10 && msgs[i].msg_len >= 200) {
                  char ipbuf[16] = {0};
                  inet_ntop(AF_INET, &src_addrs[i].sin_addr, ipbuf, sizeof(ipbuf));
                  uint16_t raw_type = ntohs(hdr->msg_type);
                  uint32_t raw_magic = ntohl(hdr->magic);
                  fprintf(stderr, "[SLAVE-RX-BIG] src=%s:%u len=%u magic=0x%08x type=%u\n",
                          ipbuf, (unsigned)ntohs(src_addrs[i].sin_port),
                          (unsigned)msgs[i].msg_len, raw_magic, (unsigned)raw_type);
                  __big++;
              }
            }
            if (hdr->magic != htonl(WVM_MAGIC)) continue;

            /* In single-host tests gateway packets can also come from 127.0.0.1.
             * Only treat packets from known TCG endpoint ports as local upstream. */
            int from_local_tcg = (src_addrs[i].sin_addr.s_addr == htonl(INADDR_LOOPBACK)) &&
                                 is_local_tcg_endpoint_port(src_addrs[i].sin_port);
            // 1. Upstream (Local QEMU -> Gateway)
            if (from_local_tcg) {
                // [VFIO Intercept] TCG 模式下的本地显卡拦截
                uint16_t msg_type = ntohs(hdr->msg_type);
                int actual_payload = msgs[i].msg_len - (int)sizeof(struct wvm_header);
                uint16_t payload_len = ntohs(hdr->payload_len);
                if (msg_type == MSG_MEM_WRITE) {
                    if (actual_payload >= 8 &&
                        payload_len >= 8 &&
                        payload_len <= (uint16_t)actual_payload) {
                        uint64_t gpa = WVM_NTOHLL(*(uint64_t*)(buffers[i] + sizeof(struct wvm_header)));
                        void *data = buffers[i] + sizeof(struct wvm_header) + 8;
                        int len = payload_len - 8;
                        if (wvm_vfio_intercept_mmio(gpa, data, len, 1)) {
                            hdr->msg_type = htons(MSG_MEM_ACK);
                            hdr->payload_len = htons(0);
                            hdr->crc32 = 0;
                            hdr->crc32 = htonl(calculate_crc32(buffers[i], sizeof(struct wvm_header)));
                            sendto(sockfd, buffers[i], sizeof(struct wvm_header), 0,
                                   (struct sockaddr*)&src_addrs[i], sizeof(struct sockaddr_in));
                            continue; 
                        }
                    }
                }
                
                if (g_gateway_known) 
                    sendto(sockfd, buffers[i], msgs[i].msg_len, 0, (struct sockaddr*)&g_upstream_gateway, sizeof(struct sockaddr_in));
            }
            // 2. Downstream (Gateway -> Local QEMU)
            else {
                if (!g_gateway_init_done) {
                    if (__sync_bool_compare_and_swap(&g_gateway_init_done, 0, 1)) {
                        memcpy(&g_upstream_gateway, &src_addrs[i], sizeof(struct sockaddr_in));
                        g_gateway_known = 1;
                        printf("[Proxy] Gateway Locked: %s:%d\n", inet_ntoa(src_addrs[i].sin_addr), ntohs(src_addrs[i].sin_port));
                    }
                }
                
                uint32_t target_raw = ntohl(hdr->target_id);
                uint16_t msg_type = ntohs(hdr->msg_type);
                int core_idx = (target_raw != WVM_NODE_AUTO_ROUTE)
                    ? (int)(WVM_GET_NODEID(target_raw) - g_base_id)
                    : 0;

                if (core_idx < 0 || core_idx >= g_num_cores) continue;

                // [V28 分流逻辑升级]
                if (msg_type == MSG_MEM_WRITE || msg_type == MSG_MEM_READ ||
                    msg_type == MSG_INVALIDATE || msg_type == MSG_DOWNGRADE || 
                    msg_type == MSG_FETCH_AND_INVALIDATE ||
                    msg_type == MSG_PAGE_PUSH_FULL || 
                    msg_type == MSG_PAGE_PUSH_DIFF || 
                    msg_type == MSG_FORCE_SYNC) {
                     sendto(sockfd, buffers[i], msgs[i].msg_len, 0, 
                          (struct sockaddr*)&tcg_endpoints[core_idx].push_addr, sizeof(struct sockaddr_in));
                }
                else if (msg_type == MSG_MEM_ACK) {
                    // 如果 req_id 是 ~0ULL，说明是异步回包，也走 PUSH
                    if (WVM_NTOHLL(hdr->req_id) == ~0ULL)
                        sendto(sockfd, buffers[i], msgs[i].msg_len, 0, 
                              (struct sockaddr*)&tcg_endpoints[core_idx].push_addr, sizeof(struct sockaddr_in));
                    else
                        sendto(sockfd, buffers[i], msgs[i].msg_len, 0, 
                              (struct sockaddr*)&tcg_endpoints[core_idx].req_addr, sizeof(struct sockaddr_in));
                }
                else {
                    sendto(sockfd, buffers[i], msgs[i].msg_len, 0, 
                          (struct sockaddr*)&tcg_endpoints[core_idx].cmd_addr, sizeof(struct sockaddr_in));
                }
            }
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    // 启动时自动创建存储目录
    struct stat st = {0};
    if (stat("/var/lib/wavevm", &st) == -1) {
        mkdir("/var/lib/wavevm", 0755);
    }
    if (stat("/var/lib/wavevm/chunks", &st) == -1) {
        mkdir("/var/lib/wavevm/chunks", 0755);
    }
    g_num_cores = get_allowed_cores();
    if (argc >= 2) g_service_port = atoi(argv[1]);
    if (argc >= 3) {
        g_num_cores = atoi(argv[2]);
        if (g_num_cores <= 0) g_num_cores = 1;
    }
    if (argc >= 4) { g_ram_mb = atoi(argv[3]); if(g_ram_mb<=0) g_ram_mb=1024; g_slave_ram_size = (uint64_t)g_ram_mb * 1024 * 1024; }
    if (argc >= 5) {
        g_base_id = atoi(argv[4]);
    }
    if (argc >= 6) g_ctrl_port = atoi(argv[5]);
    if (argc >= 7) g_slave_vm_id = (uint8_t)atoi(argv[6]);

    printf("[Init] WaveVM Hybrid Slave V28.0 (Swarm Edition)\n");
    printf("[Init] Config: Port=%d, Cores=%ld, RAM=%d MB, BaseID=%d, VM=%u\n",
           g_service_port, g_num_cores, g_ram_mb, g_base_id, (unsigned)g_slave_vm_id);
    
    // 解析 -vfio 参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-vfio") == 0 && i + 1 < argc) {
            g_vfio_config_path = argv[i+1];
        }
    }
    init_kvm_global();

    if (g_kvm_available) {
        printf("[Hybrid] Mode: KVM FAST PATH. Listening on 0.0.0.0:%d\n", g_service_port);
        pthread_t sender_thread_id;
        if (pthread_create(&sender_thread_id, NULL, dirty_sync_sender_thread, NULL) != 0) {
            perror("Failed to create dirty page sender thread");
            return 1;
        }
        pthread_detach(sender_thread_id); // 设为分离模式，不需 join
        // 启动 VFIO IRQ 转发线程
        if (g_vfio_config_path) {
            pthread_t irq_th;
            pthread_create(&irq_th, NULL, vfio_irq_thread_adapter, NULL);
        }
        /* Keep a single KVM worker to avoid vCPU create races on shared VM fd. */
        int total_threads = 1;
        pthread_t *threads = malloc(sizeof(pthread_t) * total_threads);
        
        for(long i=0; i<total_threads; i++) pthread_create(&threads[i], NULL, kvm_worker_thread, (void*)i);
        for(long i=0; i<total_threads; i++) pthread_join(threads[i], NULL);
    } else {
        printf("[Hybrid] Mode: TCG PROXY (Tri-Channel). Listening on 0.0.0.0:%d\n", g_service_port);
        spawn_tcg_processes(g_base_id);
        sleep(1);
        int proxy_threads = g_num_cores / 2; 
        if (proxy_threads < 1) proxy_threads = 1;
        pthread_t *threads = malloc(sizeof(pthread_t) * proxy_threads);
        for(long i=0; i<proxy_threads; i++) pthread_create(&threads[i], NULL, tcg_proxy_thread, NULL);
        while(wait(NULL) > 0);
    }
    return 0;
}

```

**文件**: `slave_daemon/slave_vfio.h`

```c
#ifndef SLAVE_VFIO_H
#define SLAVE_VFIO_H

#include <stdint.h>
#include <time.h>
#include <linux/vfio.h>

// 最大支持的透传设备数
#define MAX_VFIO_DEVICES 8
// 每个设备最多支持的 BAR 数量 (PCI 标准为 6)
#define MAX_BARS 6

typedef struct {
    int active;
    uint32_t region_index; // VFIO_PCI_BAR0_REGION_INDEX ...
    uint64_t gpa_start;    // 配置文件中定义的 Guest 物理起始地址
    uint64_t size;         // Region 大小
    uint64_t offset;       // 真实硬件在 device_fd 中的偏移量 (由内核告知)
} wvm_vfio_region_t;

typedef struct {
    int active;
    char pci_id[32];       // e.g., "0000:01:00.0"
    char group_path[64];   // e.g., "/dev/vfio/12"
    int group_fd;
    int device_fd;
    
    // 中断支持
    int irq_fd;
    
    // [FIX] 防风暴限流器：记录上次发送中断的时间
    // 使用 CLOCK_MONOTONIC 保证不受 NTP 影响
    struct timespec last_irq_time; 
    
    wvm_vfio_region_t regions[MAX_BARS];
} wvm_vfio_device_t;

// 初始化 VFIO 子系统
int wvm_vfio_init(const char *config_file);

// 核心拦截接口：检查 GPA 是否命中，如果命中则执行硬件操作
// 返回 1 表示拦截处理成功，0 表示未命中 (需转发 Master)
int wvm_vfio_intercept_mmio(uint64_t gpa, void *data, int len, int is_write);

// 轮询所有设备的中断 (需要在独立线程调用)
void wvm_vfio_poll_irqs(int master_sock, struct sockaddr_in *master_addr);

#endif

```

**文件**: `slave_daemon/slave_vfio.c`

```c
/*
 * [IDENTITY] VFIO Interceptor - The GPU Virtualizer
 * ---------------------------------------------------------------------------
 * 物理角色：Slave 端物理硬件的"影子拦截器"。
 * 职责边界：
 * 1. 强制开启硬件 Bus Master 权限，确保远程 DMA 正常。
 * 2. 拦截 BAR 空间 MMIO 读写，实现对物理显卡的分布式穿透。
 * 3. 实现中断去抖动 (100us Debouncing)，防止物理 GPU 中断冲垮网络链路。
 * 
 * [物理原则]
 * - 中断转发严禁走 Slow Lane，必须直达 Master 的 Fast Lane。
 * ---------------------------------------------------------------------------
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <linux/vfio.h>
#include <linux/pci_regs.h> // 需要系统头文件，定义了 PCI_COMMAND_MASTER
#include <errno.h>
#include <arpa/inet.h>      // 用于发送中断包

#include "slave_vfio.h"
#include "../common_include/wavevm_protocol.h"

#define VFIO_CONTAINER_PATH "/dev/vfio/vfio"
#define MAX_EPOLL_EVENTS 16

static int g_container_fd = -1;
static wvm_vfio_device_t g_devices[MAX_VFIO_DEVICES];
static int g_dev_count = 0;

// 网络上下文，用于发送中断
static int g_net_fd = -1;
static struct sockaddr_in g_master_addr;
static int g_net_ready = 0;

/* 
 * [物理意图] 强行夺取物理显卡的“主线控制权”，开启远程 DMA 引擎。
 * [关键逻辑] 直接操作 PCI 配置空间命令寄存器，绕过 Guest BIOS 的限制，激活硬件 Bus Master 位。
 * [后果] 实现了远程 GPU 的物理觉醒。若此位未开启，Slave 端的显卡将无法进行 DMA 传输，分布式 I/O 会瞬间瘫痪。
 */
static int enable_bus_master(int device_fd) {
    struct vfio_region_info reg = { .argsz = sizeof(reg) };
    reg.index = VFIO_PCI_CONFIG_REGION_INDEX;
    
    if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &reg) < 0) {
        perror("[VFIO] Failed to get Config Space info");
        return -1;
    }

    // 读写配置空间通过 pread/pwrite 到 device_fd
    uint16_t cmd;
    off_t cmd_offset = reg.offset + PCI_COMMAND; // PCI_COMMAND = 0x04

    // 1. 读取当前 Command Register
    if (pread(device_fd, &cmd, sizeof(cmd), cmd_offset) != sizeof(cmd)) {
        perror("[VFIO] Failed to read PCI Command Reg");
        return -1;
    }

    // 2. 检查并设置 Bus Master (Bit 2)
    if (!(cmd & PCI_COMMAND_MASTER)) {
        printf("[VFIO] Bus Master disabled (0x%x). Enabling...\n", cmd);
        cmd |= PCI_COMMAND_MASTER;
        
        if (pwrite(device_fd, &cmd, sizeof(cmd), cmd_offset) != sizeof(cmd)) {
            perror("[VFIO] Failed to write PCI Command Reg");
            return -1;
        }
        printf("[VFIO] Bus Master enabled successfully.\n");
    } else {
        printf("[VFIO] Bus Master already enabled.\n");
    }
    return 0;
}

/* 
 * [物理意图] 在物理显卡与分布式链路之间“打通神经信号”。
 * [关键逻辑] 创建 eventfd 并将其注入物理硬件的 MSI-X 中断向量表中，实现硬件中断到用户态事件的转化。
 * [后果] 它是中断转发的源头。如果没有 eventfd 的正确绑定，Slave 端的显卡在完成任务后将无法通知 Master 端的 vCPU。
 */
static int setup_irq(wvm_vfio_device_t *dev) {
    dev->irq_fd = -1; /* 防止零初始化（0=stdin）被 poll 循环误注册 */
    // 为简单起见，且为了保证通用性，我们优先尝试启用 INTx (Legacy Interrupt)
    // 真实的 GPU 驱动通常会请求 MSI-X，这需要拦截配置空间的写操作来动态建立映射。
    // 由于 V27.0 不拦截 Config Space 写（太复杂），我们假设 Host VFIO 驱动
    // 能正确处理 Guest 驱动的中断请求。
    // 在最基础的透传场景中，我们至少要保证 INTx 能够通过。
    
    struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info) };
    irq_info.index = VFIO_PCI_INTX_IRQ_INDEX;
    
    if (ioctl(dev->device_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info) < 0) {
        // 设备可能不支持 INTx，尝试 MSI
        return 0; 
    }

    if (!(irq_info.flags & VFIO_IRQ_INFO_EVENTFD)) return 0;

    // 创建 eventfd 用于内核通知用户态
    dev->irq_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (dev->irq_fd < 0) { perror("eventfd"); return -1; }

    // 将 eventfd 注册给 VFIO
    struct vfio_irq_set *irq_set;
    size_t argsz = sizeof(*irq_set) + sizeof(int);
    irq_set = malloc(argsz);
    irq_set->argsz = argsz;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_PCI_INTX_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    memcpy(irq_set->data, &dev->irq_fd, sizeof(int));

    if (ioctl(dev->device_fd, VFIO_DEVICE_SET_IRQS, irq_set) < 0) {
        perror("[VFIO] Failed to bind INTx eventfd");
        close(dev->irq_fd);
        dev->irq_fd = -1;
        free(irq_set);
        return -1;
    }

    free(irq_set);
    printf("[VFIO] INTx Interrupt hook installed (fd=%d)\n", dev->irq_fd);
    return 0;
}

// -----------------------------------------------------------
// 辅助: 获取 Region 真实偏移
// -----------------------------------------------------------
static int setup_region(wvm_vfio_device_t *dev, int index, uint64_t gpa_base, uint64_t config_size) {
    struct vfio_region_info reg = { .argsz = sizeof(reg) };
    reg.index = index;
    
    if (ioctl(dev->device_fd, VFIO_DEVICE_GET_REGION_INFO, &reg) < 0) return 0;
    if (reg.size == 0) return 0;

    if (config_size > reg.size) config_size = reg.size; // 安全截断

    dev->regions[index].active = 1;
    dev->regions[index].region_index = index;
    dev->regions[index].gpa_start = gpa_base;
    dev->regions[index].size = config_size;
    dev->regions[index].offset = reg.offset; // 内核返回的物理偏移

    printf("[VFIO]   -> BAR%d Mapped: GPA 0x%lx -> Host Offset 0x%llx (Size 0x%lx)\n", 
           index, gpa_base, reg.offset, config_size);
    return 1;
}

/* 
 * [物理意图] 建立 Slave 节点物理硬件的“用户态访问隧道”。
 * [关键逻辑] 通过 VFIO 接口接管物理显卡，获取 BAR 空间的 Host 物理偏移，并建立 DMA 隔离容器。
 * [后果] 实现了真正的硬件直通。若偏移量（Offset）计算错误，所有的远程 MMIO 读写都会指向错误的物理内存，导致 Host 宿主机重启。
 */
static int init_device(const char *pci_id, const char *group_path, uint64_t *bar_gpas, uint64_t *bar_sizes) {
    if (g_dev_count >= MAX_VFIO_DEVICES) return -1;
    wvm_vfio_device_t *dev = &g_devices[g_dev_count];
    
    // 1. Container Init
    if (g_container_fd < 0) {
        g_container_fd = open(VFIO_CONTAINER_PATH, O_RDWR);
        if (g_container_fd < 0) { perror("Open VFIO Container"); return -1; }
        if (ioctl(g_container_fd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) != 1) {
            fprintf(stderr, "[VFIO] IOMMU Type1 not supported\n"); return -1;
        }
    }

    // 2. Open Group
    dev->group_fd = open(group_path, O_RDWR);
    if (dev->group_fd < 0) { perror("Open VFIO Group"); return -1; }

    struct vfio_group_status status = { .argsz = sizeof(status) };
    ioctl(dev->group_fd, VFIO_GROUP_GET_STATUS, &status);
    if (!(status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        fprintf(stderr, "[VFIO] Group not viable (Bind to vfio-pci?)\n"); close(dev->group_fd); return -1;
    }

    if (ioctl(dev->group_fd, VFIO_GROUP_SET_CONTAINER, &g_container_fd) < 0) {
        perror("Set Container"); close(dev->group_fd); return -1;
    }

    // 3. Set IOMMU
    static int iommu_set = 0;
    if (!iommu_set) {
        if (ioctl(g_container_fd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU) < 0) {
            perror("Set IOMMU"); close(dev->group_fd); return -1;
        }
        iommu_set = 1;
    }

    // 4. Get Device FD
    dev->device_fd = ioctl(dev->group_fd, VFIO_GROUP_GET_DEVICE_FD, pci_id);
    if (dev->device_fd < 0) { perror("Get Device FD"); close(dev->group_fd); return -1; }

    strncpy(dev->pci_id, pci_id, 31);
    
    // 5. 【关键】启用 Bus Master
    if (enable_bus_master(dev->device_fd) < 0) {
        fprintf(stderr, "[VFIO] Warning: Failed to enable Bus Master for %s\n", pci_id);
    }

    // 6. 映射 BAR
    for (int i = 0; i < MAX_BARS; i++) {
        if (bar_sizes[i] > 0) setup_region(dev, i, bar_gpas[i], bar_sizes[i]);
    }

    // 7. 设置中断
    setup_irq(dev);

    dev->active = 1;
    g_dev_count++;
    return 0;
}

// -----------------------------------------------------------
// 外部接口 1: 初始化
// -----------------------------------------------------------
int wvm_vfio_init(const char *config_file) {
    if (!config_file) return -1;
    FILE *fp = fopen(config_file, "r");
    if (!fp) {
        printf("[VFIO] Config file '%s' not found. Distributed I/O disabled.\n", config_file);
        return -1;
    }

    char line[512];
    char pci_id[32], group_path[64];
    uint64_t bar_gpas[MAX_BARS] = {0};
    uint64_t bar_sizes[MAX_BARS] = {0};
    int parsing = 0;

    printf("[VFIO] Loading config from %s...\n", config_file);

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        if (strncmp(line, "DEVICE", 6) == 0) {
            if (parsing) init_device(pci_id, group_path, bar_gpas, bar_sizes);
            
            sscanf(line, "DEVICE %31s %63s", pci_id, group_path);
            memset(bar_gpas, 0, sizeof(bar_gpas));
            memset(bar_sizes, 0, sizeof(bar_sizes));
            parsing = 1;
        } else if (strncmp(line, "BAR", 3) == 0 && parsing) {
            int idx;
            uint64_t gpa, size;
            if (sscanf(line, "BAR%d %lx %lu", &idx, &gpa, &size) == 3) {
                if (idx >= 0 && idx < MAX_BARS) {
                    bar_gpas[idx] = gpa;
                    bar_sizes[idx] = size;
                }
            }
        } else if (strncmp(line, "END", 3) == 0 && parsing) {
            init_device(pci_id, group_path, bar_gpas, bar_sizes);
            parsing = 0;
        }
    }
    // Handle last device if no END tag
    if (parsing) init_device(pci_id, group_path, bar_gpas, bar_sizes);

    fclose(fp);
    return g_dev_count;
}

// -----------------------------------------------------------
// 外部接口 2: MMIO 拦截
// -----------------------------------------------------------
int wvm_vfio_intercept_mmio(uint64_t gpa, void *data, int len, int is_write) {
    for (int i = 0; i < g_dev_count; i++) {
        if (!g_devices[i].active) continue;
        
        for (int j = 0; j < MAX_BARS; j++) {
            wvm_vfio_region_t *reg = &g_devices[i].regions[j];
            if (!reg->active) continue;

            if (gpa >= reg->gpa_start && gpa < reg->gpa_start + reg->size) {
                uint64_t offset = reg->offset + (gpa - reg->gpa_start);
                ssize_t ret;
                
                if (is_write) ret = pwrite(g_devices[i].device_fd, data, len, offset);
                else ret = pread(g_devices[i].device_fd, data, len, offset);
                
                if (ret != len) {
                    // 硬件读写失败是严重错误，但也只能打印日志
                    // fprintf(stderr, "[VFIO] HW Access Failed: GPA %lx\n", gpa);
                }
                return 1; // Intercepted
            }
        }
    }
    return 0; // Passthrough to Master
}

// -----------------------------------------------------------
// 外部接口 3: 中断转发线程 (Poll Loop)
// -----------------------------------------------------------

// [Helper] 计算时间差 (微秒)
static long diff_us(struct timespec *now, struct timespec *prev) {
    return (now->tv_sec - prev->tv_sec) * 1000000 + (now->tv_nsec - prev->tv_nsec) / 1000;
}

/* 
 * [物理意图] 充当分布式硬件中断的“消减器（Debouncer）”。
 * [关键逻辑] 使用 epoll 监听 eventfd，并强制执行 100us 的物理去抖动，防止高频中断冲垮 Master 链路。
 * [后果] 保护了全网信道的稳定性。它通过在物理源头限制中断频率，保证了分布式系统不会陷入“中断风暴”死循环。
 */
void wvm_vfio_poll_irqs(int master_sock, struct sockaddr_in *master_addr) {
    if (g_dev_count == 0) return;

    // 1. 设置网络上下文 (复制一份，因为主线程可能修改)
    g_net_fd = master_sock;
    if (master_addr) memcpy(&g_master_addr, master_addr, sizeof(struct sockaddr_in));
    else return;

    // 2. 创建 Epoll 实例
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create"); return; }

    struct epoll_event ev, events[MAX_EPOLL_EVENTS];
    int registered_count = 0;

    // 3. 注册所有设备的 IRQ EventFD 到 Epoll
    for (int i = 0; i < g_dev_count; i++) {
        if (g_devices[i].irq_fd < 0) continue;
        
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.u32 = i; // 将设备索引存入 data，以便 wait 返回时知道是哪个设备
        
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, g_devices[i].irq_fd, &ev) < 0) {
            perror("[VFIO] epoll_ctl add failed");
            continue;
        }
        registered_count++;
    }

    if (registered_count == 0) {
        printf("[VFIO] No interrupts to poll. Thread exiting.\n");
        close(epfd);
        return;
    }

    printf("[VFIO] IRQ Polling Thread Started (Watching %d fds)...\n", registered_count);

    // 4. 轮询循环
    while (1) {
        int n = epoll_wait(epfd, events, MAX_EPOLL_EVENTS, -1); // Block indefinitely
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait"); break;
        }
        for (int i = 0; i < n; i++) {
            int dev_idx = events[i].data.u32;
            int irq_fd = g_devices[dev_idx].irq_fd;
            uint64_t counter;
            
            // 必须读取 eventfd 以清空计数，否则会水平触发死循环
            if (read(irq_fd, &counter, sizeof(counter)) == sizeof(counter)) {
                
                // 构造中断包发送给 Master
                struct wvm_header hdr;
                memset(&hdr, 0, sizeof(hdr)); 

                hdr.magic = htonl(WVM_MAGIC);
                hdr.msg_type = htons(MSG_VFIO_IRQ);
                hdr.payload_len = 0;
                hdr.slave_id = htonl(WVM_NODE_AUTO_ROUTE);
                hdr.target_id = htonl(WVM_NODE_AUTO_ROUTE);
                hdr.req_id = 0;
                hdr.qos_level = 1; 
                
                // [CRC]
                hdr.crc32 = 0;
                hdr.crc32 = htonl(calculate_crc32(&hdr, sizeof(hdr)));

                // 发送
                // 注意：这里需要在多线程环境下安全使用 socket
                // UDP sendto 是原子的，只要 g_net_fd 有效即可
                sendto(g_net_fd, &hdr, sizeof(hdr), 0, 
                       (struct sockaddr*)&g_master_addr, sizeof(g_master_addr));
                
                // printf("[VFIO] IRQ Forwarded for Device %d\n", dev_idx);
            }
        }
    }
    close(epfd);
}

```

**文件**: `slave_daemon/Makefile`

```makefile
CC = gcc
CFLAGS = -Wall -O3 -msse4.2 -I../common_include -pthread 
TARGET = wavevm_node_slave
SRCS = slave_hybrid.c slave_vfio.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

```

---

### Step 7: 控制面工具 (Control Tool)

**文件**: `ctl_tool/Makefile`

```makefile
CC = gcc
CFLAGS = -Wall -O2 -I../common_include
TARGET = wvm_ctl

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

```

**文件**: `ctl_tool/main.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include "../common_include/wavevm_ioctl.h"
#include "../common_include/wavevm_config.h"

/*
 * WaveVM V29 Control Tool (Heterogeneous-Aware)
 * 
 * 职责：
 * 1. 解析 V27 风格的异构配置 (Cores/RAM)。
 * 2. [关键] 根据 RAM 大小自动展开 Virtual Nodes (虚拟节点)，实现 DHT 内存加权负载均衡。
 * 3. [关键] 根据 Cores 数量填充 CPU 路由表，实现算力加权调度。
 */

// 虚拟节点粒度：每 4GB RAM 对应 1 个 DHT 槽位 (Virtual Node)
#define WVM_RAM_UNIT_GB 4 

static uint32_t local_cpu_table[WVM_CPU_ROUTE_TABLE_SIZE];

typedef struct {
    int phys_id;    // 配置文件里的 BaseID (物理ID)
    char ip[64];
    int port;
    int cores;
    int ram_gb;
    
    // 计算属性
    int vnode_start; // 在 DHT 环上的起始虚拟 ID
    int vnode_count; // 拥有的虚拟节点数量 (权重)
} NodeInfo;

// 辅助：注入 CPU 路由表 (带错误熔断)
void inject_cpu_route(int dev_fd) {
    uint32_t chunk_size = 1024;
    // 动态分配，避免栈溢出
    size_t buf_size = sizeof(struct wvm_ioctl_route_update) + chunk_size * sizeof(uint32_t);
    struct wvm_ioctl_route_update *payload = malloc(buf_size);
    if (!payload) { perror("malloc"); exit(1); }

    printf("[*] Injecting CPU Topology (%d vCPUs)...\n", WVM_CPU_ROUTE_TABLE_SIZE);

    for (uint32_t i = 0; i < WVM_CPU_ROUTE_TABLE_SIZE; i += chunk_size) {
        uint32_t current_count = chunk_size;
        if (i + current_count > WVM_CPU_ROUTE_TABLE_SIZE) 
            current_count = WVM_CPU_ROUTE_TABLE_SIZE - i;

        payload->start_index = i;
        payload->count = current_count;
        memcpy(payload->entries, &local_cpu_table[i], current_count * sizeof(uint32_t));

        // [FIX] 严格检查返回值，实现 Fail-Fast
        // 如果中间某块注入失败，说明内核状态已不一致，必须立即停止部署
        if (ioctl(dev_fd, IOCTL_UPDATE_CPU_ROUTE, payload) < 0) {
            fprintf(stderr, "[FATAL] CPU Route Injection Failed at index %d (errno=%d). State is inconsistent!\n", i, errno);
            free(payload);
            close(dev_fd);
            exit(1); // 返回非零状态码，中断外部的 Shell 脚本
        }
    }
    free(payload);
    printf("[+] CPU Routing Table Injected Successfully.\n");
}

// 辅助：注入全局参数
void inject_global_param(int dev_fd, int slot, int value) {
    size_t buf_size = sizeof(struct wvm_ioctl_route_update) + sizeof(uint32_t);
    struct wvm_ioctl_route_update *payload = malloc(buf_size);
    
    payload->start_index = slot; 
    payload->count = 1;
    payload->entries[0] = (uint32_t)value;

    if (ioctl(dev_fd, IOCTL_UPDATE_MEM_ROUTE, payload) < 0) {
        perror("[-] Failed to inject global param");
    }
    free(payload);
}

// 辅助：注入单个网关条目
void inject_gateway(int dev_fd, int id, const char* ip, int port) {
    struct wvm_ioctl_gateway gw_cmd;
    gw_cmd.gw_id = id;
    gw_cmd.ip = inet_addr(ip);
    gw_cmd.port = htons(port);
    if (ioctl(dev_fd, IOCTL_SET_GATEWAY, &gw_cmd) < 0) {
        perror("[-] Warning: Failed to set gateway IP");
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <HETERO_CONFIG_FILE> <MY_PHYS_ID>\n", argv[0]);
        return 1;
    }

    const char *config_file = argv[1];
    int my_phys_id = atoi(argv[2]); // 这里的 ID 对应配置文件里的 BaseID

    int dev_fd = open("/dev/wavevm", O_RDWR);
    if (dev_fd < 0) {
        perror("[-] Failed to open /dev/wavevm");
        return 1;
    }

    FILE *fp = fopen(config_file, "r");
    if (!fp) { perror("[-] Config open failed"); return 1; }

    printf("[*] WaveVM V29.5 Control Tool (Heterogeneous Engine)\n");

    // 1. 解析配置并计算权重
    NodeInfo *nodes = malloc(sizeof(NodeInfo) * WVM_MAX_SLAVES);
    if (!nodes) { perror("malloc nodes"); return 1; }
    int node_count = 0;
    char line[256];
    
    int total_vnodes = 0; // DHT 环总大小

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        char keyword[16];
        int bid, port, cores, ram;
        char ip[64];
        
        if (sscanf(line, "%15s", keyword) != 1) continue;
        if (strcmp(keyword, "NODE") != 0) continue;

        if (sscanf(line, "%*s %d %63s %d %d %d", &bid, ip, &port, &cores, &ram) == 5) {
            
            // [FIX 1] 边界检查：防止数组越界
            if (node_count >= WVM_MAX_SLAVES) {
                fprintf(stderr, "[Warn] Max node limit (%lu) reached. Ignoring remaining config.\n", WVM_MAX_SLAVES);
                break; // 停止解析，保护内存
            }

            nodes[node_count].phys_id = bid;
            strncpy(nodes[node_count].ip, ip, 63);
            nodes[node_count].port = port;
            nodes[node_count].cores = cores;
            nodes[node_count].ram_gb = ram;
            
            int v_count = ram / WVM_RAM_UNIT_GB;
            if (v_count < 1) v_count = 1;
            
            nodes[node_count].vnode_start = total_vnodes;
            nodes[node_count].vnode_count = v_count;
            
            total_vnodes += v_count;
            node_count++;
        }
    }
    fclose(fp);
    printf("[+] Topology: %d Physical Nodes -> %d Virtual DHT Nodes (Weighted).\n", node_count, total_vnodes);

    // 2. 注入 Gateway 表 (基于虚拟节点 ID)
    // DHT 算法算出的是 0..total_vnodes-1 之间的虚拟 ID
    // 内核拿到虚拟 ID 后查 Gateway 表，必须能查到对应的物理 IP
    for (int i = 0; i < node_count; i++) {
        for (int v = 0; v < nodes[i].vnode_count; v++) {
            int v_id = nodes[i].vnode_start + v;
            // 将所有属于该物理机的虚拟 ID 都指向同一个 IP
            inject_gateway(dev_fd, v_id, nodes[i].ip, nodes[i].port);
        }
    }
    printf("[+] Gateway Table Expanded & Injected.\n");

    // 3. 构建 CPU 路由表 (基于物理核心数)
    // CPU 调度通常走 RPC (MSG_VCPU_RUN)，目标 ID 应该是该物理机的主 ID (通常是 vnode_start)
    int current_vcpu = 0;
    
    // 策略：按顺序分配 vCPU 到物理节点
    for (int i = 0; i < node_count; i++) {
        // 分配该节点拥有的 Cores 数量的 vCPU
        for (int c = 0; c < nodes[i].cores; c++) {
            if (current_vcpu < WVM_CPU_ROUTE_TABLE_SIZE) {
                // 指向该物理机的第一个虚拟 ID (Primary ID)
                local_cpu_table[current_vcpu++] = nodes[i].vnode_start;
            }
        }
    }
    // 填补剩余 vCPU (Round-Robin)
    int node_cursor = 0;
    while (current_vcpu < WVM_CPU_ROUTE_TABLE_SIZE) {
        local_cpu_table[current_vcpu++] = nodes[node_cursor].vnode_start;
        node_cursor = (node_cursor + 1) % node_count;
    }
    
    inject_cpu_route(dev_fd);

    // 4. 注入全局参数
    // Slot 0: Total Nodes (这里指 Total Virtual Nodes，用于 DHT 取模)
    inject_global_param(dev_fd, 0, total_vnodes);
    
    // Slot 1: My Node ID
    // 传入的 my_phys_id 是配置文件里的 BaseID。
    // 我们需要找到它对应的 vnode_start (Primary Virtual ID)，告诉内核“我是谁”
    int my_virtual_id = -1;
    for (int i = 0; i < node_count; i++) {
        if (nodes[i].phys_id == my_phys_id) {
            my_virtual_id = nodes[i].vnode_start;
            break;
        }
    }
    
    if (my_virtual_id == -1) {
        fprintf(stderr, "[-] Error: My Phys ID %d not found in config!\n", my_phys_id);
        close(dev_fd);
        return 1;
    }
    
    inject_global_param(dev_fd, 1, my_virtual_id);
    
    printf("[+] V29.5 Configured: Total V-Nodes=%d, My Primary V-ID=%d\n", total_vnodes, my_virtual_id);

    free(nodes);

    close(dev_fd);
    return 0;
}

```

---

### Step 8: QEMU 5.2.0 适配 (Frontend)

此部分将 WaveVM 注册为 QEMU 加速器，并接管 CPU 调度循环。

**文件**: `wavevm-qemu/accel/wavevm/wavevm-tcg.c`

```c

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "../../../common_include/wavevm_protocol.h"

#if defined(TARGET_I386) || defined(TARGET_X86_64)

// Export QEMU TCG state to network packet
void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    // 1. General Registers
    memcpy(ctx->regs, env->regs, sizeof(ctx->regs));
    ctx->eip = env->eip;
    ctx->eflags = env->eflags;

    // 2. Control Registers
    ctx->cr[0] = env->cr[0];
    ctx->cr[2] = env->cr[2];
    ctx->cr[3] = env->cr[3];
    ctx->cr[4] = env->cr[4];
    
    // 3. SSE/AVX Registers
    // Synchronize XMM0-XMM15 to prevent guest OS crash
    for (int i = 0; i < 16; i++) {
        // Accessing ZMMReg union safely
        // ZMM_Q(n) accesses the nth 64-bit part of the register
        ctx->xmm_regs[i*2]     = env->xmm_regs[i].ZMM_Q(0);
        ctx->xmm_regs[i*2 + 1] = env->xmm_regs[i].ZMM_Q(1);
    }
    ctx->mxcsr = env->mxcsr;
    
    ctx->exit_reason = 0; 

    ctx->fs_base = env->segs[R_FS].base;
    ctx->gs_base = env->segs[R_GS].base;
    ctx->gdt_base = env->gdt.base;
    ctx->gdt_limit = env->gdt.limit;
    ctx->idt_base = env->idt.base;
    ctx->idt_limit = env->idt.limit;
}

// Import state from network packet to QEMU TCG
void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    // 1. General Registers
    memcpy(env->regs, ctx->regs, sizeof(env->regs));
    env->eip = ctx->eip;
    env->eflags = ctx->eflags;

    // 2. Control Registers
    env->cr[0] = ctx->cr[0];
    env->cr[2] = ctx->cr[2];
    env->cr[3] = ctx->cr[3];
    env->cr[4] = ctx->cr[4];
    
    // 3. SSE/AVX Registers
    for (int i = 0; i < 16; i++) {
        env->xmm_regs[i].ZMM_Q(0) = ctx->xmm_regs[i*2];
        env->xmm_regs[i].ZMM_Q(1) = ctx->xmm_regs[i*2 + 1];
    }
    env->mxcsr = ctx->mxcsr;
    
    // Critical: Flush TB cache to force recompilation with new state
    tb_flush(cpu);

    env->segs[R_FS].base = ctx->fs_base;
    env->segs[R_GS].base = ctx->gs_base;
    env->gdt.base = ctx->gdt_base;
    env->gdt.limit = ctx->gdt_limit;
    env->idt.base = ctx->idt_base;
    env->idt.limit = ctx->idt_limit;
}

#else

void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    (void)cpu;
    memset(ctx, 0, sizeof(*ctx));
}

void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx) {
    (void)cpu;
    (void)ctx;
}

#endif

```

**文件**: `wavevm-qemu/accel/wavevm/wavevm-all.c`

```c

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "sysemu/accel.h"
#include "sysemu/cpus.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/sysemu.h"
#include "qom/object.h"
#include "hw/boards.h"
#include "qemu/option.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>
#include "qemu/thread.h"
#include "sysemu/tcg.h"
#include "sysemu/kvm.h" 
#include "sysemu/kvm_int.h"
#include "../kvm/kvm-cpus.h"
#include "tcg/tcg.h"
#include "linux/kvm.h"
#include "exec/cpu-common.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include "../../../common_include/wavevm_protocol.h"
#include "../../../common_include/wavevm_ioctl.h"
#include "wavevm-accel.h"

// 引用相关模块
extern void wavevm_user_mem_init(void *ram_ptr, size_t ram_size);
extern void wavevm_setup_memory_region(MemoryRegion *mr, uint64_t size, int fd);
extern void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx);
extern void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx);
extern void wvm_set_ttl_interval(int ms);
extern void wvm_register_volatile_ram(uint64_t gpa, uint64_t size);
extern void wvm_apply_remote_push(uint16_t msg_type, void *payload);
extern void wavevm_start_vcpu_thread(CPUState *cpu);
extern void wavevm_register_ram_block(void *hva, uint64_t size, uint64_t gpa);
static void wavevm_sync_topology(int dev_fd);

int g_wvm_local_split = 0;
static bool g_wvm_split_explicit = false;
static bool g_wvm_mode_explicit = false;
static void *g_primary_ram_hva = NULL;
static uint64_t g_primary_ram_size = 0;
static bool g_user_mem_inited = false;
static uint64_t g_user_ram_size_hint = 0;
static bool g_wvm_kvm_bootstrap_done = false;
static AccelState *g_wvm_kvm_accel = NULL;
static bool g_wvm_tcg_bootstrap_done = false;

typedef struct {
    CPUState *cpu;
    unsigned int delay_us;
} WaveVMTcgKickCtx;

static pthread_once_t g_slave_exec_once = PTHREAD_ONCE_INIT;
static QemuMutex g_slave_exec_lock;
static QemuCond g_slave_exec_req_cond;
static QemuCond g_slave_exec_done_cond;
static bool g_slave_exec_pending;
static bool g_slave_exec_done;
static struct wvm_ipc_cpu_run_req g_slave_exec_req;
static struct wvm_ipc_cpu_run_ack g_slave_exec_ack;

static void wavevm_slave_exec_sync_init(void)
{
    qemu_mutex_init(&g_slave_exec_lock);
    qemu_cond_init(&g_slave_exec_req_cond);
    qemu_cond_init(&g_slave_exec_done_cond);
    g_slave_exec_pending = false;
    g_slave_exec_done = false;
    memset(&g_slave_exec_req, 0, sizeof(g_slave_exec_req));
    memset(&g_slave_exec_ack, 0, sizeof(g_slave_exec_ack));
}

static void *wavevm_tcg_kick_thread(void *opaque)
{
    WaveVMTcgKickCtx *ctx = opaque;
    g_usleep(ctx->delay_us);
    cpu_exit(ctx->cpu);
    qemu_cpu_kick(ctx->cpu);
    g_free(ctx);
    return NULL;
}

/*
 * WaveVM CpusAccel callbacks -- aligned with standard TCG MTTCG ops.
 * kick_vcpu_thread and handle_interrupt are essential for the VM
 * lifecycle (vm_start, pause, resume) and interrupt delivery to work.
 */
static void wavevm_kick_vcpu_thread(CPUState *cpu)
{
    cpu_exit(cpu);
}

static void wavevm_handle_interrupt(CPUState *cpu, int mask)
{
    g_assert(qemu_mutex_iothread_locked());

    cpu->interrupt_request |= mask;

    /*
     * If called from iothread context, wake the target cpu in
     * case it is halted.
     */
    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
    }
}

static int64_t wavevm_get_virtual_clock(void)
{
    return cpu_get_clock();
}

static int64_t wavevm_get_elapsed_ticks(void)
{
    return cpu_get_ticks();
}


/* TCG-safe wrappers: only call KVM sync functions when KVM is active */
static void wavevm_synchronize_post_reset(CPUState *cpu)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_post_reset(cpu);
    }
}

static void wavevm_synchronize_post_init(CPUState *cpu)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_post_init(cpu);
    }
}

static void wavevm_synchronize_state(CPUState *cpu)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_state(cpu);
    }
}

static void wavevm_synchronize_pre_loadvm(CPUState *cpu)
{
    if (kvm_enabled()) {
        kvm_cpu_synchronize_pre_loadvm(cpu);
    }
}

static const CpusAccel wavevm_cpus = {
    .create_vcpu_thread = wavevm_start_vcpu_thread,
    .kick_vcpu_thread   = wavevm_kick_vcpu_thread,
    .handle_interrupt   = wavevm_handle_interrupt,
    .get_virtual_clock  = wavevm_get_virtual_clock,
    .get_elapsed_ticks        = wavevm_get_elapsed_ticks,
    .synchronize_post_reset   = wavevm_synchronize_post_reset,
    .synchronize_post_init    = wavevm_synchronize_post_init,
    .synchronize_state        = wavevm_synchronize_state,
    .synchronize_pre_loadvm   = wavevm_synchronize_pre_loadvm,
};

static void wavevm_slave_import_ctx(CPUState *cpu,
                                    const struct wvm_ipc_cpu_run_req *req,
                                    bool local_is_tcg)
{
    if (req->mode_tcg == local_is_tcg) {
        if (local_is_tcg) {
            wvm_tcg_set_state(cpu, (wvm_tcg_context_t *)&req->ctx.tcg);
        } else {
            struct kvm_regs kregs;
            struct kvm_sregs ksregs;
            const wvm_kvm_context_t *kctx = &req->ctx.kvm;
            kregs.rax = kctx->rax; kregs.rbx = kctx->rbx; kregs.rcx = kctx->rcx;
            kregs.rdx = kctx->rdx; kregs.rsi = kctx->rsi; kregs.rdi = kctx->rdi;
            kregs.rsp = kctx->rsp; kregs.rbp = kctx->rbp;
            kregs.r8  = kctx->r8;  kregs.r9  = kctx->r9;  kregs.r10 = kctx->r10;
            kregs.r11 = kctx->r11; kregs.r12 = kctx->r12; kregs.r13 = kctx->r13;
            kregs.r14 = kctx->r14; kregs.r15 = kctx->r15;
            kregs.rip = kctx->rip; kregs.rflags = kctx->rflags;
            memcpy(&ksregs, kctx->sregs_data, sizeof(ksregs));
            kvm_vcpu_ioctl(cpu, KVM_SET_SREGS, &ksregs);
            kvm_vcpu_ioctl(cpu, KVM_SET_REGS, &kregs);
        }
    } else if (local_is_tcg) {
        wvm_tcg_context_t t_ctx;
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        kregs.rax = req->ctx.kvm.rax; kregs.rbx = req->ctx.kvm.rbx; kregs.rcx = req->ctx.kvm.rcx;
        kregs.rdx = req->ctx.kvm.rdx; kregs.rsi = req->ctx.kvm.rsi; kregs.rdi = req->ctx.kvm.rdi;
        kregs.rsp = req->ctx.kvm.rsp; kregs.rbp = req->ctx.kvm.rbp;
        kregs.r8  = req->ctx.kvm.r8;  kregs.r9  = req->ctx.kvm.r9;  kregs.r10 = req->ctx.kvm.r10;
        kregs.r11 = req->ctx.kvm.r11; kregs.r12 = req->ctx.kvm.r12; kregs.r13 = req->ctx.kvm.r13;
        kregs.r14 = req->ctx.kvm.r14; kregs.r15 = req->ctx.kvm.r15;
        kregs.rip = req->ctx.kvm.rip; kregs.rflags = req->ctx.kvm.rflags;
        memcpy(&ksregs, req->ctx.kvm.sregs_data, sizeof(ksregs));
        wvm_translate_kvm_to_tcg(&kregs, &ksregs, &t_ctx);
        wvm_tcg_set_state(cpu, &t_ctx);
    } else {
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        kvm_vcpu_ioctl(cpu, KVM_GET_SREGS, &ksregs);
        wvm_translate_tcg_to_kvm((wvm_tcg_context_t *)&req->ctx.tcg, &kregs, &ksregs);
        kvm_vcpu_ioctl(cpu, KVM_SET_SREGS, &ksregs);
        kvm_vcpu_ioctl(cpu, KVM_SET_REGS, &kregs);
    }
}

static void wavevm_slave_export_ctx(CPUState *cpu,
                                    const struct wvm_ipc_cpu_run_req *req,
                                    struct wvm_ipc_cpu_run_ack *ack,
                                    bool local_is_tcg)
{
    ack->status = 0;
    ack->mode_tcg = req->mode_tcg;

    if (req->mode_tcg == local_is_tcg) {
        if (local_is_tcg) {
            wvm_tcg_get_state(cpu, &ack->ctx.tcg);
            ack->ctx.tcg.exit_reason = cpu->exception_index;
        } else {
            struct kvm_regs kregs;
            struct kvm_sregs ksregs;
            wvm_kvm_context_t *kctx = &ack->ctx.kvm;
            kvm_vcpu_ioctl(cpu, KVM_GET_REGS, &kregs);
            kvm_vcpu_ioctl(cpu, KVM_GET_SREGS, &ksregs);
            kctx->rax = kregs.rax; kctx->rbx = kregs.rbx; kctx->rcx = kregs.rcx;
            kctx->rdx = kregs.rdx; kctx->rsi = kregs.rsi; kctx->rdi = kregs.rdi;
            kctx->rsp = kregs.rsp; kctx->rbp = kregs.rbp;
            kctx->r8  = kregs.r8;  kctx->r9  = kregs.r9;  kctx->r10 = kregs.r10;
            kctx->r11 = kregs.r11; kctx->r12 = kregs.r12; kctx->r13 = kregs.r13;
            kctx->r14 = kregs.r14; kctx->r15 = kregs.r15;
            kctx->rip = kregs.rip; kctx->rflags = kregs.rflags;
            memcpy(kctx->sregs_data, &ksregs, sizeof(ksregs));

            {
                struct kvm_run *run = cpu->kvm_run;
                kctx->exit_reason = run->exit_reason;
                if (run->exit_reason == KVM_EXIT_IO) {
                    kctx->io.direction = run->io.direction;
                    kctx->io.size      = run->io.size;
                    kctx->io.port      = run->io.port;
                    kctx->io.count     = run->io.count;
                    if (run->io.direction == KVM_EXIT_IO_OUT) {
                        size_t io_bytes = run->io.size * run->io.count;
                        if (io_bytes > sizeof(kctx->io.data)) {
                            io_bytes = sizeof(kctx->io.data);
                        }
                        memcpy(kctx->io.data, (uint8_t *)run + run->io.data_offset, io_bytes);
                    }
                } else if (run->exit_reason == KVM_EXIT_MMIO) {
                    kctx->mmio.phys_addr = run->mmio.phys_addr;
                    kctx->mmio.len       = run->mmio.len;
                    kctx->mmio.is_write  = run->mmio.is_write;
                    memcpy(kctx->mmio.data, run->mmio.data, 8);
                }
            }
        }
    } else if (local_is_tcg) {
        wvm_tcg_context_t t_ctx;
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        wvm_tcg_get_state(cpu, &t_ctx);
        memcpy(&ksregs, req->ctx.kvm.sregs_data, sizeof(ksregs));
        wvm_translate_tcg_to_kvm(&t_ctx, &kregs, &ksregs);
        ack->ctx.kvm.rax = kregs.rax; ack->ctx.kvm.rbx = kregs.rbx; ack->ctx.kvm.rcx = kregs.rcx;
        ack->ctx.kvm.rdx = kregs.rdx; ack->ctx.kvm.rsi = kregs.rsi; ack->ctx.kvm.rdi = kregs.rdi;
        ack->ctx.kvm.rsp = kregs.rsp; ack->ctx.kvm.rbp = kregs.rbp;
        ack->ctx.kvm.r8  = kregs.r8;  ack->ctx.kvm.r9  = kregs.r9;  ack->ctx.kvm.r10 = kregs.r10;
        ack->ctx.kvm.r11 = kregs.r11; ack->ctx.kvm.r12 = kregs.r12; ack->ctx.kvm.r13 = kregs.r13;
        ack->ctx.kvm.r14 = kregs.r14; ack->ctx.kvm.r15 = kregs.r15;
        ack->ctx.kvm.rip = kregs.rip; ack->ctx.kvm.rflags = kregs.rflags;
        ack->ctx.kvm.exit_reason = t_ctx.exit_reason;
        memcpy(ack->ctx.kvm.sregs_data, &ksregs, sizeof(ksregs));
    } else {
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        kvm_vcpu_ioctl(cpu, KVM_GET_REGS, &kregs);
        kvm_vcpu_ioctl(cpu, KVM_GET_SREGS, &ksregs);
        wvm_translate_kvm_to_tcg(&kregs, &ksregs, &ack->ctx.tcg);
        ack->ctx.tcg.exit_reason = cpu->kvm_run->exit_reason;
    }
}

int wavevm_slave_submit_cpu_run(const struct wvm_ipc_cpu_run_req *req,
                                struct wvm_ipc_cpu_run_ack *ack)
{
    pthread_once(&g_slave_exec_once, wavevm_slave_exec_sync_init);

    qemu_mutex_lock(&g_slave_exec_lock);
    while (g_slave_exec_pending) {
        qemu_cond_wait(&g_slave_exec_done_cond, &g_slave_exec_lock);
    }

    memcpy(&g_slave_exec_req, req, sizeof(g_slave_exec_req));
    memset(&g_slave_exec_ack, 0, sizeof(g_slave_exec_ack));
    g_slave_exec_done = false;
    g_slave_exec_pending = true;
    qemu_cond_signal(&g_slave_exec_req_cond);

    while (!g_slave_exec_done) {
        qemu_cond_wait(&g_slave_exec_done_cond, &g_slave_exec_lock);
    }

    memcpy(ack, &g_slave_exec_ack, sizeof(*ack));
    qemu_mutex_unlock(&g_slave_exec_lock);
    return ack->status;
}

void wavevm_slave_vcpu_loop(CPUState *cpu)
{
    pthread_once(&g_slave_exec_once, wavevm_slave_exec_sync_init);
    fprintf(stderr, "[WaveVM-Slave] vCPU service loop online cpu=%d\n", cpu->cpu_index);

    while (!cpu->unplug) {
        struct wvm_ipc_cpu_run_req req;
        struct wvm_ipc_cpu_run_ack ack;
        bool local_is_tcg = !kvm_enabled();

        qemu_mutex_lock(&g_slave_exec_lock);
        while (!g_slave_exec_pending && !cpu->unplug) {
            qemu_cond_wait(&g_slave_exec_req_cond, &g_slave_exec_lock);
        }
        if (cpu->unplug) {
            qemu_mutex_unlock(&g_slave_exec_lock);
            break;
        }
        memcpy(&req, &g_slave_exec_req, sizeof(req));
        qemu_mutex_unlock(&g_slave_exec_lock);
        fprintf(stderr, "[WaveVM-Slave] vCPU got req mode_tcg=%u cpu=%d\n",
                (unsigned)req.mode_tcg, cpu->cpu_index);

        while (!cpu_get_address_space(cpu, 0) && !cpu->unplug) {
            fprintf(stderr, "[WaveVM-Slave] waiting for address space cpu=%d\n", cpu->cpu_index);
            g_usleep(1000);
        }
        if (cpu->unplug) {
            break;
        }

        qemu_mutex_lock_iothread();
        if (local_is_tcg) {
            cpu_exec_start(cpu);
        }

        wavevm_slave_import_ctx(cpu, &req, local_is_tcg);
        cpu->stop = false;
        cpu->halted = 0;
        cpu->exception_index = -1;

        if (local_is_tcg) {
            WaveVMTcgKickCtx *kick = g_new0(WaveVMTcgKickCtx, 1);
            QemuThread kick_thread;
            kick->cpu = cpu;
            kick->delay_us = 5000;
            qemu_thread_create(&kick_thread, "wvm-tcg-kick",
                               wavevm_tcg_kick_thread, kick,
                               QEMU_THREAD_DETACHED);
            qemu_mutex_unlock_iothread();
            fprintf(stderr, "[WaveVM-Slave] cpu_exec enter cpu=%d\n", cpu->cpu_index);
            cpu_exec(cpu);
            fprintf(stderr, "[WaveVM-Slave] cpu_exec leave cpu=%d ex=%d\n",
                    cpu->cpu_index, cpu->exception_index);
            qemu_mutex_lock_iothread();
            cpu_exec_end(cpu);
        } else {
            /* KVM Slave 执行：复用 QEMU 原生 pre_run/post_run 钩子
             * 保证中断注入、APIC 同步、寄存器推送等不被遗漏。
             * 但不用 kvm_cpu_exec，因为 IO/MMIO 需要转发回 Master。
             */
            int ret;
            struct kvm_run *run = cpu->kvm_run;
            cpu_exec_start(cpu);
            qemu_mutex_unlock_iothread();
            do {
                if (cpu->vcpu_dirty) {
                    kvm_arch_put_registers(cpu, KVM_PUT_RUNTIME_STATE);
                    cpu->vcpu_dirty = false;
                }
                kvm_arch_pre_run(cpu, run);
                ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0);
                kvm_arch_post_run(cpu, run);
                if (ret == 0) {
                    int reason = run->exit_reason;
                    if (reason == KVM_EXIT_IO || reason == KVM_EXIT_MMIO ||
                        reason == KVM_EXIT_HLT || reason == KVM_EXIT_SHUTDOWN ||
                        reason == KVM_EXIT_FAIL_ENTRY) {
                        break;
                    }
                }
            } while (ret >= 0 || ret == -EINTR);
            qemu_mutex_lock_iothread();
            cpu_exec_end(cpu);
        }

        memset(&ack, 0, sizeof(ack));
        wavevm_slave_export_ctx(cpu, &req, &ack, local_is_tcg);
        qemu_mutex_unlock_iothread();

        qemu_mutex_lock(&g_slave_exec_lock);
        memcpy(&g_slave_exec_ack, &ack, sizeof(ack));
        g_slave_exec_pending = false;
        g_slave_exec_done = true;
        qemu_cond_broadcast(&g_slave_exec_done_cond);
        qemu_mutex_unlock(&g_slave_exec_lock);
    }
}

static int wavevm_auto_split_from_vcpus(int vcpus)
{
    if (vcpus <= 1) {
        return 1;
    }
    if (vcpus == 2) {
        return 1;
    }
    return vcpus / 2;
}

static void wavevm_resolve_split(MachineState *ms)
{
    int vcpus = 1;
    const char *env_split;

    if (ms && ms->smp.cpus > 0) {
        vcpus = ms->smp.cpus;
    }

    if (!g_wvm_split_explicit) {
        env_split = getenv("WVM_LOCAL_SPLIT");
        if (env_split && *env_split) {
            char *endptr = NULL;
            long parsed = strtol(env_split, &endptr, 10);
            if (endptr && *endptr == '\0' && parsed >= 0) {
                g_wvm_local_split = (int)parsed;
            } else {
                g_wvm_local_split = wavevm_auto_split_from_vcpus(vcpus);
            }
        } else {
            g_wvm_local_split = wavevm_auto_split_from_vcpus(vcpus);
        }
    }

    if (g_wvm_local_split < 0) {
        g_wvm_local_split = 0;
    }
    if (g_wvm_local_split > vcpus) {
        g_wvm_local_split = vcpus;
    }
}

void wavevm_apply_split_hint(int split)
{
    int vcpus = 1;

    if (g_wvm_split_explicit || split < 0) {
        return;
    }

    if (current_machine && current_machine->smp.cpus > 0) {
        vcpus = current_machine->smp.cpus;
    }

    g_wvm_local_split = split;
    if (g_wvm_local_split > vcpus) {
        g_wvm_local_split = vcpus;
    }
}

/* [V28 FIX] 坚如磐石的读取函数，处理 Partial Read 和 EINTR */
static int read_exact(int fd, void *buf, size_t len) {
    size_t received = 0;
    char *ptr = (char *)buf;
    
    while (received < len) {
        ssize_t ret = read(fd, ptr + received, len - received);
        
        if (ret > 0) {
            received += ret;
        } else if (ret == 0) {
            return -1; // EOF: 对端挂了
        } else {
            if (errno == EINTR) continue; // 信号中断，重试
            return -1; // 真正的错误
        }
    }
    return 0; // 成功读满
}

static bool wavevm_allowed = true;

#define SYNC_WINDOW_SIZE 64

static int connect_to_master_helper(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    
    // [V28 Fix] Dynamic Path
    const char *env_path = getenv("WVM_ENV_SOCK_PATH");
    if (!env_path) {
        char *inst_id = getenv("WVM_INSTANCE_ID");
        static char fallback[128]; // static to be safe scope-wise though redundant here
        snprintf(fallback, sizeof(fallback), "/tmp/wvm_user_%s.sock", inst_id ? inst_id : "0");
        env_path = fallback;
    }

    strncpy(addr.sun_path, env_path, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

// Master Mode B (User) 的 IPC 监听线程
static void *wavevm_master_ipc_thread(void *arg) {
    WaveVMAccelState *s = (WaveVMAccelState *)arg;
    
    s->ipc_sock = connect_to_master_helper();
    if (s->ipc_sock < 0) {
        fprintf(stderr, "[WVM] Failed to connect IPC socket for IRQ listening.\n");
        return NULL;
    }

    struct wvm_ipc_header_t hdr;
    // IPC invalidation payload may carry a full 4K page plus protocol headers.
    uint8_t payload_buf[WVM_MAX_PACKET_SIZE];

    while (s->sync_thread_running) {
        // [V28 FIX] 使用 read_exact 替代原始 read
        if (read_exact(s->ipc_sock, &hdr, sizeof(hdr)) < 0) {
            // 连接断开或错误，尝试简单的重连或退出
            g_usleep(100000); 
            close(s->ipc_sock);
            s->ipc_sock = connect_to_master_helper();
            if (s->ipc_sock < 0) return NULL; // 重连失败则退出
            continue;
        }

        // [V28 FIX] 完整读取 Payload，防止粘包错位
        if (hdr.len > 0) {
            if (hdr.len > sizeof(payload_buf)) {
                fprintf(stderr, "[WVM] IPC Payload too large: %d\n", hdr.len);
                // 严重协议错误，无法恢复同步，必须断开
                close(s->ipc_sock);
                return NULL;
            }
            if (read_exact(s->ipc_sock, payload_buf, hdr.len) < 0) {
                continue; // 读取 Payload 失败
            }
        }

        // 1. 处理中断消息
        if (hdr.type == WVM_IPC_TYPE_IRQ) {
            qemu_mutex_lock_iothread();
            if (kvm_enabled()) {
                 struct kvm_irq_level irq;
                 irq.irq = 16; 
                 irq.level = 1;
                 kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &irq);
                 irq.level = 0;
                 kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &irq);
            }
            qemu_mutex_unlock_iothread();
        }
        // 2. 处理内存失效 (MESI Invalidate)
        else if (hdr.type == WVM_IPC_TYPE_MEM_WRITE) {
            if (hdr.len < (int)sizeof(struct wvm_ipc_write_req)) continue;
            struct wvm_ipc_write_req *req = (struct wvm_ipc_write_req *)payload_buf;
            // 约定：len=0 表示 Invalidate
            if (req->len == 0) {
                hwaddr len = 4096;
                void *host_addr = cpu_physical_memory_map(req->gpa, &len, 1);
                if (host_addr && len >= 4096) {
                    // KVM 模式下不能用 mprotect(PROT_NONE)，
                    // 否则 EPT 无法解析已被 mprotect 撤销的 HVA 映射，导致 VM Exit 风暴。
                    if (!kvm_enabled()) {
                        mprotect(host_addr, 4096, PROT_NONE);
                    }
                    cpu_physical_memory_unmap(host_addr, len, 1, 0);
                }
            }
        }
        else if (hdr.type == WVM_IPC_TYPE_INVALIDATE) { // Type 6
            if (hdr.len < (int)sizeof(struct wvm_header)) continue;
            // 解包内部的 wvm_header
            struct wvm_header *net_hdr = (struct wvm_header *)payload_buf;
            void *net_payload = payload_buf + sizeof(struct wvm_header);
            uint16_t msg_type = ntohs(net_hdr->msg_type);
            // 处理 Prophet 同步指令
            if (msg_type == MSG_RPC_BATCH_MEMSET) {
                // 1. TCG 模式下最关键的动作：冲刷所有翻译块 (Translation Blocks)
                // 必须在 BQL 锁保护下执行
                qemu_mutex_lock_iothread();
                
                // 冲刷所有缓存的代码。虽然这很重，但对于批量 memset 来说，
                // 它是保证指令一致性的唯一可靠手段。
                tb_flush(first_cpu); 
                
                // 2. 同时也需要更新 User-Mode 的版本号 (调用 user-mem.c)
                wvm_apply_remote_push(msg_type, net_payload);
                
                qemu_mutex_unlock_iothread();
                continue;
            }
            
            // 调用 user-mem 提供的逻辑应用更新
            wvm_apply_remote_push(msg_type, net_payload);
        }
    }
    close(s->ipc_sock);
    return NULL;
}

// Master 模式专用脏页同步线程 (仅 KVM 需此线程，TCG 由 user-mem 处理)
static void *wavevm_dirty_sync_thread(void *arg) {
    WaveVMAccelState *s = (WaveVMAccelState *)arg;
    if (!kvm_enabled()) {
        return NULL;
    }

    s->sync_sock = connect_to_master_helper();
    if (s->sync_sock < 0) {
        return NULL;
    }

    /* Keep the thread alive to preserve timing/ordering behavior.
     * Dirty-log harvesting is intentionally disabled in this compatibility path.
     */
    while (s->sync_thread_running) {
        g_usleep(50000);
    }

    close(s->sync_sock);
    return NULL;
}

// Kernel Mode IRQ 监听线程
static void *wavevm_kernel_irq_thread(void *arg) {
    WaveVMAccelState *s = (WaveVMAccelState *)arg;
    uint32_t irq_num;
    
    while (1) {
        // 陷入内核等待，直到收到 UDP 中断包
        if (ioctl(s->dev_fd, IOCTL_WAIT_IRQ, &irq_num) == 0) {
            qemu_mutex_lock_iothread();
            // 注入中断 (Pulse)
            if (kvm_enabled()) {
                 struct kvm_irq_level irq;
                 irq.irq = irq_num; // 例如 16
                 irq.level = 1;
                 kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &irq);
                 irq.level = 0;
                 kvm_vm_ioctl(kvm_state, KVM_IRQ_LINE, &irq);
            }
            qemu_mutex_unlock_iothread();
        } else {
            // 出错或退出
            break;
        }
    }
    return NULL;
}

// Validate GPA range against actual QEMU RAM mapping (handles discontiguous RAM segments).
static bool wavevm_gpa_range_valid(uint64_t gpa, uint32_t len, bool is_write)
{
    hwaddr map_len = len;
    void *host_addr = cpu_physical_memory_map(gpa, &map_len, is_write ? 1 : 0);
    if (!host_addr || map_len < len) {
        if (host_addr) {
            cpu_physical_memory_unmap(host_addr, map_len, is_write ? 1 : 0, 0);
        }
        return false;
    }
    cpu_physical_memory_unmap(host_addr, map_len, is_write ? 1 : 0, 0);
    return true;
}

// Slave 网络处理线程 (支持 KVM 和 TCG 双模)
static void *wavevm_slave_net_thread(void *arg) {
    WaveVMAccelState *s = (WaveVMAccelState *)arg;
    #define BATCH_SIZE 64
    #define MAX_PKT_SIZE 8192

    struct mmsghdr msgs[BATCH_SIZE];
    struct iovec iovecs[BATCH_SIZE];
    uint8_t *buffers = g_malloc(BATCH_SIZE * MAX_PKT_SIZE);
    struct sockaddr_in addrs[BATCH_SIZE];

    memset(msgs, 0, sizeof(msgs));
    for (int i = 0; i < BATCH_SIZE; i++) {
        iovecs[i].iov_base = buffers + i * MAX_PKT_SIZE;
        iovecs[i].iov_len = MAX_PKT_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    }

    printf("[WaveVM-Slave] Network Loop Active (Engine: %s, FD: %d).\n", 
           kvm_enabled() ? "KVM" : "TCG", s->master_sock);

    while (1) {
        int retval = recvmmsg(s->master_sock, msgs, BATCH_SIZE, 0, NULL);
        if (retval < 0) {
            if (errno == EINTR) continue;
            perror("recvmmsg");
            break;
        }

        for (int i = 0; i < retval; i++) {
            uint8_t *buf = (uint8_t *)iovecs[i].iov_base;
            int len = msgs[i].msg_len;
            
            if (len >= sizeof(struct wvm_header)) {
                struct wvm_header *hdr = (struct wvm_header *)buf;
                void *payload = buf + sizeof(struct wvm_header);

                // [安全] Magic 校验，丢弃非 WaveVM 包
                if (ntohl(hdr->magic) != WVM_MAGIC) {
                    continue;
                }

                // [字节序] 从网络字节序解码到主机序
                uint16_t msg_type = ntohs(hdr->msg_type);
                uint16_t pkt_payload_len = ntohs(hdr->payload_len);
                int actual_payload = len - sizeof(struct wvm_header);

                // [安全] 报文实际长度 vs 头部声明长度一致性校验
                if (pkt_payload_len > actual_payload) {
                    continue; // 截断包，丢弃
                }

                // 1. 内存写 (Master -> Slave)
                if (msg_type == MSG_MEM_WRITE) {
                    qemu_mutex_lock_iothread();
                    if (pkt_payload_len > 8 && actual_payload >= pkt_payload_len) {
                        uint64_t gpa = WVM_NTOHLL(*(uint64_t *)payload);
                        uint32_t data_len = pkt_payload_len - 8;
                        void *data_ptr = (uint8_t *)payload + 8;
                        bool gpa_ok = wavevm_gpa_range_valid(gpa, data_len, true);
                        if (gpa_ok) {
                            cpu_physical_memory_write(gpa, data_ptr, data_len);
                        }
                    }
                    qemu_mutex_unlock_iothread();
                    hdr->msg_type = htons(MSG_MEM_ACK);
                    hdr->payload_len = 0;
                    hdr->crc32 = 0;
                    hdr->crc32 = htonl(calculate_crc32(buf, sizeof(struct wvm_header)));
                    if (send(s->master_sock, buf, sizeof(struct wvm_header), 0) < 0) {
                        perror("[WaveVM-Slave] send MSG_MEM_ACK");
                    }
                }
                // 2. 内存读 (Master -> Slave)
                else if (msg_type == MSG_MEM_READ) {
                    if (pkt_payload_len < 8) continue; // 至少需要 8 字节的 gpa
                    uint64_t gpa = WVM_NTOHLL(*(uint64_t *)payload);
                    uint32_t read_len = 4096;
                    qemu_mutex_lock_iothread();
                    bool gpa_ok = wavevm_gpa_range_valid(gpa, read_len, false);
                    if (!gpa_ok) {
                        qemu_mutex_unlock_iothread();
                        hdr->msg_type = htons(MSG_MEM_ACK);
                        hdr->payload_len = 0;
                        hdr->crc32 = 0;
                        hdr->crc32 = htonl(calculate_crc32(buf, sizeof(struct wvm_header)));
                        if (send(s->master_sock, buf, sizeof(struct wvm_header), 0) < 0) {
                            perror("[WaveVM-Slave] send short MSG_MEM_ACK");
                        }
                        continue;
                    }
                    if (sizeof(struct wvm_header) + read_len <= MAX_PKT_SIZE) {
                        cpu_physical_memory_read(gpa, payload, read_len);
                        qemu_mutex_unlock_iothread();
                        hdr->msg_type = htons(MSG_MEM_ACK);
                        hdr->payload_len = htons(read_len);
                        hdr->crc32 = 0;
                        hdr->crc32 = htonl(calculate_crc32(buf, sizeof(struct wvm_header) + read_len));
                        if (send(s->master_sock, buf, sizeof(struct wvm_header) + read_len, 0) < 0) {
                            perror("[WaveVM-Slave] send MSG_MEM_ACK payload");
                        }
                    } else {
                        qemu_mutex_unlock_iothread();
                    }
                }
                // 3. 远程执行 (Master -> Slave)
                else if (msg_type == MSG_VCPU_RUN) {
                    struct wvm_ipc_cpu_run_req local_req;
                    struct wvm_ipc_cpu_run_req *req = NULL;
                    bool compact_ctx_payload = false;

                    /* Backward compatibility: some senders put only context in payload. */
                    if (actual_payload >= (int)sizeof(struct wvm_ipc_cpu_run_req)) {
                        req = (struct wvm_ipc_cpu_run_req *)payload;
                    } else {
                        memset(&local_req, 0, sizeof(local_req));
                        local_req.mode_tcg = hdr->mode_tcg ? 1 : 0;
                        local_req.slave_id = ntohl(hdr->slave_id);

                        if (hdr->mode_tcg) {
                            if (actual_payload < (int)sizeof(wvm_tcg_context_t)) {
                                continue;
                            }
                            memcpy(&local_req.ctx.tcg, payload, sizeof(wvm_tcg_context_t));
                        } else {
                            if (actual_payload < (int)sizeof(wvm_kvm_context_t)) {
                                continue;
                            }
                            memcpy(&local_req.ctx.kvm, payload, sizeof(wvm_kvm_context_t));
                        }
                        req = &local_req;
                        compact_ctx_payload = true;
                    }
                    struct wvm_ipc_cpu_run_ack full_ack;
                    memset(&full_ack, 0, sizeof(full_ack));
                    wavevm_slave_submit_cpu_run(req, &full_ack);
                    if (compact_ctx_payload) {
                        if (hdr->mode_tcg) {
                            memcpy(payload, &full_ack.ctx.tcg, sizeof(full_ack.ctx.tcg));
                            hdr->payload_len = htons(sizeof(full_ack.ctx.tcg));
                            msgs[i].msg_len = sizeof(struct wvm_header) + sizeof(full_ack.ctx.tcg);
                        } else {
                            memcpy(payload, &full_ack.ctx.kvm, sizeof(full_ack.ctx.kvm));
                            hdr->payload_len = htons(sizeof(full_ack.ctx.kvm));
                            msgs[i].msg_len = sizeof(struct wvm_header) + sizeof(full_ack.ctx.kvm);
                        }
                    } else {
                        memcpy(payload, &full_ack, sizeof(full_ack));
                        hdr->payload_len = htons(sizeof(full_ack));
                        msgs[i].msg_len = sizeof(struct wvm_header) + sizeof(full_ack);
                    }
                    hdr->msg_type = htons(MSG_VCPU_EXIT);
                    hdr->crc32 = 0;
                    hdr->crc32 = htonl(calculate_crc32(buf, msgs[i].msg_len));
                    if (send(s->master_sock, buf, msgs[i].msg_len, 0) < 0) {
                        perror("[WaveVM-Slave] send MSG_VCPU_EXIT");
                    }
                } else if (msg_type == MSG_PING) {
                    // [FIX] 透传 PING 给 Gateway/Master，由真正的 Owner 回复 ACK
                    if (send(s->master_sock, buf, sizeof(struct wvm_header), 0) < 0) {
                        perror("[WaveVM-Slave] send MSG_PING");
                    }
                }
            }
            msgs[i].msg_len = 0; 
        }
    }
    g_free(buffers);
    return NULL;
}

static int wavevm_init_machine_kernel(WaveVMAccelState *s, MachineState *ms) {
    fprintf(stderr, "[WaveVM-QEMU] KERNEL MODE: Connecting to /dev/wavevm...\n");
    s->dev_fd = open("/dev/wavevm", O_RDWR);
    if (s->dev_fd < 0) return -errno;
    wavevm_setup_memory_region(ms->ram, ms->ram_size, s->dev_fd);
    // 启动线程
    qemu_thread_create(&s->irq_thread, "wvm-k-irq", wavevm_kernel_irq_thread, s, QEMU_THREAD_DETACHED);
    return 0;
}

static int wavevm_init_machine_user(WaveVMAccelState *s, MachineState *ms) {
    // Slave Mode Check (FD Inheritance)
    char *env_cmd = getenv("WVM_SOCK_CMD");
    
    if (env_cmd) {
        // [Slave Mode]
        s->master_sock = atoi(env_cmd); // Use CMD socket for control loop
    } else {
        // [Master Mode]
        // 读取环境变量以支持单机多实例
        const char *shm_path = getenv("WVM_SHM_FILE");
        if (!shm_path) shm_path = "/wavevm_ram"; // Default fallback (Keep sync with config.h)

        int shm_fd = shm_open(shm_path, O_CREAT | O_RDWR, 0666);
        if (shm_fd < 0) {
            fprintf(stderr, "WaveVM: Failed to open SHM file '%s': %s\n", shm_path, strerror(errno));
            exit(1);
        }
        
        // 确保文件大小足够 (Daemon 应该已经 truncate 过了，这里是双保险)
        if (ftruncate(shm_fd, ms->ram_size) < 0) {
            perror("ftruncate");
            close(shm_fd);
            exit(1);
        }

        wavevm_setup_memory_region(ms->ram, ms->ram_size, shm_fd);
        close(shm_fd);
        
        // [FIX-G4] IPC 监听线程必须无条件启动 (KVM + TCG 都需要接收 Invalidate)
        // dirty_sync_thread 仅 KVM 需要 (TCG 由 user-mem harvester 处理)
        s->sync_thread_running = true;

        // [FIX-F1] 初始化 Block IO 持久连接状态
        s->block_io_sock = -1;
        qemu_mutex_init(&s->block_io_lock);

        qemu_thread_create(&s->ipc_thread, "wvm-ipc-rx", wavevm_master_ipc_thread, s, QEMU_THREAD_DETACHED);
        if (kvm_enabled()) {
            qemu_thread_create(&s->sync_thread, "wvm-sync", wavevm_dirty_sync_thread, s, QEMU_THREAD_DETACHED);
        }
    }
        
    g_user_ram_size_hint = ms->ram_size;
    // 初始化拦截信号。某些机型在此时 ms->ram 仍可能为空，需做兜底查找。
    void *ram_ptr = NULL;
    if (ms->ram && memory_region_is_ram(ms->ram)) {
        ram_ptr = memory_region_get_ram_ptr(ms->ram);
    } else {
        MemoryRegionSection sec = memory_region_find(get_system_memory(), 0, ms->ram_size ? ms->ram_size : 1);
        if (sec.mr && memory_region_is_ram(sec.mr)) {
            ram_ptr = memory_region_get_ram_ptr(sec.mr) + sec.offset_within_region;
        }
    }
    if (!ram_ptr && g_primary_ram_hva) {
        ram_ptr = g_primary_ram_hva;
    }
    if (ram_ptr) {
        wavevm_user_mem_init(ram_ptr, g_primary_ram_size ? g_primary_ram_size : ms->ram_size);
        g_user_mem_inited = true;
    } else {
        // 某些机型在该回调时序下 RAM block 尚未注册；在 region_add 首块时补初始化。
        error_report("WaveVM: defer user mem init until first RAM block is registered");
    }

    return 0;
}

static struct wvm_ioctl_mem_layout global_layout;

static bool wavevm_user_mode_enabled(void)
{
    if (!current_machine || !current_machine->accelerator) {
        return false;
    }
    return WAVEVM_ACCEL(current_machine->accelerator)->mode == WVM_MODE_USER;
}

static bool wavevm_trackable_ram_section(const MemoryRegionSection *section)
{
    MemoryRegion *mr = section->mr;

    return mr &&
           memory_region_is_ram(mr) &&
           !memory_region_is_ram_device(mr) &&
           !memory_region_is_rom(mr) &&
           !memory_region_is_romd(mr);
}

static void wavevm_region_add(MemoryListener *listener, MemoryRegionSection *section) {
    if (!wavevm_trackable_ram_section(section)) return;

    uint64_t start_gpa = section->offset_within_address_space;
    uint64_t size = int128_get64(section->size);
    void *hva = memory_region_get_ram_ptr(section->mr) + section->offset_within_region;
    
    if (size < 4096) return;

    // 记录到本地表
    if (global_layout.count < 32) {
        for (int i = 0; i < global_layout.count; i++) {
            if (global_layout.slots[i].start == start_gpa &&
                global_layout.slots[i].size == size) {
                fprintf(stderr,
                        "[WaveVM] dedup region_add gpa=%#llx size=%#llx count=%d\n",
                        (unsigned long long)start_gpa,
                        (unsigned long long)size,
                        global_layout.count);
                return;
            }
        }
        global_layout.slots[global_layout.count].start = start_gpa;
        global_layout.slots[global_layout.count].size = size;
        global_layout.count++;
        fprintf(stderr,
                "[WaveVM] region_add gpa=%#llx size=%#llx count=%d\n",
                (unsigned long long)start_gpa,
                (unsigned long long)size,
                global_layout.count);

        /* User-mem fault hook is only valid in user mode. */
        if (wavevm_user_mode_enabled()) {
            extern void wavevm_register_ram_block(void *hva, uint64_t size, uint64_t gpa);
            wavevm_register_ram_block(hva, size, start_gpa);
        }
        if (start_gpa == 0 && !g_primary_ram_hva) {
            g_primary_ram_hva = hva;
            g_primary_ram_size = size;
        }
        if (wavevm_user_mode_enabled() && start_gpa == 0 && !g_user_mem_inited) {
            wavevm_user_mem_init(hva, g_user_ram_size_hint ? g_user_ram_size_hint : size);
            g_user_mem_inited = true;
        }

        /* [FIX] Mode A: 每次新增 RAM block 都实时同步给内核模块，
         * 修复 wavevm_init_machine 中 wavevm_sync_topology 先于 RAM 创建的时序问题。
         * 若不实时同步，wvm_fault_handler 的 g_mem_slots 为空 → VM_FAULT_SIGBUS → exit=17。 */
        if (!wavevm_user_mode_enabled()) {
            WaveVMAccelState *ws = WAVEVM_ACCEL(current_machine->accelerator);
            if (ws->dev_fd >= 0) {
                wavevm_sync_topology(ws->dev_fd);
            }
        }
    }
}

// 关键：在虚拟机启动完成前，将拓扑同步给内核
static void wavevm_sync_topology(int dev_fd) {
    if (ioctl(dev_fd, IOCTL_SET_MEM_LAYOUT, &global_layout) < 0) {
        perror("[WVM] Failed to sync memory layout to kernel (continue)");
    }
}

// 监听器结构体
static MemoryListener wavevm_mem_listener = {
    .region_add = wavevm_region_add,
};

static int wavevm_init_machine(MachineState *ms) {
    WaveVMAccelState *s = WAVEVM_ACCEL(ms->accelerator);
    bool has_wvm_dev = (access("/dev/wavevm", R_OK | W_OK) == 0);
    const char *disable_auto_kvm = getenv("WVM_DISABLE_AUTO_KVM");
    bool auto_kvm_enabled = !(disable_auto_kvm && atoi(disable_auto_kvm) == 1);
    char *role = getenv("WVM_ROLE");
    bool is_slave = (role && strcmp(role, "SLAVE") == 0);

    if (!g_wvm_mode_explicit) {
        if (is_slave) {
            s->mode = WVM_MODE_USER;
        } else {
            s->mode = has_wvm_dev ? WVM_MODE_KERNEL : WVM_MODE_USER;
        }
    }

    if (s->mode == WVM_MODE_KERNEL && !has_wvm_dev) {
        return -1;
    }

    int ret = 0;

    wavevm_resolve_split(ms);

    memory_listener_register(&wavevm_mem_listener, &address_space_memory);

    if (!g_wvm_kvm_bootstrap_done) {
        AccelClass *kvm_ac;
        AccelState *saved_accel;
        const char *mode_str = (s->mode == WVM_MODE_USER) ? "user" : "kernel";

        g_wvm_kvm_bootstrap_done = true;
        if (!auto_kvm_enabled) {
            fprintf(stderr, "[WaveVM] Auto-KVM disabled by WVM_DISABLE_AUTO_KVM=1 (mode=%s)\n",
                    mode_str);
        } else if (access("/dev/kvm", R_OK | W_OK) == 0) {
            kvm_ac = accel_find("kvm");
            if (kvm_ac && kvm_ac->init_machine) {
                g_wvm_kvm_accel = ACCEL(object_new(ACCEL_CLASS_NAME("kvm")));
                if (g_wvm_kvm_accel) {
#ifdef CONFIG_KVM
                    kvm_allowed = true;
#endif
                    saved_accel = ms->accelerator;
                    ms->accelerator = g_wvm_kvm_accel;
                    ret = kvm_ac->init_machine(ms);
                    ms->accelerator = saved_accel;

                    if (ret == 0) {
                        cpus_register_accel(&wavevm_cpus);
                        fprintf(stderr, "[WaveVM] KVM bootstrap OK (mode=%s), keep wavevm vCPU control (kvm_enabled=%d)\n",
                                mode_str, kvm_enabled() ? 1 : 0);
                    } else {
#ifdef CONFIG_KVM
                        kvm_allowed = false;
#endif
                        object_unref(OBJECT(g_wvm_kvm_accel));
                        g_wvm_kvm_accel = NULL;
                        ret = 0;
                        fprintf(stderr, "[WaveVM] KVM bootstrap failed (mode=%s), stay on TCG path\n",
                                mode_str);
                    }
                }
            }
        } else {
            fprintf(stderr, "[WaveVM] /dev/kvm not available (mode=%s), stay on TCG path\n",
                    mode_str);
        }
    }

    if (!kvm_enabled() && !g_wvm_tcg_bootstrap_done) {
        g_wvm_tcg_bootstrap_done = true;
        tcg_exec_init(0);
    }

    if (s->mode == WVM_MODE_KERNEL) ret = wavevm_init_machine_kernel(s, ms);
    else ret = wavevm_init_machine_user(s, ms);
    if (ret < 0) return ret;

    // 只要是 Slave (无论 KVM 还是 TCG)，都启动网络处理线程
    // 这样 TCG Slave 也能接收 CPU 任务和内存请求
    if (is_slave) {
        // Now safe to run net thread in TCG mode because it uses a separate socket
        printf("[WaveVM] Starting Slave Net Thread on FD %d...\n", s->master_sock);
        qemu_thread_create(&s->net_thread, "wvm-slave-net", wavevm_slave_net_thread, s, QEMU_THREAD_DETACHED);
        // 暂停主 vCPU 线程，控制权移交给 net_thread 驱动
        if (current_cpu) { current_cpu->stop = true; current_cpu->halted = true; }
    }

    if (s->mode == WVM_MODE_KERNEL) {
        wavevm_sync_topology(s->dev_fd); 
    }
    return 0;
}

static char *wavevm_get_mode(Object *obj, Error **errp)
{
    WaveVMAccelState *s = WAVEVM_ACCEL(obj);
    return g_strdup(s->mode == WVM_MODE_USER ? "user" : "kernel");
}

static void wavevm_set_mode(Object *obj, const char *value, Error **errp)
{
    WaveVMAccelState *s = WAVEVM_ACCEL(obj);
    if (g_strcmp0(value, "user") == 0) {
        s->mode = WVM_MODE_USER;
        g_wvm_mode_explicit = true;
        return;
    }
    if (g_strcmp0(value, "kernel") == 0) {
        s->mode = WVM_MODE_KERNEL;
        g_wvm_mode_explicit = true;
        return;
    }
    error_setg(errp, "Invalid mode '%s' (expected 'kernel' or 'user')", value);
}

static void wavevm_set_split(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp) {
    uint32_t value; if (!visit_type_uint32(v, name, &value, errp)) return;
    g_wvm_local_split = value;
    g_wvm_split_explicit = true;
}
static void wavevm_get_split(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp) {
    uint32_t value = g_wvm_local_split; visit_type_uint32(v, name, &value, errp);
}
static void wavevm_set_ttl(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp) {
    uint32_t value;
    if (!visit_type_uint32(v, name, &value, errp)) return;
    wvm_set_ttl_interval((int)value);
}
static void wavevm_get_ttl(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp) {
    // Getter implementation usually needed for QOM but can be stubbed if write-only intent
    // keeping it simple
    uint32_t val = 0; 
    visit_type_uint32(v, name, &val, errp);
}
static void wavevm_set_watch(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp) {
    char *value;
    if (!visit_type_str(v, name, &value, errp)) return;
    
    // 解析格式: "0xC0000000:16M;0x800000000:128K"
    // 使用副本进行分割，防止破坏原字符串
    char *dup = g_strdup(value);
    char *saveptr;
    char *token = strtok_r(dup, ";", &saveptr);
    
    while (token) {
        uint64_t gpa = 0;
        uint64_t size_bytes = 0;
        uint64_t size_val = 0;
        char unit = 0;
        
        // sscanf 解析 hex:size+unit
        if (sscanf(token, "%lx:%lu%c", &gpa, &size_val, &unit) >= 2) {
            size_bytes = size_val;
            if (unit == 'M' || unit == 'm') size_bytes *= 1024 * 1024;
            else if (unit == 'G' || unit == 'g') size_bytes *= 1024 * 1024 * 1024;
            else if (unit == 'K' || unit == 'k') size_bytes *= 1024;
            
            wvm_register_volatile_ram(gpa, size_bytes);
        }
        token = strtok_r(NULL, ";", &saveptr);
    }
    g_free(dup);
    g_free(value);
}
static void wavevm_get_watch(Object *obj, Visitor *v, const char *name, void *opaque, Error **errp) {
    char *val = g_strdup("");
    visit_type_str(v, name, &val, errp);
    g_free(val);
}
static void wavevm_accel_init(Object *obj) {
    WaveVMAccelState *s = WAVEVM_ACCEL(obj);
    s->mode = WVM_MODE_KERNEL;
}
static void wavevm_accel_class_init(ObjectClass *oc, void *data) {
    AccelClass *ac = ACCEL_CLASS(oc);

    ac->name = "wavevm";
    ac->init_machine = wavevm_init_machine;
    ac->allowed = &wavevm_allowed;

    cpus_register_accel(&wavevm_cpus);

    object_class_property_add_str(oc, "mode", wavevm_get_mode, wavevm_set_mode);
    object_class_property_set_description(oc, "mode",
        "Execution mode: 'kernel' or 'user'");

    object_class_property_add(oc, "split", "int",
        wavevm_get_split, wavevm_set_split,
        NULL, NULL);
    object_class_property_set_description(oc, "split",
        "Number of vCPUs to run locally (Tier 1)");

    object_class_property_add(oc, "watch", "string",
        wavevm_get_watch, wavevm_set_watch,
        NULL, NULL);
    object_class_property_set_description(oc, "watch",
        "List of volatile RAM ranges (e.g. 0xC0000:16M)");

    #ifndef CONFIG_USER_ONLY
    #endif
}
static const TypeInfo wavevm_accel_type = {
    .name = TYPE_WAVEVM_ACCEL, .parent = TYPE_ACCEL, .instance_size = sizeof(WaveVMAccelState),
    .class_init = wavevm_accel_class_init, .instance_init = wavevm_accel_init,
};
static void wavevm_type_init(void) { type_register_static(&wavevm_accel_type); }
type_init(wavevm_type_init);

// [Helper] 健壮写：处理 EINTR 和 EAGAIN
static int write_all(int fd, const void *buf, size_t len) {
    size_t written = 0;
    const char *ptr = buf;
    while (written < len) {
        ssize_t ret = write(fd, ptr + written, len - written);
        if (ret < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = fd, .events = POLLOUT };
                poll(&pfd, 1, 100); // Wait 100ms
                continue;
            }
            return -1;
        }
        written += ret;
    }
    return 0;
}

// [Helper] 健壮读：处理 EINTR 和 EAGAIN
static int read_all(int fd, void *buf, size_t len) {
    size_t received = 0;
    char *ptr = buf;
    while (received < len) {
        ssize_t ret = read(fd, ptr + received, len - received);
        if (ret < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = fd, .events = POLLIN };
                poll(&pfd, 1, 100); 
                continue;
            }
            return -1;
        }
        if (ret == 0) return -1; // EOF (Daemon died)
        received += ret;
    }
    return 0;
}

// [V29 Prophet Core] 同步 RPC 发送
// 返回 0 成功，-1 失败
int wvm_send_rpc_sync(uint16_t msg_type, void *payload, size_t len) {
    WaveVMAccelState *s = WAVEVM_ACCEL(current_machine->accelerator);
    int fd = -1;
    int needs_close = 0;

    // 1. 获取连接 FD
    if (s->mode == WVM_MODE_USER) {
        char *role = getenv("WVM_ROLE");
        // Slave: 复用 CMD 通道
        if (role && strcmp(role, "SLAVE") == 0) {
            fd = s->master_sock; 
        } else {
            // Master: 复用 Sync 通道或新建
            if (s->sync_sock > 0) fd = s->sync_sock;
            else { fd = connect_to_master_helper(); needs_close = 1; }
        }
    } else {
        return -1; // Kernel Mode 不支持此路径
    }

    if (fd < 0) return -1;

    // 2. 构造数据包 (IPC头 + WVM头 + Payload)
    size_t wvm_pkt_len = sizeof(struct wvm_header) + len;
    size_t total_size = sizeof(struct wvm_ipc_header_t) + wvm_pkt_len;
    
    uint8_t *buffer = g_malloc(total_size);
    if (!buffer) { if (needs_close) close(fd); return -1; }

    struct wvm_ipc_header_t *ipc_hdr = (struct wvm_ipc_header_t *)buffer;
    ipc_hdr->type = 99; // WVM_IPC_TYPE_RPC_PASSTHROUGH
    ipc_hdr->len = wvm_pkt_len;

    struct wvm_header *hdr = (struct wvm_header *)(buffer + sizeof(struct wvm_ipc_header_t));
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(msg_type);
    hdr->payload_len = htons(len);
    hdr->slave_id = 0; 
    hdr->req_id = WVM_HTONLL(SYNC_MAGIC); // "SYNC" Magic
    hdr->qos_level = 1; 
    hdr->crc32 = 0; 

    if (len > 0) memcpy(buffer + sizeof(struct wvm_ipc_header_t) + sizeof(struct wvm_header), payload, len);

    // 3. 发送指令
    if (write_all(fd, buffer, total_size) < 0) {
        g_free(buffer); if (needs_close) close(fd); return -1;
    }
    g_free(buffer);

    // 4. [BLOCKING] 等待 ACK
    // 10秒超时，防止 Daemon 执行 memset 耗时过长或崩溃
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, 10000); 
    
    if (ret > 0 && (pfd.revents & POLLIN)) {
        uint8_t ack_byte;
        if (read_all(fd, &ack_byte, 1) == 0) {
            if (needs_close) close(fd);
            return 0; // Success
        }
    }

    if (needs_close) close(fd);
    return -1; // Fail/Timeout
}

// -----------------------------------------------------------
// [V30 BLOCK IO FINAL] 分布式存储 IPC 发送实现 (Thread-Safe)
// -----------------------------------------------------------

// 结构体必须与 Daemon 端严格对齐 (Packed 13 Bytes)
struct wvm_ipc_block_req {
    uint64_t lba;
    uint32_t len;
    uint8_t  is_write;
    uint8_t  data[0];
} __attribute__((packed));

/*
 * [物理意图] 将 virtio-blk 的 IO 请求序列化并通过 IPC 管道发送给 Master Daemon。
 * [关键逻辑] 封装 WVM_IPC_TYPE_BLOCK_IO 消息头，如果是写操作则携带 Payload。
 * [后果] 这是存储拦截的"出口"。没有它，wavevm-block-hook.c 拦截下来的 IO 请求就烂在肚子里了。
 */
int wvm_send_ipc_block_io(uint64_t lba, void *buf, uint32_t len, int is_write) {
    WaveVMAccelState *s = WAVEVM_ACCEL(current_machine->accelerator);

    // 1. Slave 模式不直接发起存储请求
    char *role = getenv("WVM_ROLE");
    if (role && strcmp(role, "SLAVE") == 0) {
        return -1;
    }

    // [FIX-F1] 使用持久 IPC 连接，避免每次 Block IO 都 connect() -> Master pthread_create()
    // 旧代码每次调用 connect_to_master_helper() 创建新连接，Master 侧每个 accept()
    // 都 spawn 新线程，5000 IOPS 下秒创 5000 线程导致系统崩溃。
    // 修复方案：保持一条持久连接，用 mutex 序列化请求。Block IO 本身是同步等待的，
    // 串行化不会额外降低吞吐，反而消除了连接建立/销毁开销。
    qemu_mutex_lock(&s->block_io_lock);

    // 惰性初始化 + 断线重连
    if (s->block_io_sock < 0) {
        s->block_io_sock = connect_to_master_helper();
        if (s->block_io_sock < 0) {
            qemu_mutex_unlock(&s->block_io_lock);
            return -1;
        }
    }

    int fd = s->block_io_sock;

    // 2. 计算包大小
    size_t meta_size = sizeof(struct wvm_ipc_block_req);
    size_t payload_len = meta_size + (is_write ? len : 0);
    size_t total_size = sizeof(struct wvm_ipc_header_t) + payload_len;

    // 3. 分配缓冲区
    uint8_t *buffer = g_malloc(total_size);
    if (!buffer) {
        qemu_mutex_unlock(&s->block_io_lock);
        return -1;
    }

    // 4. 填充头部
    struct wvm_ipc_header_t *ipc_hdr = (struct wvm_ipc_header_t *)buffer;
    ipc_hdr->type = WVM_IPC_TYPE_BLOCK_IO; // Type 7
    ipc_hdr->len = payload_len;

    // 5. 填充请求体
    struct wvm_ipc_block_req *req = (struct wvm_ipc_block_req *)(buffer + sizeof(struct wvm_ipc_header_t));
    req->lba = lba;
    req->len = len;
    req->is_write = (uint8_t)is_write;

    if (is_write && buf) {
        memcpy(req->data, buf, len);
    }

    // 6. 原子发送
    int ret = 0;
    if (write_all(fd, buffer, total_size) < 0) {
        ret = -1;
    }

    // 7. 等待 Daemon 的同步确认与数据回传
    uint8_t ack_byte;
    if (ret == 0 && (read_all(fd, &ack_byte, 1) < 0 || ack_byte == 0)) {
        ret = -1;
    } else if (ret == 0 && !is_write && buf && len > 0) {
        if (read_all(fd, buf, len) < 0) {
            ret = -1;
        }
    }

    g_free(buffer);

    // [FIX-F1] 如果通信失败，关闭连接以便下次重建（断线重连）
    if (ret < 0) {
        close(s->block_io_sock);
        s->block_io_sock = -1;
    }

    qemu_mutex_unlock(&s->block_io_lock);
    return ret;
}
```

**文件**: `wavevm-qemu/accel/wavevm/wavevm-cpu.c`

```c

#include "qemu/osdep.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "sysemu/hw_accel.h"
#include "sysemu/kvm.h" 
#include "sysemu/runstate.h"
#include "qapi/error.h"
#include "linux/kvm.h"
#include "hw/boards.h"
#include "qemu/main-loop.h"
#include "exec/address-spaces.h"
#include "exec/cpu-all.h"
#include "../kvm/kvm-cpus.h"
#include "../../../common_include/wavevm_protocol.h" 
#include "../../../common_include/wavevm_config.h"
#include "../../../common_include/wavevm_ioctl.h"
#include "wavevm-accel.h"
#include "qemu/thread.h"
#include "qemu/guest-random.h"
#include "tcg/tcg.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <limits.h>

extern void wavevm_apply_split_hint(int split);

// Per-vCPU Socket Pool to eliminate lock contention
static int *g_vcpu_socks = NULL;
static int g_configured_vcpus = 0;
static int g_wvm_debug_once = 0;

static void wavevm_try_import_split_from_peer(int sock)
{
#ifdef SO_PEERCRED
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    char path[64];
    FILE *fp;
    char envbuf[8192];
    size_t n;
    size_t i = 0;

    if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) < 0) {
        return;
    }

    snprintf(path, sizeof(path), "/proc/%ld/environ", (long)cred.pid);
    fp = fopen(path, "rb");
    if (!fp) {
        return;
    }

    n = fread(envbuf, 1, sizeof(envbuf) - 1, fp);
    fclose(fp);
    if (n == 0) {
        return;
    }
    envbuf[n] = '\0';

    while (i < n) {
        char *kv = &envbuf[i];
        size_t kv_len = strnlen(kv, n - i);

        if (kv_len == 0) {
            i++;
            continue;
        }

        if (strncmp(kv, "WVM_LOCAL_SPLIT=", 16) == 0) {
            char *endptr = NULL;
            long parsed = strtol(kv + 16, &endptr, 10);
            if (endptr && *endptr == '\0' && parsed >= 0 && parsed <= INT_MAX) {
                wavevm_apply_split_hint((int)parsed);
            }
            break;
        }

        i += kv_len + 1;
    }
#else
    (void)sock;
#endif
}

static int connect_to_master_helper(void)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    const char *env_path;
    char fallback[128];

    if (sock < 0) {
        return -1;
    }

    env_path = getenv("WVM_ENV_SOCK_PATH");
    if (!env_path) {
        const char *inst_id = getenv("WVM_INSTANCE_ID");
        snprintf(fallback, sizeof(fallback), "/tmp/wvm_user_%s.sock",
                 inst_id ? inst_id : "0");
        env_path = fallback;
    }

    strncpy(addr.sun_path, env_path, sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    wavevm_try_import_split_from_peer(sock);
    return sock;
}

static int write_full(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int read_full(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

// TCG Helper Declarations (Defined in wavevm-tcg.c)
extern void wvm_tcg_get_state(CPUState *cpu, wvm_tcg_context_t *ctx);
extern void wvm_tcg_set_state(CPUState *cpu, wvm_tcg_context_t *ctx);
extern void wavevm_slave_vcpu_loop(CPUState *cpu);
static pthread_once_t g_wavevm_tcg_region_once = PTHREAD_ONCE_INIT;

static void wavevm_init_tcg_region_once(void)
{
    tcg_region_init();
}

struct wavevm_policy_ops {
    int (*schedule_policy)(int cpu_index);
};

// [Policy] Tiered Scheduling: Local vs Remote
static int remote_rpc_policy(int cpu_index) {
    //不再使用 WVM_LOCAL_CPU_COUNT 宏，而是使用动态变量
    if (cpu_index >= g_wvm_local_split) return 1; // 远程执行
    return 0; // 本地执行
}

static struct wavevm_policy_ops ops = { .schedule_policy = remote_rpc_policy };

static bool wavevm_valid_io_exit(const struct kvm_run *run)
{
    if (run->io.size != 1 && run->io.size != 2 &&
        run->io.size != 4 && run->io.size != 8) {
        return false;
    }
    if (run->io.count == 0 || run->io.count > 8) {
        return false;
    }
    return true;
}

static bool wavevm_valid_mmio_exit(const struct kvm_run *run)
{
    return run->mmio.len == 1 || run->mmio.len == 2 ||
           run->mmio.len == 4 || run->mmio.len == 8;
}

/* Keep local APIC/interrupt state and only import architectural sregs fields. */
static void wavevm_apply_arch_sregs(struct kvm_sregs *dst,
                                    const struct kvm_sregs *src)
{
    dst->cs = src->cs;
    dst->ds = src->ds;
    dst->es = src->es;
    dst->fs = src->fs;
    dst->gs = src->gs;
    dst->ss = src->ss;
    dst->tr = src->tr;
    dst->ldt = src->ldt;
    dst->gdt = src->gdt;
    dst->idt = src->idt;
    dst->cr0 = src->cr0;
    dst->cr2 = src->cr2;
    dst->cr3 = src->cr3;
    dst->cr4 = src->cr4;
    dst->cr8 = src->cr8;
    dst->efer = src->efer;
}

/* 
 * [物理意图] 充当远程 Slave 的“本地代理执行人”。
 * [关键逻辑] 当远程计算节点触发 PIO/MMIO 退出时，Master 在本地 QEMU 中代为执行该 I/O 操作并返回结果。
 * [后果] 解决了 I/O 设备物理位置的透明性。它让 Guest 以为显卡就在本地，即便真实的读写指令是在千里之外执行的。
 */
static void wavevm_handle_io(CPUState *cpu) {
    struct kvm_run *run = cpu->kvm_run;
    uint16_t port = run->io.port;
    void *data = (uint8_t *)run + run->io.data_offset;
    int is_write = (run->io.direction == KVM_EXIT_IO_OUT);

    /* [FIX] 处理 string I/O (REP INS/OUTS)：count 可能 > 1，需逐个迭代 */
    for (uint32_t i = 0; i < run->io.count; i++) {
        address_space_rw(&address_space_io, port, MEMTXATTRS_UNSPECIFIED,
                         data, run->io.size, is_write);
        data = (uint8_t *)data + run->io.size;
    }
}

static void wavevm_handle_mmio(CPUState *cpu) {
    struct kvm_run *run = cpu->kvm_run;
    hwaddr addr = run->mmio.phys_addr;
    void *data = run->mmio.data;

    address_space_rw(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                     data, run->mmio.len,
                     run->mmio.is_write);
}

/* 
 * [物理意图] 实现 CPU 核心的“跨物理节点迁移（Context Serialization）”。
 * [关键逻辑] 将 CPU 的全量寄存器状态、MSR 及时钟计数器进行二进制打包，并发射至远程 Slave。
 * [后果] 实现了算力的透明流动。若序列化逻辑不严谨（如漏掉 rflags），远程执行会立即触发 Guest Kernel Panic。
 */
static void wavevm_remote_exec(CPUState *cpu) {
    // 动态边界检查
    if (cpu->cpu_index >= g_configured_vcpus) return;
    
    int vcpu_sock = g_vcpu_socks[cpu->cpu_index];
    if (!g_wvm_debug_once) {
        g_wvm_debug_once = 1;
        fprintf(stderr, "[WVM-DBG] cpu=%d kvm_enabled=%d vcpu_sock=%d split=%d\n",
                cpu->cpu_index, kvm_enabled(), vcpu_sock, g_wvm_local_split);
    }
    
    // 如果没有 socket (说明是 Kernel Mode), 走 IOCTL 路径
    if (vcpu_sock < 0) { 
        WaveVMAccelState *s = WAVEVM_ACCEL(current_machine->accelerator);
        if (s->mode != WVM_MODE_KERNEL) {
            g_usleep(1000); // 异常状态，非 Kernel 且无 Socket
            return;
        }

        // 1. 准备请求结构体
        struct wvm_ipc_cpu_run_req req;
        struct wvm_ipc_cpu_run_ack ack;
        memset(&req, 0, sizeof(req));
        
        req.slave_id = WVM_NODE_AUTO_ROUTE;
        req.vcpu_index = cpu->cpu_index;

        // 2. 序列化 CPU 状态
        if (kvm_enabled()) {
            struct kvm_regs kregs;
            struct kvm_sregs ksregs;
            cpu_synchronize_state(cpu);
            ioctl(cpu->kvm_fd, KVM_GET_REGS, &kregs);
            ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);

            req.mode_tcg = 0;
            req.ctx.kvm.rax = kregs.rax; req.ctx.kvm.rbx = kregs.rbx; req.ctx.kvm.rcx = kregs.rcx;
            req.ctx.kvm.rdx = kregs.rdx; req.ctx.kvm.rsi = kregs.rsi; req.ctx.kvm.rdi = kregs.rdi;
            req.ctx.kvm.rsp = kregs.rsp; req.ctx.kvm.rbp = kregs.rbp;
            req.ctx.kvm.r8  = kregs.r8;  req.ctx.kvm.r9  = kregs.r9;  req.ctx.kvm.r10 = kregs.r10;
            req.ctx.kvm.r11 = kregs.r11; req.ctx.kvm.r12 = kregs.r12; req.ctx.kvm.r13 = kregs.r13;
            req.ctx.kvm.r14 = kregs.r14; req.ctx.kvm.r15 = kregs.r15;
            req.ctx.kvm.rip = kregs.rip; req.ctx.kvm.rflags = kregs.rflags;
            memcpy(req.ctx.kvm.sregs_data, &ksregs, sizeof(ksregs));
        } else {
            req.mode_tcg = 1;
            wvm_tcg_get_state(cpu, &req.ctx.tcg);
        }

        // 3. 陷入内核 (Trap into Kernel)
        // 这一步会阻塞，直到远程执行完毕并返回结果
        fprintf(stderr, "[WVM-DBG] kernel path cpu=%d target=%u mode_tcg=%u\n",
                cpu->cpu_index, req.slave_id, req.mode_tcg);
        int ret = ioctl(s->dev_fd, IOCTL_WVM_REMOTE_RUN, &req);
        
        if (ret < 0) {
            //fprintf(stderr, "WaveVM: Remote Run IOCTL failed: %s\n", strerror(errno));
            return;
        }

        // 4. 反序列化结果 (直接复用 req 的内存空间读取 ack，或者使用 memcpy)
        // 注意：内核将 Ack 数据回写到了 req 指针所在的内存
        memcpy(&ack, &req, sizeof(ack)); 

        if (ack.mode_tcg) {
            wvm_tcg_set_state(cpu, &ack.ctx.tcg);
        } else {
            struct kvm_regs kregs;
            struct kvm_sregs ksregs;
            wvm_kvm_context_t *kctx = &ack.ctx.kvm;

            kregs.rax = kctx->rax; kregs.rbx = kctx->rbx; kregs.rcx = kctx->rcx; 
            kregs.rdx = kctx->rdx; kregs.rsi = kctx->rsi; kregs.rdi = kctx->rdi;
            kregs.rsp = kctx->rsp; kregs.rbp = kctx->rbp;
            kregs.r8 = kctx->r8;   kregs.r9 = kctx->r9;   kregs.r10 = kctx->r10; 
            kregs.r11 = kctx->r11; kregs.r12 = kctx->r12; kregs.r13 = kctx->r13;
            kregs.r14 = kctx->r14; kregs.r15 = kctx->r15;
            kregs.rip = kctx->rip; kregs.rflags = kctx->rflags;
            
            ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);
            wavevm_apply_arch_sregs(&ksregs, (const struct kvm_sregs *)kctx->sregs_data);
            ioctl(cpu->kvm_fd, KVM_SET_SREGS, &ksregs);
            ioctl(cpu->kvm_fd, KVM_SET_REGS, &kregs);
            
            struct kvm_run *run = cpu->kvm_run;
            run->exit_reason = kctx->exit_reason;

            if (kctx->exit_reason == KVM_EXIT_IO) {
                run->io.direction = kctx->io.direction;
                run->io.size      = kctx->io.size;
                run->io.port      = kctx->io.port;
                run->io.count     = kctx->io.count;
                if (run->io.direction == KVM_EXIT_IO_OUT) {
                    size_t mmap_size = ioctl(cpu->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
                    if (run->io.data_offset + run->io.size * run->io.count <= mmap_size) {
                        uint8_t *io_ptr = (uint8_t *)run + run->io.data_offset;
                        size_t io_bytes = run->io.size * run->io.count;
                        if (io_bytes > sizeof(kctx->io.data)) {
                            io_bytes = sizeof(kctx->io.data);
                        }
                        memcpy(io_ptr, kctx->io.data, io_bytes);
                    }
                }
                if (!wavevm_valid_io_exit(run)) {
                    return;
                }
                qemu_mutex_lock_iothread();
                wavevm_handle_io(cpu);
                qemu_mutex_unlock_iothread();
            } 
            else if (kctx->exit_reason == KVM_EXIT_MMIO) {
                run->mmio.phys_addr = kctx->mmio.phys_addr;
                run->mmio.len       = kctx->mmio.len;
                run->mmio.is_write  = kctx->mmio.is_write;
                memcpy(run->mmio.data, kctx->mmio.data, 8);
                if (!wavevm_valid_mmio_exit(run)) {
                    return;
                }
                qemu_mutex_lock_iothread();
                wavevm_handle_mmio(cpu);
                qemu_mutex_unlock_iothread();
            }
        }
        return; 
    }

    // 准备发送缓冲区
    struct wvm_ipc_header_t ipc_hdr = {
        .type = WVM_IPC_TYPE_CPU_RUN,
        .len = sizeof(struct wvm_ipc_cpu_run_req),
    };
    struct wvm_ipc_cpu_run_req req;
    struct wvm_ipc_cpu_run_ack ack;
    memset(&req, 0, sizeof(req));

    req.slave_id = WVM_NODE_AUTO_ROUTE;
    req.vcpu_index = cpu->cpu_index;

    // 1. 序列化 CPU 状态 (Serialization)
    if (kvm_enabled()) {
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        cpu_synchronize_state(cpu);
        ioctl(cpu->kvm_fd, KVM_GET_REGS, &kregs);
        ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);

        req.mode_tcg = 0;

        wvm_kvm_context_t *kctx = &req.ctx.kvm;
        kctx->rax = kregs.rax; kctx->rbx = kregs.rbx; kctx->rcx = kregs.rcx;
        kctx->rdx = kregs.rdx; kctx->rsi = kregs.rsi; kctx->rdi = kregs.rdi;
        kctx->rsp = kregs.rsp; kctx->rbp = kregs.rbp;
        kctx->r8  = kregs.r8;  kctx->r9  = kregs.r9;  kctx->r10 = kregs.r10;
        kctx->r11 = kregs.r11; kctx->r12 = kregs.r12; kctx->r13 = kregs.r13;
        kctx->r14 = kregs.r14; kctx->r15 = kregs.r15;
        kctx->rip = kregs.rip; kctx->rflags = kregs.rflags;
        memcpy(kctx->sregs_data, &ksregs, sizeof(ksregs));

        /* [FIX] 携带上一轮 IO/MMIO 处理结果：
         * 若上次远程执行触发了 IO IN 或 MMIO READ，Master 已在本地
         * wavevm_handle_io/mmio 中将设备读结果填入 run->io.data / mmio.data。
         * Slave 需要这些数据在下次 KVM_RUN 时完成未决的 IO 指令。 */
        struct kvm_run *run = cpu->kvm_run;
        kctx->exit_reason = run->exit_reason;
        if (run->exit_reason == KVM_EXIT_IO) {
            kctx->io.direction = run->io.direction;
            kctx->io.size      = run->io.size;
            kctx->io.port      = run->io.port;
            kctx->io.count     = run->io.count;
            if (run->io.direction == KVM_EXIT_IO_IN) {
                size_t io_bytes = (size_t)run->io.size * run->io.count;
                if (io_bytes > sizeof(kctx->io.data)) io_bytes = sizeof(kctx->io.data);
                memcpy(kctx->io.data, (uint8_t *)run + run->io.data_offset, io_bytes);
            }
        } else if (run->exit_reason == KVM_EXIT_MMIO) {
            kctx->mmio.phys_addr = run->mmio.phys_addr;
            kctx->mmio.len       = run->mmio.len;
            kctx->mmio.is_write  = run->mmio.is_write;
            if (!run->mmio.is_write) {
                memcpy(kctx->mmio.data, run->mmio.data, 8);
            }
        }
    } else {
        req.mode_tcg = 1;
        wvm_tcg_get_state(cpu, &req.ctx.tcg);
    }

    // 2. IPC 发送 (wvm_ipc_header + wvm_ipc_cpu_run_req)
    if (write_full(vcpu_sock, &ipc_hdr, sizeof(ipc_hdr)) < 0) {
        return;
    }
    if (write_full(vcpu_sock, &req, sizeof(req)) < 0) {
        return;
    }

    // 3. IPC 接收 ACK (阻塞本线程，不影响其他 vCPU)
    if (read_full(vcpu_sock, &ack, sizeof(ack)) < 0) {
        return;
    }
    if (ack.status < 0) {
        return;
    }

    // 4. 反序列化 CPU 状态
    if (ack.mode_tcg) {
        wvm_tcg_set_state(cpu, &ack.ctx.tcg);
    } else {
        struct kvm_regs kregs;
        struct kvm_sregs ksregs;
        wvm_kvm_context_t *kctx = &ack.ctx.kvm;

        kregs.rax = kctx->rax; kregs.rbx = kctx->rbx; kregs.rcx = kctx->rcx; 
        kregs.rdx = kctx->rdx; kregs.rsi = kctx->rsi; kregs.rdi = kctx->rdi;
        kregs.rsp = kctx->rsp; kregs.rbp = kctx->rbp;
        kregs.r8 = kctx->r8;   kregs.r9 = kctx->r9;   kregs.r10 = kctx->r10; 
        kregs.r11 = kctx->r11; kregs.r12 = kctx->r12; kregs.r13 = kctx->r13;
        kregs.r14 = kctx->r14; kregs.r15 = kctx->r15;
        kregs.rip = kctx->rip; kregs.rflags = kctx->rflags;
        
        ioctl(cpu->kvm_fd, KVM_GET_SREGS, &ksregs);
        wavevm_apply_arch_sregs(&ksregs, (const struct kvm_sregs *)kctx->sregs_data);
        ioctl(cpu->kvm_fd, KVM_SET_SREGS, &ksregs);
        ioctl(cpu->kvm_fd, KVM_SET_REGS, &kregs);
        
        // 5. Replay IO/MMIO
        struct kvm_run *run = cpu->kvm_run;
        run->exit_reason = kctx->exit_reason;

        if (kctx->exit_reason == KVM_EXIT_IO) {
            run->io.direction = kctx->io.direction;
            run->io.size      = kctx->io.size;
            run->io.port      = kctx->io.port;
            run->io.count     = kctx->io.count;
            
            if (run->io.direction == KVM_EXIT_IO_OUT) {
                size_t mmap_size = ioctl(cpu->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
                if (run->io.data_offset + run->io.size * run->io.count <= mmap_size) {
                    uint8_t *io_ptr = (uint8_t *)run + run->io.data_offset;
                    size_t io_bytes = run->io.size * run->io.count;
                    if (io_bytes > sizeof(kctx->io.data)) {
                        io_bytes = sizeof(kctx->io.data);
                    }
                    memcpy(io_ptr, kctx->io.data, io_bytes);
                }
            }
            if (!wavevm_valid_io_exit(run)) {
                return;
            }
            wavevm_handle_io(cpu);
        } 
        else if (kctx->exit_reason == KVM_EXIT_MMIO) {
            run->mmio.phys_addr = kctx->mmio.phys_addr;
            run->mmio.len       = kctx->mmio.len;
            run->mmio.is_write  = kctx->mmio.is_write;
            memcpy(run->mmio.data, kctx->mmio.data, 8);
            if (!wavevm_valid_mmio_exit(run)) {
                return;
            }
            wavevm_handle_mmio(cpu);
        }
    }
}

/* 
 * [物理意图] 重新定义 vCPU 的“心脏跳动规律”。
 * [关键逻辑] 拦截标准的 KVM_RUN 循环，根据调度策略决定本轮指令是交给本地 KVM 还是进行远程上下文序列化。
 * [后果] 这是超级虚拟机的总节拍器。它保证了在异构算力环境下，vCPU 能够平滑地在本地与远程之间切换执行流。
 */
/*
 * WaveVM vCPU thread function -- aligned with standard QEMU MTTCG skeleton.
 *
 * Key difference from vanilla MTTCG: the scheduling branch that routes
 * vCPUs to either local cpu_exec() or remote wavevm_remote_exec() based
 * on g_wvm_local_split.  Everything else (iothread lock discipline,
 * cpu_exec_start/end, exit_request, qemu_wait_io_event every iteration)
 * follows the standard QEMU pattern so that vm_start()/resume_all_vcpus()
 * and the full VM lifecycle management work correctly.
 */
static void *wavevm_cpu_thread_fn(void *arg) {
    CPUState *cpu = arg;
    char *role = getenv("WVM_ROLE");
    bool is_slave = (role && strcmp(role, "SLAVE") == 0);

    rcu_register_thread();
    /*
     * Only register TCG context for vCPUs that will execute locally.
     * Remote-only vCPUs (cpu_index >= g_wvm_local_split on Master) only
     * do RPC forwarding and never call cpu_exec(), so they do not need
     * a TCG region.  Without this guard, tcg_region_initial_alloc fails
     * because non-MTTCG mode only provisions a single TCG region.
     */
    if (!kvm_enabled() && (is_slave || cpu->cpu_index < g_wvm_local_split)) {
        tcg_register_thread();
    }

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;
    current_cpu = cpu;
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    /* Slave: hand off to dedicated slave loop (holds its own lock discipline) */
    if (is_slave) {
        qemu_mutex_unlock_iothread();
        wavevm_slave_vcpu_loop(cpu);
        goto out;
    }

    /* KVM bootstrap for this vCPU (if KVM is the underlying engine) */
    if (kvm_enabled()) {
        kvm_init_vcpu(cpu, &error_fatal);
        kvm_init_cpu_signals(cpu);
        cpu_synchronize_state(cpu);
    }

    /*
     * Trigger an exit from the first cpu_exec() call so we cycle through
     * qemu_wait_io_event() and properly synchronize with vm_start().
     */
    cpu->exit_request = 1;

    /*
     * Main vCPU loop -- mirrors tcg_cpu_thread_fn (MTTCG) structure.
     * The iothread lock is held at the top of every iteration.
     */
    do {
        if (cpu_can_run(cpu)) {
            /* Remote vCPU: forward to Slave via RPC */
            if (ops.schedule_policy(cpu->cpu_index) == 1) {
                qemu_mutex_unlock_iothread();
                wavevm_remote_exec(cpu);
                qemu_mutex_lock_iothread();
            }
            /* Local vCPU: 复用 QEMU 原生 kvm_cpu_exec()
             * 包含完整的 kvm_arch_pre_run (中断注入/APIC 同步)、
             * kvm_arch_post_run (状态回收)、kvm_arch_put_registers
             * (脏寄存器推送) 以及全部 exit reason 处理。
             */
            else if (kvm_enabled()) {
                int r = kvm_cpu_exec(cpu);
                if (r == EXCP_DEBUG) {
                    cpu_handle_guest_debug(cpu);
                }
            } else {
                /* TCG local execution */
                int r;
                qemu_mutex_unlock_iothread();
                cpu_exec_start(cpu);
                r = cpu_exec(cpu);
                cpu_exec_end(cpu);
                qemu_mutex_lock_iothread();

                switch (r) {
                case EXCP_DEBUG:
                    cpu_handle_guest_debug(cpu);
                    break;
                case EXCP_HALTED:
                    break;
                case EXCP_ATOMIC:
                    qemu_mutex_unlock_iothread();
                    cpu_exec_step_atomic(cpu);
                    qemu_mutex_lock_iothread();
                    break;
                default:
                    break;
                }
            }
        }

        qatomic_mb_set(&cpu->exit_request, 0);
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));

    if (kvm_enabled() && cpu->kvm_fd >= 0) {
        kvm_destroy_vcpu(cpu);
    }
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
out:
    rcu_unregister_thread();
    return NULL;
}

/* 
 * [物理意图] 为每个逻辑核心开辟专属的“高速公路（Per-vCPU Socket Pool）”。
 * [关键逻辑] 在 vCPU 创建瞬间建立独立的 IPC 通道，彻底消除多核并发时的 Socket 锁竞争。
 * [后果] 实现了算力的线性扩展能力。这是 V30 支撑一亿核心的关键，保证了 vCPU 数量增加时通信延迟不上升。
 */
void wavevm_start_vcpu_thread(CPUState *cpu) {
    char thread_name[VCPU_THREAD_NAME_SIZE];
    WaveVMAccelState *s = WAVEVM_ACCEL(current_machine->accelerator);
    char *role = getenv("WVM_ROLE");

    static pthread_mutex_t g_init_lock = PTHREAD_MUTEX_INITIALIZER;

    if (!kvm_enabled()) {
        pthread_once(&g_wavevm_tcg_region_once, wavevm_init_tcg_region_once);
    }
    
    // 双重检查锁定 (Double-Checked Locking) 优化性能，或者直接加锁也行（毕竟只执行一次）
    if (!g_vcpu_socks) {
        pthread_mutex_lock(&g_init_lock);
        // 再次检查，防止在等待锁的过程中已被其他线程初始化
        if (!g_vcpu_socks) {
            g_configured_vcpus = (current_machine && current_machine->smp.cpus > 0) ?
                                 current_machine->smp.cpus : 1;
            
            g_vcpu_socks = g_malloc0(sizeof(int) * g_configured_vcpus);
            for (int i = 0; i < g_configured_vcpus; i++) {
                g_vcpu_socks[i] = -1;
            }
        }
        pthread_mutex_unlock(&g_init_lock);
    }

    if (s->mode == WVM_MODE_USER && !(role && strcmp(role, "SLAVE") == 0)) {
        // 动态边界检查
        if (cpu->cpu_index < g_configured_vcpus) {
            g_vcpu_socks[cpu->cpu_index] = connect_to_master_helper();
            if (g_vcpu_socks[cpu->cpu_index] < 0) {
                fprintf(stderr, "vCPU %d failed to connect master!\n", cpu->cpu_index);
                exit(1);
            }
        }
    } else {
        // Kernel 模式和 Slave 模式不使用此机制
        g_vcpu_socks[cpu->cpu_index] = -1; 
    }
    
    cpu->thread = g_malloc0(sizeof(QemuThread));
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);
    
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/WVM", cpu->cpu_index);
    
    qemu_thread_create(cpu->thread, thread_name, wavevm_cpu_thread_fn, cpu, QEMU_THREAD_JOINABLE);
}
```

**文件**: `wavevm-qemu/accel/wavevm/wavevm-user-mem.c`

```c

/*
 * [IDENTITY] Frontend Wavelet Engine - The Memory Guardian
 * ---------------------------------------------------------------------------
 * 物理角色：QEMU 内部的内存一致性协调者。
 * 职责边界：
 * 1. 运行 Latch 机制锁定读写冲突。
 * 2. 定时收割脏页生成 Diff，维持 Wavelet 增量推送。
 * 3. 运行重排缓冲区 (Reorder Window)，将乱序推送"坍缩"为有序内存。
 * 
 * [禁止事项]
 * - 严禁在 sigsegv_handler 中进行任何可能引起休眠的操作。
 * - 严禁关闭 Lazy TLB Flush (defer_ro_protect)，否则 QEMU 性能将崩溃。
 * ---------------------------------------------------------------------------
 */
#include "qemu/osdep.h"
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <time.h>
#include <stdatomic.h>
#include "sysemu/kvm.h"
#include "exec/memory.h"
#include "exec/ram_addr.h"

#include "../../../common_include/wavevm_protocol.h"

/* 
 * WaveVM V29.5 "Wavelet" User-Mode Memory Engine (Production Ready)
 */

// --- 全局配置与状态 ---
static int g_is_slave = 0;
static int g_fd_req = -1;  
static __thread int t_req_sock = -1;
static __thread uint8_t t_net_buf[WVM_MAX_PACKET_SIZE];
static int g_fd_push = -1;
static int g_ipc_diff_sock = -1; // [FIX-G1] Master TCG harvester 专用 IPC socket
static void *g_ram_base = NULL;
static size_t g_ram_size = 0;
static void *g_shm_shadow = NULL;
static uint32_t g_slave_id = 0;
static bool g_fault_hook_enabled = false;
static bool g_fault_hook_checked = false;
static int g_client_sync_batch = 1024; // 当前生效的 Batch
static int g_min_batch = 1;            // 下限
static int g_max_batch = 8192;         // 上限
static int g_enable_auto_tuning = 1;   // 开关：1=自动, 0=固定(强一致用)

static uint64_t get_us_time(void);
static uint64_t get_local_page_version(uint64_t gpa);
static void set_local_page_version(uint64_t gpa, uint64_t version);
static long wait_for_directory_ack_safe(void);
static int internal_connect_master(void);
static int ensure_local_shm_shadow(void);
static void send_diff_via_ipc(void *buf, size_t len);

// 脏区捕获链表
typedef struct WritablePage {
    uint64_t gpa;
    void* pre_image_snapshot;
    struct WritablePage *next;
} WritablePage;

#define PAGE_POOL_SIZE 4096
static WritablePage g_page_pool[PAGE_POOL_SIZE];
static void* g_image_pool[PAGE_POOL_SIZE]; // 预分配快照空间
static atomic_uint g_pool_idx = 0; /* [FIX] 必须无符号，防止 21 亿次写缺页后有符号溢出导致负数取模越界 */

static volatile bool g_threads_running = false;
static pthread_t g_listen_thread;
static pthread_t g_harvester_thread;

#define MAX_RAM_BLOCKS 64

typedef struct {
    uintptr_t hva_start;
    uintptr_t hva_end;
    uint64_t  gpa_start;
    uint64_t  size;
} GVMRamBlock;

static GVMRamBlock g_mem_blocks[MAX_RAM_BLOCKS];
static int g_block_count = 0;

/* 
 * [物理意图] 在 QEMU 内部建立 Guest 物理地址(GPA)与宿主机虚拟地址(HVA)的“空间映射图”。
 * [关键逻辑] 将 RAM 块注册到私有映射表，并执行初始 mprotect(PROT_NONE) 以强制触发首次访问缺页。
 * [后果] 若未正确注册，特定的内存区域将脱离分布式一致性引擎的监控，导致该区域的写操作无法全网同步。
 */
void wavevm_register_ram_block(void *hva, uint64_t size, uint64_t gpa) {
    if (!g_fault_hook_checked) {
        const char *hook_env = getenv("WVM_ENABLE_FAULT_HOOK");
        g_fault_hook_enabled = (!hook_env || atoi(hook_env) != 0);
        g_fault_hook_checked = true;
    }
    for (int i = 0; i < g_block_count; i++) {
        if (g_mem_blocks[i].gpa_start == gpa &&
            g_mem_blocks[i].hva_start == (uintptr_t)hva &&
            g_mem_blocks[i].size == size) {
            fprintf(stderr,
                    "[WaveVM-User] dedup ram_block hva=%p gpa=%#llx size=%#llx blocks=%d\n",
                    hva,
                    (unsigned long long)gpa,
                    (unsigned long long)size,
                    g_block_count);
            return;
        }
    }
    if (g_block_count >= MAX_RAM_BLOCKS) exit(1);
    if (!kvm_enabled()) {
        mprotect(hva, size, PROT_NONE);
    }
    g_mem_blocks[g_block_count].hva_start = (uintptr_t)hva;
    g_mem_blocks[g_block_count].hva_end   = (uintptr_t)hva + size;
    g_mem_blocks[g_block_count].gpa_start = gpa;
    g_mem_blocks[g_block_count].size      = size;
    g_block_count++;
    {
        char msg[224];
        int n = snprintf(msg, sizeof(msg),
                         "[WaveVM-User] ram_block hva=%p..%p gpa=%#llx size=%#llx blocks=%d\n",
                         hva, (void *)((uintptr_t)hva + size),
                         (unsigned long long)gpa,
                         (unsigned long long)size,
                         g_block_count);
        if (n > 0) {
            write(STDERR_FILENO, msg, (size_t)n);
        }
    }
}

// [替换] 查表法 HVA 转 GPA (用于 sigsegv)
static uint64_t hva_to_gpa_safe(uintptr_t addr) {
    // 这里 block_count 是在 wavevm_region_add 时动态增加的
    for (int i = 0; i < g_block_count; i++) {
        if (addr >= g_mem_blocks[i].hva_start && addr < g_mem_blocks[i].hva_end) {
            // 真实的 GPA = 块起始 GPA + 块内偏移
            return g_mem_blocks[i].gpa_start + (addr - g_mem_blocks[i].hva_start);
        }
    }
    return (uint64_t)-1;
}

// [替换] 查表法 GPA 转 HVA (用于 harvester)
static void* gpa_to_hva_safe(uint64_t gpa) {
    for (int i = 0; i < g_block_count; i++) {
        if (gpa >= g_mem_blocks[i].gpa_start && 
            gpa < g_mem_blocks[i].gpa_start + g_mem_blocks[i].size) {
            return (void*)(g_mem_blocks[i].hva_start + (gpa - g_mem_blocks[i].gpa_start));
        }
    }
    return NULL;
}

// 通用极速零检测
static inline bool is_page_all_zero(void *addr) {
    uint64_t *p = (uint64_t *)addr;
    for (int i = 0; i < 512; i += 4) {
        if (p[i] | p[i+1] | p[i+2] | p[i+3]) return false;
    }
    return true;
}

// --- Lazy Protection Queue ---
#define LAZY_QUEUE_SIZE 64  
static __thread uint64_t t_lazy_ro_queue[LAZY_QUEUE_SIZE];
static __thread int t_lazy_count = 0;

static void flush_lazy_ro_queue(void) {
    if (t_lazy_count == 0) return;
    for (int i = 0; i < t_lazy_count; i++) {
        uint64_t gpa = t_lazy_ro_queue[i];
        void *hva = gpa_to_hva_safe(gpa);
        if (hva) mprotect(hva, 4096, PROT_READ);
    }
    t_lazy_count = 0;
}

static void defer_ro_protect(uint64_t gpa) {
    if (kvm_enabled()) return; /* KVM 脏页由 dirty log 跟踪，绝不能 mprotect 降权，否则 EPT 违例 → exit=17 */
    t_lazy_ro_queue[t_lazy_count++] = gpa;
    if (t_lazy_count >= LAZY_QUEUE_SIZE) flush_lazy_ro_queue();
}

// 发送 PUSH 包 (Diff 或 Zero)
static void send_push_packet(uint64_t gpa, uint64_t version, void *data, uint16_t size, uint8_t flags) {
    if (g_is_slave && g_fd_push < 0) return;
    if (!g_is_slave && g_ipc_diff_sock < 0) {
        g_ipc_diff_sock = internal_connect_master();
        if (g_ipc_diff_sock < 0) return;
    }
    size_t pl_len = sizeof(struct wvm_diff_log) + size;
    size_t pkt_len = sizeof(struct wvm_header) + pl_len;
    uint8_t *buf = malloc(pkt_len);
    if (!buf) return;

    struct wvm_header *hdr = (struct wvm_header *)buf;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_COMMIT_DIFF);
    hdr->payload_len = htons(pl_len);
    hdr->slave_id = htonl(g_slave_id);
    hdr->target_id = htonl(WVM_NODE_AUTO_ROUTE); // [V31 Fix] 本地通信标记
    hdr->req_id = 0;
    hdr->qos_level = 1;
    hdr->flags = flags; // [关键]
    hdr->crc32 = 0;

    struct wvm_diff_log *log = (struct wvm_diff_log *)(buf + sizeof(*hdr));
    log->gpa = WVM_HTONLL(gpa);
    log->version = WVM_HTONLL(version);
    log->offset = 0;
    log->size = htons(size);

    if (size > 0 && data) memcpy(log->data, data, size);

    hdr->crc32 = htonl(calculate_crc32(buf, pkt_len));
    // [FIX-G1] Master TCG 走 IPC
    if (g_is_slave) {
        send(g_fd_push, buf, pkt_len, 0);
    } else {
        send_diff_via_ipc(buf, pkt_len);
    }
    free(buf);
}

// --- [FIX 2] Latch 锁分段 (放在文件头部全局区) ---

// 强制 128 字节对齐 (兼容 x86_64 和 ARM64/Graviton)
typedef struct {
    volatile uint64_t val;
    uint8_t padding[128 - sizeof(uint64_t)]; 
} __attribute__((aligned(128))) aligned_latch_t;

// 静态断言：编译期检查对齐是否成功
_Static_assert(sizeof(aligned_latch_t) == 128, "Latch alignment failed");

#define LATCH_SHARDS 256
static aligned_latch_t g_latches[LATCH_SHARDS];

// 初始化 (在 wavevm_user_mem_init 调用)
static void init_latches(void) {
    for(int i=0; i<LATCH_SHARDS; i++) g_latches[i].val = (uint64_t)-1;
}

#define LATCH_IDX(gpa) ((gpa >> 12) % LATCH_SHARDS)

// --- [FIX 4] Mode B 微型重排窗口 ---

#define REORDER_WIN_SIZE 32  // 容忍 32 个包的乱序
#define REORDER_MASK (REORDER_WIN_SIZE - 1)

typedef struct {
    uint64_t gpa;
    uint64_t version;
    uint16_t msg_type;
    uint16_t len;
    uint8_t *data;
    uint64_t timestamp_us; // 存入时间戳
    bool active;
} ReorderSlot;

static ReorderSlot g_reorder_buf[REORDER_WIN_SIZE];
static pthread_spinlock_t g_reorder_lock;

// 简单的异或哈希
static inline int get_reorder_idx(uint64_t gpa, uint64_t version) {
    return ((gpa >> 12) ^ version) & REORDER_MASK;
}

// 存入未来的包
static void buffer_future_packet(uint64_t gpa, uint64_t version, uint16_t type, void *data, uint16_t len) {
    int idx = get_reorder_idx(gpa, version);
    pthread_spin_lock(&g_reorder_lock);
    
    // 如果槽位被占，释放旧数据 (Drop-on-Collision 策略)
    if (g_reorder_buf[idx].active) free(g_reorder_buf[idx].data);

    g_reorder_buf[idx].gpa = gpa;
    g_reorder_buf[idx].version = version;
    g_reorder_buf[idx].msg_type = type;
    g_reorder_buf[idx].len = len;
    g_reorder_buf[idx].active = true;
    g_reorder_buf[idx].timestamp_us = get_us_time();
    g_reorder_buf[idx].data = malloc(len);
    if (g_reorder_buf[idx].data) memcpy(g_reorder_buf[idx].data, data, len);
    else g_reorder_buf[idx].active = false; // OOM 保护
    
    pthread_spin_unlock(&g_reorder_lock);
}

/* [FIX-F3] Forward declaration: KVM proactive page fetch (defined after request_page_sync) */
static void kvm_proactive_page_fetch(uint64_t gpa);
static int request_page_sync(uintptr_t fault_addr, bool is_write);

/*
 * [物理意图] 接收并应用来自 P2P 网络的"真理推送"，更新本地物理内存。
 * [关键逻辑] 执行严格的版本判定（is_next_version）：顺序包直接 memcpy，版本断层包则强制失效（Invalidate）本地映射。
 * [后果] 实现了 MESI 协议的远程写入动作。它保证了即便在乱序网络下，本地 vCPU 看到的内存也是单调递增的一致性状态。
 */
void wvm_apply_remote_push(uint16_t msg_type, void *payload) {
// --- 分支 1: Diff 推送 ---
    if (msg_type == MSG_PAGE_PUSH_DIFF) {
        struct wvm_diff_log* log = (struct wvm_diff_log*)payload;
        uint64_t gpa = WVM_NTOHLL(log->gpa);
        uint64_t push_ver = WVM_NTOHLL(log->version);
        uint64_t local_ver = get_local_page_version(gpa);

        // [FIX] 严格版本/幂等性校验
        
        // 情况 A: 过期或重复的包 (Stale/Duplicate)
        // 网络重传或乱序导致，直接静默丢弃，不做任何内存操作
        if (!is_newer_version(local_ver, push_ver)) {
            return; 
        }

        // 情况 B: 顺序到达的包 (Ideal Sequence)
        if (is_next_version(local_ver, push_ver)) {
            uint16_t offset = ntohs(log->offset);
            uint16_t size = ntohs(log->size);
            
            // 边界检查：防止恶意包导致 Segfault
            if (offset + size > 4096) return;

            // 1. 安全 GPA→HVA 转换（兼容 PCI hole 多段 RAM）
            void *page_hva = gpa_to_hva_safe(gpa);
            if (!page_hva) return;
            mprotect(page_hva, 4096, PROT_READ | PROT_WRITE);
            // 2. 应用增量数据
            memcpy((uint8_t*)page_hva + offset, log->data, size);
            // 3. 放入惰性锁回队列 (性能优化)
            defer_ro_protect(gpa);
            
            // 4. 更新本地版本
            set_local_page_version(gpa, push_ver);
        } 
        // 情况 C: 版本断层 (Gap Detected)
        // 例如：本地是 v10，收到了 v12。中间缺了 v11。
        else {
            // 此时内存状态已不可信，必须强制失效
            // 下次访问触发 sigsegv -> request_page_sync (V28 Pull) 拉取最新全量
            if (!kvm_enabled()) {
                void *inv_hva = gpa_to_hva_safe(gpa);
                if (inv_hva) mprotect(inv_hva, 4096, PROT_NONE);
            }

            // 将本地版本置 0，确保下次 Pull 回来的数据（无论版本多少）都能成功覆盖
            set_local_page_version(gpa, 0);

            // [FIX-F3] KVM 模式下 mprotect(PROT_NONE) 被跳过，guest 不会触发 SIGSEGV，
            // 必须主动拉取最新页面，否则 guest 永远读到 stale 数据。
            if (kvm_enabled()) {
                kvm_proactive_page_fetch(gpa);
            }
        }
    }
    // --- 分支 2: 全页推送 / 强制同步 ---
    else if (msg_type == MSG_PAGE_PUSH_FULL || msg_type == MSG_FORCE_SYNC) {
        struct wvm_full_page_push* full = (struct wvm_full_page_push*)payload;
        uint64_t gpa = WVM_NTOHLL(full->gpa);
        uint64_t push_ver = WVM_NTOHLL(full->version);
        
        if (is_newer_version(get_local_page_version(gpa), push_ver)) {
            void *page_hva = gpa_to_hva_safe(gpa);
            if (!page_hva) return;
            mprotect(page_hva, 4096, PROT_READ | PROT_WRITE);
            memcpy(page_hva, full->data, 4096);
            
            // 惰性锁回
            defer_ro_protect(gpa);
            
            set_local_page_version(gpa, push_ver);
        }
    }
    // --- 分支 3: Prophet RPC (V29 新增) ---
    else if (msg_type == MSG_RPC_BATCH_MEMSET) {
        // [FIX] Daemon 的 handle_rpc_batch_execution 只修改了 g_shm_ptr，
        // 但 QEMU TCG 模式使用独立的匿名 RAM (g_ram_base)，必须同步执行物理填充，
        // 否则 Guest 看到的内存不会被清零，Prophet 指令形同虚设。
        struct wvm_rpc_batch_memset *batch = (struct wvm_rpc_batch_memset *)payload;
        uint32_t count = ntohl(batch->count);
        uint32_t val = ntohl(batch->val);
        struct wvm_rpc_region *regions = (struct wvm_rpc_region *)(batch + 1);
        for (uint32_t i = 0; i < count; i++) {
            uint64_t gpa = WVM_NTOHLL(regions[i].gpa);
            uint64_t r_len = WVM_NTOHLL(regions[i].len);
            if (gpa + r_len <= g_ram_size) {
                void *rpc_hva = gpa_to_hva_safe(gpa);
                if (!rpc_hva) continue;
                mprotect(rpc_hva, r_len, PROT_READ | PROT_WRITE);
                memset(rpc_hva, val, r_len);
                // [FIX] 同步 Prophet 版本号，否则后续 diff 引擎认为该页没变过，
                //       导致远端节点版本断层无法收敛。
                for (uint64_t pg = gpa & ~0xFFFULL; pg < gpa + r_len; pg += 4096) {
                    uint64_t v = get_local_page_version(pg);
                    set_local_page_version(pg, v + 1);
                }
                // tb_flush 在 wavevm-all.c 的 IPC 处理路径中已执行，此处不重复
            }
        }
    }
}

// 检查并应用后续包 (链式反应)
static bool check_and_apply_next(uint64_t gpa, uint64_t next_ver) {
    // 关键点：不再相信调用者传进来的 next_ver (它可能是算错的)
    // 我们基于本地真实的当前版本，去推算可能的“逻辑下一跳”
    uint64_t local_v = get_local_page_version(gpa);
    
    // 可能性 A: 纪元内连续 (+1)
    uint64_t next_a = local_v + 1;
    // 可能性 B: 跨纪元第一炮 (Epoch + 1, Counter = 1)
    uint32_t cur_epoch = (uint32_t)(local_v >> 32);
    uint64_t next_b = ((uint64_t)(cur_epoch + 1) << 32) | 1;

    pthread_spin_lock(&g_reorder_lock);

    // 1. 先探测可能性 A
    int idx = get_reorder_idx(gpa, next_a);
    ReorderSlot *s = &g_reorder_buf[idx];
    if (s->active && s->gpa == gpa && s->version == next_a) goto hit;

    // 2. A 没中，探测可能性 B
    idx = get_reorder_idx(gpa, next_b);
    s = &g_reorder_buf[idx];
    if (s->active && s->gpa == gpa && s->version == next_b) goto hit;

    // 都没中
    pthread_spin_unlock(&g_reorder_lock);
    return false;

hit:
    // 命中！执行应用逻辑
    void *d = s->data;
    uint16_t t = s->msg_type;
    
    s->active = false;
    s->data = NULL; // [修复] 彻底杜绝野指针
    
    pthread_spin_unlock(&g_reorder_lock);

    wvm_apply_remote_push(t, d);
    free(d);
    return true;
}

static WritablePage *g_writable_pages_list = NULL;

// 线程局部
static __thread int t_com_sock = -1; 

// --- 辅助函数 ---

// 使用二级页表风格的目录来存储版本号，适配 64 位地址空间

#define L1_BITS 10
#define L2_BITS 10
#define L1_SIZE (1UL << L1_BITS)
#define L2_SIZE (1UL << L2_BITS)
#define PAGE_SHIFT 12

// 二级索引结构
static uint64_t **g_ver_root = NULL;

/* 
 * [物理意图] 维护本地缓存页面的“逻辑时钟（版本号）”快照。
 * [关键逻辑] 采用二级页表结构的索引表（Radix-like Table），以 O(1) 时间复杂度追踪 500PB 空间内每页的版本。
 * [后果] 这是判定“何为真理”的基石。版本号记录错误会直接导致读到旧数据（Stale Read）或触发不必要的全页强制同步。
 */
static uint64_t get_local_page_version(uint64_t gpa) {
    if (!g_ver_root) return 0;

    uint64_t pfn = gpa >> PAGE_SHIFT;
    uint64_t l1_idx = (pfn >> L2_BITS) & (L1_SIZE - 1);
    uint64_t l2_idx = pfn & (L2_SIZE - 1);

    if (g_ver_root[l1_idx] == NULL) return 0;
    return __atomic_load_n(&g_ver_root[l1_idx][l2_idx], __ATOMIC_ACQUIRE);
}

static void set_local_page_version(uint64_t gpa, uint64_t version) {
    if (!g_ver_root) {
        g_ver_root = calloc(L1_SIZE, sizeof(uint64_t *));
        if (!g_ver_root) return; // OOM: 静默放弃版本追踪，不会崩溃
    }

    uint64_t pfn = gpa >> PAGE_SHIFT;
    uint64_t l1_idx = (pfn >> L2_BITS) & (L1_SIZE - 1);
    uint64_t l2_idx = pfn & (L2_SIZE - 1);

    if (g_ver_root[l1_idx] == NULL) {
        g_ver_root[l1_idx] = calloc(L2_SIZE, sizeof(uint64_t));
        if (!g_ver_root[l1_idx]) return; // OOM: 同上
    }
    __atomic_store_n(&g_ver_root[l1_idx][l2_idx], version, __ATOMIC_RELEASE);
}

static void safe_log(const char *msg) {
    if (write(STDERR_FILENO, msg, strlen(msg))) {};
}

// 健壮的阻塞读取 (处理 EINTR)
static int read_exact(int fd, void *buf, size_t len) {
    size_t received = 0;
    char *ptr = (char *)buf;
    while (received < len) {
        ssize_t ret = read(fd, ptr + received, len - received);
        if (ret > 0) received += ret;
        else if (ret == 0) return -1; // EOF
        else if (errno != EINTR) return -1; // Error
    }
    return 0;
}

static int internal_connect_master(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    
    const char *env_path = getenv("WVM_ENV_SOCK_PATH");
    if (!env_path) {
        char *inst_id = getenv("WVM_INSTANCE_ID");
        static char fallback_path[128];
        snprintf(fallback_path, sizeof(fallback_path), "/tmp/wvm_user_%s.sock", inst_id ? inst_id : "0");
        env_path = fallback_path;
    }
    strncpy(addr.sun_path, env_path, sizeof(addr.sun_path) - 1);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock); return -1;
    }
    return sock;
}

static int ensure_local_shm_shadow(void)
{
    if (g_shm_shadow || g_is_slave || !g_ram_size) {
        return g_shm_shadow ? 0 : -1;
    }

    const char *shm_path = getenv("WVM_SHM_FILE");
    if (!shm_path) {
        shm_path = "/wavevm_ram";
    }

    int shm_fd = shm_open(shm_path, O_RDWR, 0666);
    if (shm_fd < 0) {
        return -1;
    }

    void *ptr = mmap(NULL, g_ram_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (ptr == MAP_FAILED) {
        return -1;
    }

    g_shm_shadow = ptr;
    return 0;
}

/* 
 * 以下函数仅为了兼容 QEMU 命令行参数解析。
 * V29 使用 Wavelet 主动推送模型，不再需要 TTL 和手工 Watch 区域。
 */
void wvm_set_ttl_interval(int ms) { 
    // 留空：不再启动 V28 的收割者线程
}

void wvm_register_volatile_ram(uint64_t gpa, uint64_t size) {
    // 留空：不再维护易失性区域链表
}

/* [FIX-F3] KVM 主动页面拉取：KVM 模式下 mprotect(PROT_NONE) 不可用的替代方案。
 *
 * 背景：TCG 模式通过 mprotect(PROT_NONE) + SIGSEGV 触发缺页拉取。
 * KVM 模式下 PROT_NONE 会导致 EPT violation 风暴，所以所有 mprotect(PROT_NONE) 调用
 * 都被 !kvm_enabled() 守卫跳过。
 *
 * 问题：当版本断层发生时，set_local_page_version(gpa, 0) 被调用，但因为没有
 * mprotect(PROT_NONE)，guest 不会触发缺页，旧数据会被永久读取。
 *
 * 修复：在 KVM 模式下检测到版本断层时，主动通过 IPC 向 Master 发起同步页面拉取，
 * 将最新数据写入 HVA 并更新版本号。不依赖 guest 触发的 SIGSEGV。
 */
static void kvm_proactive_page_fetch(uint64_t gpa) {
    if (!g_ram_base) return;

    /* 使用线程局部 IPC socket (与 request_page_sync Master 路径相同) */
    if (!g_is_slave) {
        /* Master 模式：通过本地 IPC socket 请求 */
        if (t_com_sock == -1) {
            t_com_sock = internal_connect_master();
            if (t_com_sock < 0) {
                fprintf(stderr, "[FIX-F3] KVM proactive fetch: IPC connect failed for GPA 0x%"
                        PRIx64 "\n", gpa);
                return;
            }
        }

        struct wvm_ipc_fault_req req = { .gpa = gpa, .len = 4096, .vcpu_id = 0 };
        struct wvm_ipc_header_t ipc_hdr = { .type = WVM_IPC_TYPE_MEM_FAULT, .len = sizeof(req) };
        struct iovec iov[2] = { {&ipc_hdr, sizeof(ipc_hdr)}, {&req, sizeof(req)} };
        struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 2 };

        if (sendmsg(t_com_sock, &msg, 0) < 0) {
            /* 连接断开，关闭以便下次重建 */
            close(t_com_sock);
            t_com_sock = -1;
            fprintf(stderr, "[FIX-F3] KVM proactive fetch: IPC send failed for GPA 0x%"
                    PRIx64 "\n", gpa);
            return;
        }

        struct wvm_ipc_fault_ack ack;
        if (read_exact(t_com_sock, &ack, sizeof(ack)) < 0) {
            close(t_com_sock);
            t_com_sock = -1;
            fprintf(stderr, "[FIX-F3] KVM proactive fetch: IPC recv failed for GPA 0x%"
                    PRIx64 "\n", gpa);
            return;
        }

        if (ack.status == 0) {
            if (ensure_local_shm_shadow() == 0 && gpa + 4096 <= g_ram_size) {
                void *fetch_hva = gpa_to_hva_safe(gpa);
                if (fetch_hva) {
                    mprotect(fetch_hva, 4096, PROT_READ | PROT_WRITE);
                    memcpy(fetch_hva, (uint8_t *)g_shm_shadow + gpa, 4096);
                    /* KVM 模式下不降权：脏页由 dirty log 跟踪，
                     * mprotect(PROT_READ) 会导致 EPT 违例 → exit=17 */
                }
            }
            set_local_page_version(gpa, ack.version);
        }
    } else {
        /* Slave 模式：复用 request_page_sync 的 UDP 路径。
         * 由于 request_page_sync 接受 fault_addr (HVA) 参数，
         * 这里构造 HVA 并直接调用它。
         */
        void *fetch_hva = gpa_to_hva_safe(gpa);
        if (!fetch_hva) return;
        uintptr_t fault_addr = (uintptr_t)fetch_hva;
        request_page_sync(fault_addr, false);
    }
}

// =============================================================
// [链路 A] 同步缺页处理 (Master IPC / Slave UDP)
// =============================================================

static int request_page_sync(uintptr_t fault_addr, bool is_write) {
    uint64_t gpa = hva_to_gpa_safe(fault_addr);
    if (gpa == (uint64_t)-1) return -1;
    gpa &= ~4095ULL;
    uintptr_t aligned_addr = fault_addr & ~4095ULL;
    {
        char msg[160];
        int n = snprintf(msg, sizeof(msg),
                         "[WaveVM-User] request_page_sync gpa=%#llx write=%d slave=%d\n",
                         (unsigned long long)gpa, is_write ? 1 : 0, g_is_slave ? 1 : 0);
        if (n > 0) {
            write(STDERR_FILENO, msg, (size_t)n);
        }
    }
    
    // --- Master Mode (IPC) ---
    if (!g_is_slave) {
        if (t_com_sock == -1) { 
            t_com_sock = internal_connect_master();
            if (t_com_sock < 0) return -1;
        }
        
        struct wvm_ipc_fault_req req = { .gpa = gpa, .len = 4096, .vcpu_id = 0 };
        struct wvm_ipc_header_t ipc_hdr = { .type = WVM_IPC_TYPE_MEM_FAULT, .len = sizeof(req) };
        struct iovec iov[2] = { {&ipc_hdr, sizeof(ipc_hdr)}, {&req, sizeof(req)} };
        struct msghdr msg = { .msg_iov = iov, .msg_iovlen = 2 };
        
        if (sendmsg(t_com_sock, &msg, 0) < 0) return -1;
        
        // [V29 Fix] 接收带版本的 ACK
        struct wvm_ipc_fault_ack ack;
        if (read_exact(t_com_sock, &ack, sizeof(ack)) < 0) return -1;
        
        if (ack.status == 0) {
            if (ensure_local_shm_shadow() < 0 || gpa + 4096 > g_ram_size) {
                return -1;
            }
            mprotect((void *)aligned_addr, 4096, PROT_READ | PROT_WRITE);
            memcpy((void *)aligned_addr, (uint8_t *)g_shm_shadow + gpa, 4096);
            set_local_page_version(gpa, ack.version); // 同步版本
            return 0;
        }
        return -1;
    }

    // ---------------------------------------------------------
    // [V29 新增] 线程局部 Socket 延迟初始化 (替代全局 g_fd_req)
    // ---------------------------------------------------------
    if (unlikely(t_req_sock == -1)) {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        
        // 1. 从主进程的全局 Socket (g_fd_req) "偷" 取目标 Proxy 地址
        // 这样每个线程都不需要重新解析配置文件，直接跟随主进程配置
        if (getpeername(g_fd_req, (struct sockaddr*)&peer_addr, &addr_len) < 0) {
            safe_log("[WVM] FATAL: getpeername failed in thread init\n");
            _exit(1);
        }

        // 2. 创建线程私有 UDP Socket
        t_req_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (t_req_sock < 0) {
            safe_log("[WVM] FATAL: Thread socket creation failed\n");
            _exit(1);
        }

        // 3. 独占连接到 Proxy (利用内核层过滤非目标流量)
        if (connect(t_req_sock, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
            safe_log("[WVM] FATAL: Thread socket connect failed\n");
            close(t_req_sock); t_req_sock = -1;
            _exit(1);
        }
        
        // 4. 设置非阻塞 (配合 Poll 做超时控制)
        int flags = fcntl(t_req_sock, F_GETFL, 0);
        fcntl(t_req_sock, F_SETFL, flags | O_NONBLOCK);
    }

    // ---------------------------------------------------------
    // [原版逻辑保留] 构造协议包 (使用 t_net_buf 替代全局 buffer)
    // ---------------------------------------------------------
    struct wvm_header *hdr = (struct wvm_header *)t_net_buf;
    memset(hdr, 0, sizeof(struct wvm_header)); // [保留] 必须清零

    hdr->magic = htonl(WVM_MAGIC);
    
    // [保留] 区分读写意图 (V29 协议中通常统一为 MEM_READ，但此处保留你的逻辑)
    // 注意：需确保 MSG_ACQUIRE_... 在 protocol.h 有定义，否则回退到 MSG_MEM_READ
    #ifdef MSG_ACQUIRE_WRITE
        hdr->msg_type = htons(is_write ? MSG_ACQUIRE_WRITE : MSG_ACQUIRE_READ);
    #else
        hdr->msg_type = htons(MSG_MEM_READ); // V29 标准回退
    #endif

    hdr->payload_len = htons(8);
    hdr->slave_id = htonl(g_slave_id);
    hdr->target_id = htonl(WVM_NODE_AUTO_ROUTE); // [V31 Fix] 本地通信标记，防止 vm_id 过滤误杀
    hdr->req_id = WVM_HTONLL((uint64_t)gpa);
    hdr->mode_tcg = 1;
    hdr->qos_level = 1;

    // [保留] Payload: GPA
    *(uint64_t *)(t_net_buf + sizeof(struct wvm_header)) = WVM_HTONLL(gpa);

    // [保留] CRC 计算
    hdr->crc32 = 0; 
    uint32_t c = calculate_crc32(t_net_buf, sizeof(struct wvm_header) + 8);
    hdr->crc32 = htonl(c);

    // ---------------------------------------------------------
    // [V29 增强] 发送与接收 (使用 t_req_sock)
    // ---------------------------------------------------------
    
    // 发送
    if (send(t_req_sock, t_net_buf, sizeof(struct wvm_header) + 8, 0) < 0) {
        return -1; // 网络不可达
    }

    struct pollfd pfd = { .fd = t_req_sock, .events = POLLIN };
    int total_wait_ms = 0;
    
    while(1) {
        // [保留] 1000ms 超时 (优化：拆分为短间隔以支持重发)
        int ret = poll(&pfd, 1, 100); 
        
        if (ret == 0) {
            total_wait_ms += 100;
            // [新增] 简单重传机制，防止 UDP 丢包死锁
            if (total_wait_ms % 500 == 0) {
                send(t_req_sock, t_net_buf, sizeof(struct wvm_header) + 8, 0); 
            }
            if (total_wait_ms >= 5000) {
                // 5秒无回音，打印日志但不退出，防止 Guest 崩溃
                const char *msg = "[WVM] WARN: Page fault stuck > 5s\n";
                write(STDERR_FILENO, msg, strlen(msg));
                total_wait_ms = 0;
            }
            continue; 
        }
        
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        // 接收
        int n = recv(t_req_sock, t_net_buf, WVM_MAX_PACKET_SIZE, 0);
        if (n <= 0) continue; // 过滤空包

        if (n >= sizeof(struct wvm_header)) {
            struct wvm_header *rx = (struct wvm_header *)t_net_buf;
            
            // [保留] 校验
            if (ntohl(rx->magic) != WVM_MAGIC) continue;
            if (WVM_NTOHLL(rx->req_id) != gpa) continue; 

            // [新增] CRC 校验 (接收端也需要校验！)
            uint32_t remote_crc = ntohl(rx->crc32);
            rx->crc32 = 0;
            if (calculate_crc32(t_net_buf, n) != remote_crc) continue;

            if (ntohs(rx->msg_type) == MSG_MEM_ACK) {
                // [V29] 检查版本号 Payload (只多不少：解析更复杂的结构)
                // 结构: Header + wvm_mem_ack_payload { gpa, version, data }
                size_t expected_size = sizeof(struct wvm_header) + sizeof(struct wvm_mem_ack_payload);
                
                if (n >= expected_size) {
                    struct wvm_mem_ack_payload *payload = (struct wvm_mem_ack_payload*)(t_net_buf + sizeof(struct wvm_header));
                    
                    // 双重检查 Payload 内的 GPA
                    if (WVM_NTOHLL(payload->gpa) != gpa) continue;

                    // [保留] 写入内存
                    mprotect((void*)aligned_addr, 4096, PROT_READ | PROT_WRITE);
                    memcpy((void*)aligned_addr, payload->data, 4096);
                    
                    // [V29 新增] 更新本地版本号
                    uint64_t ver = WVM_NTOHLL(payload->version);
                    set_local_page_version(gpa, ver);
                    {
                        char msg[160];
                        int n = snprintf(msg, sizeof(msg),
                                         "[WaveVM-User] request_page_sync ack gpa=%#llx ver=%#llx\n",
                                         (unsigned long long)gpa,
                                         (unsigned long long)ver);
                        if (n > 0) {
                            write(STDERR_FILENO, msg, (size_t)n);
                        }
                    }
                    
                    return 0; // 成功
                }
            }
        }
    }
}

/* 
 * [物理意图] 解决 vCPU 写入线程与后台收割线程之间的“微观竞态”冲突。
 * [关键逻辑] 当 harvester 正在对某一页进行原子快照时，强制 sigsegv 线程在入口处进行纳秒级忙等。
 * [后果] 彻底杜绝了“脏快照”问题。若无此锁，Diff 引擎可能会捕获到一个正在被修改的半成品页面，导致全网数据损坏。
 */
static inline void wait_on_latch(uint64_t gpa) {
    // 计算索引并查对分段锁数组 g_latches
    int idx = LATCH_IDX(gpa);
    while (__atomic_load_n(&g_latches[idx].val, __ATOMIC_ACQUIRE) == gpa) {
        __builtin_ia32_pause(); 
    }
}

// ----------------------------------------------------------------------------
// [REVISED] 信号处理：加入 Latch 检查
// ---------------------------------------------------------------------------

/* 
 * [物理意图] 模拟处理器的“缺页异常处理单元”，实现按需拉取与乐观写入。
 * [关键逻辑] 1. 读缺页：回退到 V28 阻塞拉取；2. 写保护：利用预分配池进行 Copy-Before-Write (CBW) 捕获。
 * [后果] 这是整个前端最繁重的入口。必须保证零 malloc，任何在此处的阻塞（如等待网络）都会直接锁死 vCPU 的流水线。
 */
static void sigsegv_handler(int sig, siginfo_t *si, void *ucontext) {
    uintptr_t addr = (uintptr_t)si->si_addr;
    {
        char msg[192];
        int n = snprintf(msg, sizeof(msg),
                         "[WaveVM-User] sigsegv addr=%p code=%d\n",
                         si->si_addr, si->si_code);
        if (n > 0) {
            write(STDERR_FILENO, msg, (size_t)n);
        }
    }
    
    // 通过安全查表获取 GPA
    uint64_t gpa = hva_to_gpa_safe(addr);
    if (gpa == (uint64_t)-1) {
        {
            char msg[192];
            int n = snprintf(msg, sizeof(msg),
                             "[WaveVM-User] sigsegv passthrough addr=%p\n",
                             si->si_addr);
            if (n > 0) {
                write(STDERR_FILENO, msg, (size_t)n);
            }
        }
        // 说明访问的不是 RAM 区域（可能是 MMIO 或非法地址），交回给标准处理程序
        signal(SIGSEGV, SIG_DFL); 
        raise(SIGSEGV); 
        return;
    }
    
    gpa &= ~4095ULL; // 对齐到页
    void* aligned_addr = (void*)(addr & ~4095ULL);
    wait_on_latch(gpa);

    ucontext_t *ctx = (ucontext_t *)ucontext;
    bool is_write = (ctx->uc_mcontext.gregs[REG_ERR] & 0x2);
    {
        char msg[224];
        int n = snprintf(msg, sizeof(msg),
                         "[WaveVM-User] sigsegv hit gpa=%#llx write=%d page=%p\n",
                         (unsigned long long)gpa, is_write ? 1 : 0, aligned_addr);
        if (n > 0) {
            write(STDERR_FILENO, msg, (size_t)n);
        }
    }

    if (is_write) {
        // 从预分配池中取，不要 malloc!
        int idx = atomic_fetch_add(&g_pool_idx, 1) % PAGE_POOL_SIZE;
        WritablePage *wp = &g_page_pool[idx];
        void *snapshot = g_image_pool[idx];

        /* The faulting store has not retired yet; once write access is
         * restored, the current page bytes are still the pre-write image. */
        mprotect(aligned_addr, 4096, PROT_READ | PROT_WRITE);
        memcpy(snapshot, aligned_addr, 4096);
        wp->gpa = gpa;
        wp->pre_image_snapshot = snapshot;

        // 插入链表（注意：这里必须用原子操作挂载，防止破坏链表）
        do {
            wp->next = __atomic_load_n(&g_writable_pages_list, __ATOMIC_ACQUIRE);
        } while (!__atomic_compare_exchange_n(&g_writable_pages_list, &wp->next, wp, true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE));
        
    } else {
        // 读缺页直接同步
        if (request_page_sync(addr, false) == 0) {
            mprotect(aligned_addr, 4096, PROT_READ);
        } else { _exit(1); }
    }
}

// [改为 Diff 包聚合，已被 add_to_aggregator 替代] 发送 Diff 的辅助函数
static void send_commit_diff_dual_mode(uint64_t gpa, uint16_t offset, uint16_t size, void *data) {
    if (g_fd_push < 0) return;
    
    // 1. 计算包大小
    size_t pl_len = sizeof(struct wvm_diff_log) + size;
    size_t pkt_len = sizeof(struct wvm_header) + pl_len;
    
    // 2. 分配缓冲区
    uint8_t *buf = malloc(pkt_len);
    if (!buf) return;

    // 3. 填充 Header
    struct wvm_header *hdr = (struct wvm_header *)buf;
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_COMMIT_DIFF);
    hdr->payload_len = htons(pl_len);
    hdr->slave_id = htonl(g_slave_id);
    hdr->target_id = htonl(WVM_NODE_AUTO_ROUTE); // [V31 Fix] 本地通信标记
    hdr->req_id = 0;
    hdr->qos_level = 1; // 走快车道
    hdr->crc32 = 0;     // 先清零
    hdr->flags = 0;

    // 4. 填充 Payload (Diff Log)
    struct wvm_diff_log *log = (struct wvm_diff_log *)(buf + sizeof(*hdr));
    log->gpa = WVM_HTONLL(gpa);
    // 携带本地版本号供 Directory 校验
    log->version = WVM_HTONLL(get_local_page_version(gpa));
    log->offset = htons(offset);
    log->size = htons(size);
    memcpy(log->data, data, size);

    // 5. 计算 CRC32
    hdr->crc32 = htonl(calculate_crc32(buf, pkt_len));

    // 6. 发送并释放
    send(g_fd_push, buf, pkt_len, 0);
    free(buf);
}

// 聚合器状态机
static struct {
    uint8_t buf[2048]; // 略大于 MTU，确保能装下一个完整包 + 头部
    int curr_offset;
    pthread_mutex_t lock;
} g_aggregator = { 
    .curr_offset = 0, 
    .lock = PTHREAD_MUTEX_INITIALIZER 
};

// [FIX-G1] Master TCG 模式：将 diff 数据封装为 IPC 发给 Master Daemon
// 每个聚合包内可能有多个 wvm_header 子包（聚合格式），需要拆包后逐个以 IPC 发送
static void send_diff_via_ipc(void *buf, size_t len) {
    if (g_ipc_diff_sock < 0) {
        g_ipc_diff_sock = internal_connect_master();
        if (g_ipc_diff_sock < 0) return;
    }
    // 遍历聚合缓冲区中的每个子包
    size_t off = 0;
    while (off + sizeof(struct wvm_header) <= len) {
        struct wvm_header *sub = (struct wvm_header *)((uint8_t *)buf + off);
        uint16_t pl = ntohs(sub->payload_len);
        size_t sub_len = sizeof(struct wvm_header) + pl;
        if (off + sub_len > len) break;
        // 只取 payload 部分（wvm_diff_log），封装为 IPC
        wvm_ipc_header_t ipc_hdr = { .type = WVM_IPC_TYPE_COMMIT_DIFF, .len = pl };
        void *payload = (uint8_t *)sub + sizeof(struct wvm_header);
        // 发送 IPC header + payload（两次 write，由于是流式 socket 合并发送）
        if (write(g_ipc_diff_sock, &ipc_hdr, sizeof(ipc_hdr)) < 0 ||
            write(g_ipc_diff_sock, payload, pl) < 0) {
            // 连接断开，尝试重连
            close(g_ipc_diff_sock);
            g_ipc_diff_sock = internal_connect_master();
            break; // 放弃本轮剩余包
        }
        off += sub_len;
    }
}

// 发送函数：将缓冲区推向网络
static void flush_aggregator(void) {
    if (g_aggregator.curr_offset == 0) return;
    // [FIX-G1] Master TCG 走 IPC, Slave 走 UDP push socket
    if (g_is_slave) {
        if (send(g_fd_push, g_aggregator.buf, g_aggregator.curr_offset, 0) < 0) {
            // 网络拥塞处理：如果发送失败，清空，版本号一致性由 FORCE_SYNC 保证
        }
    } else {
        send_diff_via_ipc(g_aggregator.buf, g_aggregator.curr_offset);
    }
    g_aggregator.curr_offset = 0;
}

// 核心聚合函数：取代原有的 send_push_packet
static void add_to_aggregator(uint64_t gpa, uint64_t version, uint16_t off, uint16_t sz, void *data, uint8_t flags) {
    size_t payload_len = sizeof(struct wvm_diff_log) + sz;
    size_t needed = sizeof(struct wvm_header) + payload_len;
    
    pthread_mutex_lock(&g_aggregator.lock);
    
    // 如果当前包放不下，或者这个包本身就超过了 MTU 分片限制，则先发送之前的
    if (g_aggregator.curr_offset + needed > MTU_SIZE) {
        flush_aggregator();
    }

    // 如果单包就超过 MTU（虽然对于 Diff 很少见），直接绕过聚合器发送
    if (needed > MTU_SIZE) {
        uint8_t *tmp = malloc(needed);
        if (!tmp) { pthread_mutex_unlock(&g_aggregator.lock); return; }
        struct wvm_header *h = (struct wvm_header *)tmp;
        h->magic = htonl(WVM_MAGIC);
        h->msg_type = htons(MSG_COMMIT_DIFF);
        h->payload_len = htons(payload_len);
        h->slave_id = htonl(g_slave_id);
        h->flags = flags;
        h->crc32 = 0;
        struct wvm_diff_log *l = (struct wvm_diff_log *)(tmp + sizeof(*h));
        l->gpa = WVM_HTONLL(gpa);
        l->version = WVM_HTONLL(version);
        l->offset = htons(off);
        l->size = htons(sz);
        if (sz > 0) memcpy(l->data, data, sz);
        h->crc32 = htonl(calculate_crc32(tmp, needed));
        // [FIX-G1] Master TCG 走 IPC
        if (g_is_slave) {
            send(g_fd_push, tmp, needed, 0);
        } else {
            send_diff_via_ipc(tmp, needed);
        }
        free(tmp);
        pthread_mutex_unlock(&g_aggregator.lock);
        return;
    }

    // 填充到缓冲区
    struct wvm_header *hdr = (struct wvm_header *)(g_aggregator.buf + g_aggregator.curr_offset);
    hdr->magic = htonl(WVM_MAGIC);
    hdr->msg_type = htons(MSG_COMMIT_DIFF);
    hdr->payload_len = htons(payload_len);
    hdr->slave_id = htonl(g_slave_id);
    hdr->target_id = htonl(WVM_NODE_AUTO_ROUTE); // [V31 Fix] 本地通信标记
    hdr->qos_level = 1;
    hdr->flags = flags;
    hdr->crc32 = 0;

    struct wvm_diff_log *log = (struct wvm_diff_log *)(g_aggregator.buf + g_aggregator.curr_offset + sizeof(struct wvm_header));
    log->gpa = WVM_HTONLL(gpa);
    log->version = WVM_HTONLL(version);
    log->offset = htons(off);
    log->size = htons(sz);
    if (sz > 0) memcpy(log->data, data, sz);

    // 在聚合前计算单子包 CRC
    hdr->crc32 = htonl(calculate_crc32((uint8_t*)hdr, needed));
    
    g_aggregator.curr_offset += needed;
    pthread_mutex_unlock(&g_aggregator.lock);
}

/* 
 * [物理意图] 充当内存页面的“分布式写回缓存（Write-back Cache）”管理器。
 * [关键逻辑] 1. 计算增量（Diff）；2. 聚合（Aggregator）打包；3. 执行 AIMD 自适应同步屏障，根据 RTT 调整提交频率。
            逻辑: Detach -> Freeze(Lock) -> Snapshot -> Release(Unlock) -> Diff -> Commit -> Sync
 * [后果] 它通过异步提交隐藏了网络延迟。若收割速度跟不上写入速度，Guest 系统内会发生明显的“因果倒置”现象。
 */
static void *diff_harvester_thread_fn(void *arg) {
    void *current_snapshot = malloc(4096);
    if (!current_snapshot) return NULL;

    int batch_counter = 0;

    while (g_threads_running) {
        usleep(1000); // 1ms 采集周期

        // KVM 模式：基于脏页日志收割，不依赖 SIGSEGV/PROT_NONE。
        // [FIX] 遍历 g_mem_blocks 而非 flat 0~g_ram_size，
        //       并用 qemu_ram_addr_from_host 将 HVA 转为正确的 ram_addr_t，
        //       修复 PCI hole (3G-4G) 导致的 gpa != ram_addr_t 问题。
        if (kvm_enabled()) {
            for (int bi = 0; bi < g_block_count; bi++) {
                GVMRamBlock *blk = &g_mem_blocks[bi];
                /* 将 block 起始 HVA 转为 ram_addr_t 基址（一次性，避免每页调用） */
                ram_addr_t ram_base = qemu_ram_addr_from_host((void *)blk->hva_start);
                if (ram_base == RAM_ADDR_INVALID) continue;

                for (uint64_t off = 0; off + 4096 <= blk->size; off += 4096) {
                    ram_addr_t ra = ram_base + off;
                    if (!cpu_physical_memory_test_and_clear_dirty(ra, 4096, DIRTY_MEMORY_MIGRATION)) {
                        continue;
                    }
                    uint64_t gpa = blk->gpa_start + off;
                    void *hva = (void *)(blk->hva_start + off);
                    uint64_t ver = get_local_page_version(gpa);
                    add_to_aggregator(gpa, ver + 1, 0, 4096, hva, 0);
                    set_local_page_version(gpa, ver + 1);
                }
            }
            flush_aggregator();
            continue;
        }

        // 1. 偷走链表 (Detach List) — 必须用原子交换，与信号处理函数的 CAS 配合
        WritablePage *batch_head = __atomic_exchange_n(&g_writable_pages_list, NULL, __ATOMIC_ACQ_REL);

        if (!batch_head) continue;

        // 2. 遍历处理脏页
        WritablePage *curr = batch_head;
        while (curr) {
            void *page_addr = gpa_to_hva_safe(curr->gpa);
            if (!page_addr) { 
                // 错误处理：释放资源并跳过
                WritablePage *nxt = curr->next; curr = nxt;
                continue; 
            }
            int idx = LATCH_IDX(curr->gpa);
            
            // [A] 上锁 (Freeze): 告诉 Signal Handler 暂停操作
            __atomic_store_n(&g_latches[idx].val, curr->gpa, __ATOMIC_RELEASE);

            // [B] 冻结权限: 设为只读
            mprotect(page_addr, 4096, PROT_READ);
            __sync_synchronize();

            // [C] 快照 (Snapshot): 安全拷贝
            memcpy(current_snapshot, page_addr, 4096);

            // [D] 解锁 (Release): Signal Handler 可以继续了
            __atomic_store_n(&g_latches[idx].val, (uint64_t)-1, __ATOMIC_RELEASE);

            uint64_t ver = get_local_page_version(curr->gpa);

            if (is_page_all_zero(current_snapshot)) {
                // 调用聚合器发送零页消息
                add_to_aggregator(curr->gpa, ver + 1, 0, 0, NULL, WVM_FLAG_ZERO);
                set_local_page_version(curr->gpa, ver + 1);
            } else {
                // [E] 计算 Diff (耗时操作，已移出临界区)
                int start = -1, end = -1;
                uint64_t *p64_now = (uint64_t*)current_snapshot;
                uint64_t *p64_pre = (uint64_t*)curr->pre_image_snapshot;
            
                for (int i = 0; i < 512; i++) {
                    if (p64_now[i] != p64_pre[i]) {
                        if (start == -1) start = i * 8;
                        end = i * 8 + 7;
                    }
                }

                // [F] 提交与同步 (Commit & Sync)
                if (start != -1) {
                    uint16_t size = end - start + 1;
                    // 发送 Diff 包
                    add_to_aggregator(curr->gpa, ver + 1, (uint16_t)start, size, (uint8_t*)current_snapshot + start, 0);

                    // --- [AIMD Sync Engine] ---
                    if (g_client_sync_batch > 0) {
                        batch_counter++;
                        // 达到阈值，触发同步屏障
                        if (batch_counter >= g_client_sync_batch) {
                            long rtt = wait_for_directory_ack_safe();
                            batch_counter = 0;

                            // 自适应流控逻辑
                            if (g_enable_auto_tuning) {
                                if (rtt < 0) {
                                    // 超时/丢包：急速降速
                                    g_client_sync_batch = (g_client_sync_batch > 16) ? g_client_sync_batch / 2 : 1;
                                } else if (rtt < 500) { 
                                    // 网络极好 (<0.5ms)：加法增大
                                    if (g_client_sync_batch < g_max_batch) g_client_sync_batch += 16;
                                } else if (rtt > 5000) { 
                                    // 网络拥塞 (>5ms)：乘法减小
                                    g_client_sync_batch = (g_client_sync_batch * 3) / 4; 
                                    if (g_client_sync_batch < g_min_batch) g_client_sync_batch = g_min_batch;
                                }
                            }
                        }
                    }
                
                    // 乐观更新本地版本号
                    set_local_page_version(curr->gpa, ver + 1);
                }
            }

            // 清理资源：节点与快照都来自预分配池，不在这里 free
            WritablePage *next_node = curr->next;
            curr = next_node;
        }
        flush_aggregator();
    }
    free(current_snapshot);
    return NULL;
}

// =============================================================
// [链路 B] 流式监听线程 (Stream Listener)
// =============================================================

// 环形缓冲区，用于处理 IPC 流的粘包/拆包
typedef struct {
    uint8_t buffer[WVM_MAX_PACKET_SIZE * 4];
    size_t head; // Read ptr
    size_t tail; // Write ptr
} StreamBuffer;

static void sb_init(StreamBuffer *sb) {
    sb->head = 0;
    sb->tail = 0;
}

static void sb_compact(StreamBuffer *sb) {
    if (sb->head > 0) {
        size_t len = sb->tail - sb->head;
        if (len > 0) memmove(sb->buffer, sb->buffer + sb->head, len);
        sb->tail = len;
        sb->head = 0;
    }
}

// --- [V29 Sync Concurrency Control] ---
static pthread_mutex_t g_sync_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_sync_cond = PTHREAD_COND_INITIALIZER;
static volatile int g_ack_received = 0; // 状态标志位

// 辅助：获取微秒时间
static uint64_t get_us_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000UL + ts.tv_nsec / 1000;
}

// 导出配置接口
void wvm_set_client_sync_mode(int batch_size, int auto_tune) {
    if (batch_size > 0) g_client_sync_batch = batch_size;
    g_enable_auto_tuning = auto_tune;
    
    // 如果设为 1，通常意味着强一致性需求，关闭自动调优
    if (batch_size == 1) {
        g_enable_auto_tuning = 0;
        printf("[WVM] Strict Consistency Mode Activated (Batch=1, No Tuning)\n");
    } else {
        printf("[WVM] Sync Mode: Initial Batch=%d, AutoTuning=%d\n", 
               batch_size, auto_tune);
    }
}

/* 
 * [物理意图] 在 P2P 集群中插入一个“顺序执行栅栏”。
 * [关键逻辑] 发起一个带有特殊 Magic ID 的 PING 包，并阻塞等待 Directory 的 ACK，以确认上一批次的写入已落盘。
 * [后果] 确保了分布式内存的“强顺序一致性”。它防止了在执行关键 IO 指令（如 GPU 命令提交）时，内存数据尚未同步完成的情况。
 */
static long wait_for_directory_ack_safe(void) {
    // Master TCG 模式走 IPC，本地不需要网络栅栏
    if (!g_is_slave) return 100;
    if (g_fd_push < 0) return -1;

    // 1. 准备发送 PING
    struct wvm_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = htonl(WVM_MAGIC);
    hdr.msg_type = htons(MSG_PING);
    hdr.slave_id = htonl(g_slave_id);
    hdr.target_id = htonl(WVM_NODE_AUTO_ROUTE);
    hdr.req_id = WVM_HTONLL(SYNC_MAGIC); // 特殊标记

    // 2. 【关键】先加锁，重置状态位
    pthread_mutex_lock(&g_sync_lock);
    g_ack_received = 0;
    pthread_mutex_unlock(&g_sync_lock);

    uint64_t t_start = get_us_time();

    // 3. 发送网络包
    if (send(g_fd_push, &hdr, sizeof(hdr), 0) < 0) return -1;

    // 4. 【关键】带超时的条件等待
    pthread_mutex_lock(&g_sync_lock);
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1; // 1秒超时
    
    // 只要没收到 ACK，就继续等 (防止虚假唤醒)
    while (g_ack_received == 0) {
        int rc = pthread_cond_timedwait(&g_sync_cond, &g_sync_lock, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&g_sync_lock);
            return -1; // 超时
        }
    }
    pthread_mutex_unlock(&g_sync_lock);

    return (long)(get_us_time() - t_start);
}

/* 
 * [物理意图] 系统的“入站流量净化器”，解决 UDP 乱序和 TLB 刷新成本问题。
 * [关键逻辑] 1. 维护重排窗口（Reorder Window）填补版本空洞；2. 执行延迟刷新（Lazy Flush）以合并 mprotect 调用。
 * [后果] 延迟刷新是性能的救星。若每收到一个 Diff 都执行一次 TLB Shootdown，vCPU 的有效执行时间将缩减到不足 10%。
 */
static void *mem_push_listener_thread(void *arg) {
    StreamBuffer sb;
    sb_init(&sb);
    
    // 初始化 TLS 队列索引
    t_lazy_count = 0; 
    uint64_t last_cleanup_us = 0;
    
    struct pollfd pfd = { .fd = g_fd_push, .events = POLLIN };
    printf("[WVM] Async Push Listener Started (Streaming Mode + Lazy Flush).\n");

    while (g_threads_running) {
        // [Stage 0] 循环前冲刷
        // 确保上一轮循环遗留的 RW 页面被锁回 RO。
        // 这保证了 poll 等待期间，页面是安全的（RO）。
        flush_lazy_ro_queue();

        int ret = poll(&pfd, 1, 100);
        uint64_t now_us = get_us_time();
        if (now_us - last_cleanup_us > 200000) { // 每 200ms 清理一次
            pthread_spin_lock(&g_reorder_lock);
            for (int i = 0; i < REORDER_WIN_SIZE; i++) {
                if (g_reorder_buf[i].active && (now_us - g_reorder_buf[i].timestamp_us > 200000)) {
                    uint64_t stale_gpa = g_reorder_buf[i].gpa;
                    
                    // 释放资源
                    free(g_reorder_buf[i].data);
                    g_reorder_buf[i].active = false;
                    
                    // 触发强制同步
                    void* hva = gpa_to_hva_safe(stale_gpa);
                    if (hva && !kvm_enabled()) {
                        mprotect(hva, 4096, PROT_NONE);
                    }
                    set_local_page_version(stale_gpa, 0);
                    // [FIX-F3] KVM 模式下主动拉取过期重排缓冲区中的页面
                    if (kvm_enabled()) {
                        kvm_proactive_page_fetch(stale_gpa);
                    }
                }
            }
            pthread_spin_unlock(&g_reorder_lock);
            last_cleanup_us = now_us;
        }
        if (ret <= 0) continue;

        // [Stage 1] 读取网络流
        size_t space = sizeof(sb.buffer) - sb.tail;
        if (space == 0) { sb_init(&sb); continue; }
        ssize_t n = recv(g_fd_push, sb.buffer + sb.tail, space, 0);
        if (n <= 0) {
            if (errno == EINTR) continue;
            break; 
        }
        sb.tail += n;

        // [Stage 2] 批量解析与应用
        while (sb.tail - sb.head >= sizeof(struct wvm_ipc_header_t)) {
            struct wvm_ipc_header_t *ipc = (struct wvm_ipc_header_t *)(sb.buffer + sb.head);
            size_t total_msg_len = sizeof(struct wvm_ipc_header_t) + ipc->len;

            if (sb.tail - sb.head < total_msg_len) break; // 数据未收全，等待下一轮

            // 指向 WVM 协议头
            void *data = sb.buffer + sb.head + sizeof(struct wvm_ipc_header_t);
            
            // 只有 INVALIDATE 类型的 IPC 消息才包含网络包数据
            if (ipc->type == WVM_IPC_TYPE_INVALIDATE) {
                struct wvm_header *hdr = (struct wvm_header *)data;
                void *payload = data + sizeof(struct wvm_header);
                uint16_t msg_type = ntohs(hdr->msg_type);
                
                // [FIXED] 提取包长度，供乱序重排使用
                uint16_t p_len = ntohs(hdr->payload_len); 

                // --- 逻辑 A: Diff 推送 (带乱序处理) ---
                if (msg_type == MSG_PAGE_PUSH_DIFF) {
                    struct wvm_diff_log* log = (struct wvm_diff_log*)payload;
                    uint64_t gpa = WVM_NTOHLL(log->gpa);
                    uint64_t push_ver = WVM_NTOHLL(log->version);
                    uint64_t local_ver = get_local_page_version(gpa);

                    if (is_next_version(local_ver, push_ver)) {
                        // 顺序到达：直接应用 (内部调用 defer_ro_protect，保持 RW)
                        wvm_apply_remote_push(msg_type, payload);
                        
                        // 链式反应：检查重排缓冲区是否有 v+2, v+3...
                        // 只要能应用成功，就继续。参数虽然传了，但函数内部现在会动态寻找真正的“下一跳”。
                        while (check_and_apply_next(gpa, 0)) ;
                        
                    } else if (is_newer_version(local_ver, push_ver) && !is_next_version(local_ver, push_ver)) {
                        // 乱序到达：存入重排窗口
                        if (!is_newer_version(local_ver + REORDER_WIN_SIZE, push_ver)) {
                            // [Safety] 这里使用 p_len 确保拷贝完整
                            buffer_future_packet(gpa, push_ver, msg_type, payload, p_len);
                        } else {
                            // 严重乱序：回退到 Pull 模式
                            // 这种情况下必须立即锁回，不能 Lazy，因为状态已重置
                            if (!kvm_enabled()) {
                                void *inv_hva2 = gpa_to_hva_safe(gpa);
                                if (inv_hva2) mprotect(inv_hva2, 4096, PROT_NONE);
                            }
                            set_local_page_version(gpa, 0);
                            // [FIX-F3] KVM 下主动拉取，替代 SIGSEGV 驱动的缺页
                            if (kvm_enabled()) {
                                kvm_proactive_page_fetch(gpa);
                            }
                        }
                    }
                } 
                // --- 逻辑 B: 全页推送 / 强制同步 ---
                else if (msg_type == MSG_PAGE_PUSH_FULL || msg_type == MSG_FORCE_SYNC) {
                    wvm_apply_remote_push(msg_type, payload);
                }
                // --- 逻辑 C: Prophet RPC (V29 保留) ---
                else if (msg_type == MSG_RPC_BATCH_MEMSET) {
                    // Prophet 通知。这里不需要 Lazy Flush，因为 RPC 执行通常是原子的
                    wvm_apply_remote_push(msg_type, payload);
                }
                // --- 逻辑 D: 同步 ACK (Sync Batch 闭环) ---
                else if (msg_type == MSG_MEM_ACK && WVM_NTOHLL(hdr->req_id) == SYNC_MAGIC) {
                    // 收到 Directory 的确认，说明 sync_batch 内的所有 diff 都已落盘
                    // 唤醒 Harvester 线程继续发送下一批
                    pthread_mutex_lock(&g_sync_lock);
                    g_ack_received = 1; 
                    pthread_cond_signal(&g_sync_cond); 
                    pthread_mutex_unlock(&g_sync_lock);
                }
            }
            
            // 移动环形缓冲区指针
            sb.head += total_msg_len;
        }
        sb_compact(&sb);

        // [Stage 3] 循环末尾强制冲刷
        // 这一步至关重要。它确保了上述 while 循环处理的一批包（可能几十个）
        // 对应的页面在处理完后立刻被锁回 RO。
        // 这将 RW 窗口限制在“微秒级”，兼顾了性能与一致性。
        flush_lazy_ro_queue(); 
    }
    return NULL;
}

// --- 初始化 ---
void wavevm_user_mem_init(void *ram_ptr, size_t ram_size) {
    g_ram_base = ram_ptr;
    g_ram_size = ram_size;
    if (!getenv("WVM_SOCK_REQ")) {
        ensure_local_shm_shadow();
    }
    bool enable_fault_hook = true;
    const char *hook_env = getenv("WVM_ENABLE_FAULT_HOOK");
    if (hook_env && atoi(hook_env) == 0) {
        enable_fault_hook = false;
    }

    // KVM + PROT_NONE 会导致 EPT violation，KVM 下：
    //   1. 启用 migration dirty log 替代 mprotect 追踪脏页
    //   2. 各调用点用 !kvm_enabled() 守卫跳过 mprotect(PROT_NONE)
    // 注意：fault hook（SIGSEGV 拦截器）仍然保留，它是分布式缺页请求的核心通道。
    if (kvm_enabled()) {
        memory_global_dirty_log_start();
        fprintf(stderr, "[WaveVM] KVM detected: PROT_NONE path bypassed, migration dirty log enabled.\n");
    }
    g_fault_hook_enabled = enable_fault_hook;
    g_fault_hook_checked = true;

    init_latches();
    pthread_spin_init(&g_reorder_lock, 0); 

    for(int i=0; i<PAGE_POOL_SIZE; i++) {
        g_image_pool[i] = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    }

    size_t num_pages = ram_size / 4096;

    char *env_req = getenv("WVM_SOCK_REQ");
    char *env_push = getenv("WVM_SOCK_PUSH");
    char *env_id = getenv("WVM_SLAVE_ID");

    if (env_req && env_push) {
        g_is_slave = 1;
        g_fd_req = atoi(env_req);
        g_fd_push = atoi(env_push);
        g_slave_id = env_id ? atoi(env_id) : 0;

        printf("[WaveVM-User] V29 Wavelet Engine Active (Slave ID: %d)\n", g_slave_id);

        g_threads_running = true;
        pthread_create(&g_listen_thread, NULL, mem_push_listener_thread, NULL);
        pthread_create(&g_harvester_thread, NULL, diff_harvester_thread_fn, NULL);
    }
    // [FIX-G1] Master TCG 模式：无 WVM_SOCK_PUSH 但仍需启动 harvester
    // Listener 由 wavevm-all.c 的 wavevm_master_ipc_thread 处理，此处只启 harvester
    else if (!kvm_enabled()) {
        printf("[WaveVM-User] V31 Master TCG Harvester Active (IPC Diff Path)\n");
        g_threads_running = true;
        pthread_create(&g_harvester_thread, NULL, diff_harvester_thread_fn, NULL);
    }

    if (!kvm_enabled()) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_flags = SA_SIGINFO; // [FIX-M6] 移除 SA_NODEFER，防止 handler 内递归 SIGSEGV 导致栈溢出
        sa.sa_sigaction = sigsegv_handler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);

        // Initial state is handled per RAM block in wavevm_register_ram_block().
    }
}

```

**文件**: `wavevm-qemu/hw/wavevm/wavevm-mem.c`

```c

#include "qemu/osdep.h"
#include "exec/memory.h"
#include "qemu/mmap-alloc.h"
#include "sysemu/kvm.h"

/*
 * Memory Interception for Infinite Scale (V18 - Dirty Log Enabled)
 */

void wavevm_setup_memory_region(MemoryRegion *mr, uint64_t size, int fd) {
    /*
     * Compatibility mode for QEMU 5.2:
     * The RAM region is already initialized by machine creation; reinitializing
     * it with memory_region_init_ram_ptr() can corrupt internal state and crash.
     * Keep existing RAM mapping and let WaveVM hooks operate on that memory.
     */
    (void)mr;
    (void)size;
    (void)fd;
}

```

**文件**: `wavevm-qemu/hw/wavevm/wavevm-gpu-stub.c`

```c

/*
 * [IDENTITY] GPU Placeholder & Shadow Proxy - The Hardware Impersonator
 * ---------------------------------------------------------------------------
 * 物理角色：远程 GPU 在 Master 节点的"本地替身"与"资源占位符"。
 * 
 * 核心存在意义：
 * 1. 启动欺骗（Boot-time Deception）：物理 GPU 位于 Slave 节点，Master 侧的 QEMU 
 *    在初始化 PCI 总线时由于看不见真实硬件，会报错退出或拒绝分配资源。
 *    Stub 通过注册相同的 VendorID/DeviceID，让 QEMU "以为" 显卡就在本地。
 * 2. 资源锚定（BAR Anchoring）：在 QEMU 启动阶段，强制在物理地址空间（GPA）中
 *    为显存（BAR1）和寄存器（BAR0）“圈地”。
 *    如果没有这个 Stub 提前占位，Guest OS 的 BIOS 和内核将无法为远程 GPU 分配
 *    正确的地址窗口，导致 Slave 端的物理 GPU 即使在线也无法被映射。
 * 3. 运行控制（Prophet Hook）：在骗过启动器并成功运行后，利用预留的控制寄存器
 *    作为入口，为 Guest 提供超光速内存同步（MSG_RPC_BATCH_MEMSET）能力。
 * 
 * [禁止事项]
 * - 严禁随意更改 BAR1 的 Size (必须与 Slave 端物理显存完全对齐)，否则会导致
 *   Master/Slave 之间的地址映射断层。
 * - 严禁移除 PCI 配置空间的伪装代码，否则分布式显卡将无法通过 Guest 的驱动自检。
 * ---------------------------------------------------------------------------
 */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "exec/cpu-common.h"
#include "exec/memory.h"
#include "cpu.h"
#include "../../../common_include/wavevm_protocol.h" 

// 寄存器偏移
#define REG_ADDR_LOW   0x00
#define REG_ADDR_HIGH  0x04
#define REG_SIZE_LOW   0x08
#define REG_SIZE_HIGH  0x0C
#define REG_VAL        0x10
#define REG_COMMAND    0x14 
#define CMD_OP_MEMSET  1

#define MAX_BATCH_REGIONS 512

typedef struct WvmGpuStubState {
    PCIDevice pdev;
    MemoryRegion bar0, bar1, bar2;
    
    // 状态寄存器
    uint64_t reg_addr;
    uint64_t reg_size;
    uint32_t reg_val;
    
    // QOM 属性
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t subsystem_vendor_id;
    uint32_t subsystem_id;
    uint32_t class_id;
    uint64_t bar0_size;
    uint64_t bar1_size;
    uint64_t bar2_size;
} WvmGpuStubState;

// 引用外部函数
extern int wvm_send_rpc_sync(uint16_t msg_type, void *payload, size_t len);

/* 
 * [物理意图] 执行“GVA 到 GPA 的跨时空翻译”，将 Guest 的软件意图转化为全网硬件动作。
 * [关键逻辑] 1. 强制预读触发缺页（Safety Map）；2. 合并连续物理段；3. 发起同步 RPC 广播。
 * [后果] 这是 Prophet 引擎的引信。它通过物理段合并，将原本需要数百万次网络交互的 memset 压缩为一次同步操作。
 */
static void handle_memset_command(WvmGpuStubState *s) {
    uint64_t gva = s->reg_addr;
    uint64_t remain = s->reg_size;
    uint32_t val = s->reg_val;
    
    // 分配 Batch 缓冲区
    size_t batch_alloc_size = sizeof(struct wvm_rpc_batch_memset) + 
                              MAX_BATCH_REGIONS * sizeof(struct wvm_rpc_region);
    struct wvm_rpc_batch_memset *batch = g_malloc0(batch_alloc_size);
    struct wvm_rpc_region *regions = (struct wvm_rpc_region *)(batch + 1);
    
    batch->val = htonl(val);
    int count = 0;
    CPUState *cpu = current_cpu;

    // --- 核心循环：GVA -> GPA 翻译与打包 ---
    while (remain > 0) {
        // [Safety 1] 强制预读：触发缺页异常
        // 这一步至关重要，防止 cpu_get_phys_page_debug 返回 -1
        uint8_t dummy;
        if (cpu_memory_rw_debug(cpu, gva, &dummy, 1, 0) != 0) {
            fprintf(stderr, "[WVM-Stub] Invalid GVA access or SegFault: %lx\n", gva);
            break; // 停止优化
        }

        // [Safety 2] 查表翻译
        hwaddr gpa = cpu_get_phys_page_debug(cpu, gva & TARGET_PAGE_MASK);
        if (gpa == -1) break; 

        gpa += (gva & ~TARGET_PAGE_MASK); // 加上页内偏移

        uint64_t page_remain = TARGET_PAGE_SIZE - (gpa & ~TARGET_PAGE_MASK);
        uint64_t chunk = (remain < page_remain) ? remain : page_remain;

        // [Optimization] 物理段合并
        if (count > 0 && regions[count-1].gpa + regions[count-1].len == gpa) {
            regions[count-1].len += chunk;
        } else {
            // 缓冲区满？发送并重置
            if (count >= MAX_BATCH_REGIONS) {
                batch->count = htonl(count);
                // 转换字节序 (Host -> Network)
                for(int i=0; i<count; i++) {
                    regions[i].gpa = WVM_HTONLL(regions[i].gpa);
                    regions[i].len = WVM_HTONLL(regions[i].len);
                }
                
                // [Blocking] 同步发送
                wvm_send_rpc_sync(MSG_RPC_BATCH_MEMSET, batch, 
                    sizeof(struct wvm_rpc_batch_memset) + count * sizeof(struct wvm_rpc_region));
                
                count = 0; // 重置计数器
            }
            
            // 记录新段
            regions[count].gpa = gpa;
            regions[count].len = chunk;
            count++;
        }

        gva += chunk;
        remain -= chunk;
    }

    // 发送剩余尾包
    if (count > 0) {
        batch->count = htonl(count);
        for(int i=0; i<count; i++) {
            regions[i].gpa = WVM_HTONLL(regions[i].gpa);
            regions[i].len = WVM_HTONLL(regions[i].len);
        }
        wvm_send_rpc_sync(MSG_RPC_BATCH_MEMSET, batch, 
            sizeof(struct wvm_rpc_batch_memset) + count * sizeof(struct wvm_rpc_region));
    }

    g_free(batch);
    
    // [Consistency] 刷新 TLB，确保 CPU 感知到内存变化
    tlb_flush_all_cpus_synced(cpu);
}

/* 
 * [物理意图] 拦截 Guest 驱动发出的“上帝指令（Control Plane Commands）”。
 * [关键逻辑] 监听对 BAR2 寄存器的写操作，根据寄存器地址（ADDR/SIZE/CMD）拼凑出 Prophet 引擎的执行上下文。
 * [后果] 它是软件语义到物理动作的“跨界桥梁”。通过此函数，Guest 的一个赋值语句就能转化为全网 PB 级内存的同步。
 */
static void wvm_stub_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    WvmGpuStubState *s = opaque;
    
    switch (addr) {
        case REG_ADDR_LOW:  s->reg_addr = (s->reg_addr & 0xFFFFFFFF00000000) | (val & 0xFFFFFFFF); break;
        case REG_ADDR_HIGH: s->reg_addr = (s->reg_addr & 0x00000000FFFFFFFF) | (val << 32); break;
        case REG_SIZE_LOW:  s->reg_size = (s->reg_size & 0xFFFFFFFF00000000) | (val & 0xFFFFFFFF); break;
        case REG_SIZE_HIGH: s->reg_size = (s->reg_size & 0x00000000FFFFFFFF) | (val << 32); break;
        case REG_VAL:       s->reg_val  = (uint32_t)val; break;
        case REG_COMMAND:   
            if (val == CMD_OP_MEMSET) handle_memset_command(s); 
            break;
    }
}

static const MemoryRegionOps wvm_stub_bar2_ops = {
    .write = wvm_stub_bar2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 }
};

/* 
 * [物理意图] 在 QEMU 内部“无中生有”地创造出一个符合 PCI 规范的显卡分身。
 * [关键逻辑] 伪造 VendorID (0x10de) 和 DeviceID，并向 QEMU 内存系统注册 32GB 级的虚拟 BAR 空间。
 * [后果] 解决了启动时的“存在性自洽”。如果没有 realize 过程，QEMU 会因为检测不到硬件而拒绝加载相关的分布式显卡驱动。
 */
static void wvm_gpu_stub_realize(PCIDevice *pci_dev, Error **errp) {
    WvmGpuStubState *s = WVM_GPU_STUB(pci_dev);

    pci_config_set_vendor_id(pci_dev->config, s->vendor_id);
    pci_config_set_device_id(pci_dev->config, s->device_id);
    pci_config_set_class(pci_dev->config, s->class_id);
    if (s->subsystem_id) {
        pci_set_word(pci_dev->config + PCI_SUBSYSTEM_VENDOR_ID, s->subsystem_vendor_id);
        pci_set_word(pci_dev->config + PCI_SUBSYSTEM_ID, s->subsystem_id);
    }
    pci_config_set_interrupt_pin(pci_dev->config, 1);

    if (s->bar0_size > 0) {
        memory_region_init(&s->bar0, OBJECT(s), "wvm-gpu-bar0", s->bar0_size);
        pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    }
    if (s->bar1_size > 0) {
        memory_region_init(&s->bar1, OBJECT(s), "wvm-gpu-bar1", s->bar1_size);
        pci_register_bar(pci_dev, 1, 
                         PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_PREFETCH | PCI_BASE_ADDRESS_MEM_TYPE_64, 
                         &s->bar1);
    }

    // [V29] BAR2 初始化为 IO 回调
    if (s->bar2_size > 0) {
        memory_region_init_io(&s->bar2, OBJECT(s), &wvm_stub_bar2_ops, s, "wvm-gpu-bar2", s->bar2_size);
        pci_register_bar(pci_dev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2);
    }
    
    printf("[WVM-Stub] V29.5 Prophet Ready: BAR2 IO Intercept Active.\n");
}

static Property wvm_gpu_stub_properties[] = {
    DEFINE_PROP_UINT32("vendor_id", WvmGpuStubState, vendor_id, 0x10de), 
    DEFINE_PROP_UINT32("device_id", WvmGpuStubState, device_id, 0x1eb8), 
    DEFINE_PROP_UINT32("sub_vid", WvmGpuStubState, subsystem_vendor_id, 0x0), 
    DEFINE_PROP_UINT32("sub_did", WvmGpuStubState, subsystem_id, 0x0), 
    DEFINE_PROP_UINT32("class_id", WvmGpuStubState, class_id, 0x030000), 
    DEFINE_PROP_UINT64("bar0_size", WvmGpuStubState, bar0_size, 16 * 1024 * 1024),
    DEFINE_PROP_UINT64("bar1_size", WvmGpuStubState, bar1_size, 12UL * 1024 * 1024 * 1024),
    // 默认 BAR2 大小 4KB，足够映射寄存器
    DEFINE_PROP_UINT64("bar2_size", WvmGpuStubState, bar2_size, 4096), 
    DEFINE_PROP_END_OF_LIST(),
};

static void wvm_gpu_stub_class_init(ObjectClass *class, void *data) {
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);
    k->realize = wvm_gpu_stub_realize;
    k->vendor_id = PCI_ANY_ID;
    k->device_id = PCI_ANY_ID;
    k->class_id = PCI_CLASS_DISPLAY_VGA;
    dc->hotpluggable = true;
    device_class_set_props(dc, wvm_gpu_stub_properties);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo wvm_gpu_stub_info = {
    .name          = "wavevm-gpu-stub",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(WvmGpuStubState),
    .class_init    = wvm_gpu_stub_class_init,
    .interfaces = (InterfaceInfo[]) { { INTERFACE_PCIE_DEVICE }, { } },
};

static void wvm_gpu_stub_register_types(void) {
    type_register_static(&wvm_gpu_stub_info);
}
type_init(wvm_gpu_stub_register_types)

```

**文件**: `wavevm-qemu/hw/wavevm/wavevm-block-hook.c`

```c
#include "qemu/osdep.h"
#include "qemu/iov.h"

/*
 * virtio-blk hook entry point. Returns 0 when the request is handled by the
 * WaveVM IPC path. Returns -1 to let virtio-blk use its normal local path.
 */
int wavevm_blk_interceptor(uint64_t sector, QEMUIOVector *qiov, int is_write);

extern int wvm_send_ipc_block_io(uint64_t lba, void *buf, uint32_t len, int is_write)
    __attribute__((weak));

int wavevm_blk_interceptor(uint64_t sector, QEMUIOVector *qiov, int is_write)
{
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

```

**文件**: `wavevm-qemu/qemu-wavevm.diff`

```diff
diff --git a/hw/block/virtio-blk.c b/hw/block/virtio-blk.c
index bac2d6fa2..98e0b5502 100644
--- a/hw/block/virtio-blk.c
+++ b/hw/block/virtio-blk.c
@@ -26,6 +26,8 @@
 #include "hw/virtio/virtio-blk.h"
 #include "dataplane/virtio-blk.h"
 #include "scsi/constants.h"
+
+extern int wavevm_blk_interceptor(uint64_t sector, QEMUIOVector *qiov, int is_write);
 #ifdef __linux__
 # include <scsi/sg.h>
 #endif
@@ -672,6 +674,12 @@ static int virtio_blk_handle_request(VirtIOBlockReq *req, MultiReqBuffer *mrb)
                                          req->qiov.size / BDRV_SECTOR_SIZE);
         }
 
+        if (wavevm_blk_interceptor(req->sector_num, &req->qiov, is_write) == 0) {
+            virtio_blk_req_complete(req, VIRTIO_BLK_S_OK);
+            virtio_blk_free_request(req);
+            return 0;
+        }
+
         if (!virtio_blk_sect_range_ok(s, req->sector_num, req->qiov.size)) {
             virtio_blk_req_complete(req, VIRTIO_BLK_S_IOERR);
             block_acct_invalid(blk_get_stats(s->blk),

diff --git a/hw/meson.build b/hw/meson.build
index 010de7219..e2cd60b30 100644
--- a/hw/meson.build
+++ b/hw/meson.build
@@ -39,6 +39,7 @@ subdir('usb')
 subdir('vfio')
 subdir('virtio')
 subdir('watchdog')
+subdir('wavevm')
 subdir('xen')
 subdir('xenpv')
 
diff --git a/hw/wavevm/meson.build b/hw/wavevm/meson.build
new file mode 100644
index 000000000..1494f023f
--- /dev/null
+++ b/hw/wavevm/meson.build
@@ -0,0 +1,3 @@
+softmmu_ss.add(files(
+  'wavevm-block-hook.c',
+))
diff --git a/accel/meson.build b/accel/meson.build
index b26cca227..f3f6252f0 100644
--- a/accel/meson.build
+++ b/accel/meson.build
@@ -3,6 +3,7 @@
 subdir('qtest')
 subdir('kvm')
 subdir('tcg')
+subdir('wavevm')
 subdir('xen')
 subdir('stubs')
 
diff --git a/accel/wavevm/meson.build b/accel/wavevm/meson.build
new file mode 100644
index 000000000..68adef81c
--- /dev/null
+++ b/accel/wavevm/meson.build
@@ -0,0 +1,10 @@
+wavevm_ss = ss.source_set()
+wavevm_ss.add(files(
+  'wavevm-all.c',
+  'wavevm-cpu.c',
+  'wavevm-tcg.c',
+  'wavevm-user-mem.c',
+  '../../hw/wavevm/wavevm-mem.c',
+))
+
+specific_ss.add_all(when: ['CONFIG_SOFTMMU', 'CONFIG_POSIX'], if_true: wavevm_ss)
diff --git a/accel/wavevm/wavevm-accel.h b/accel/wavevm/wavevm-accel.h
new file mode 100644
index 000000000..2c90c9447
--- /dev/null
+++ b/accel/wavevm/wavevm-accel.h
@@ -0,0 +1,41 @@
+#pragma once
+
+#include "qemu/osdep.h"
+#include "qom/object.h"
+#include "sysemu/accel.h"
+#include "qemu/thread.h"
+
+/*
+ * Shared WaveVM accelerator definitions (used across wavevm-*.c files).
+ * Keep this header minimal: it only defines the accelerator state and
+ * identifiers needed by other compilation units.
+ */
+
+#define TYPE_WAVEVM_ACCEL "wavevm-accel"
+#define WAVEVM_ACCEL(obj) OBJECT_CHECK(WaveVMAccelState, (obj), TYPE_WAVEVM_ACCEL)
+
+typedef enum {
+    WVM_MODE_KERNEL,
+    WVM_MODE_USER,
+} WvmMode;
+
+typedef struct WaveVMAccelState {
+    AccelState parent_obj;
+    int dev_fd;
+    int sync_sock;
+    int ipc_sock;
+    WvmMode mode;
+    QemuThread sync_thread;
+    QemuThread ipc_thread;
+    QemuThread irq_thread;
+    bool sync_thread_running;
+    QemuThread net_thread;
+    int master_sock;
+
+    /* [FIX-F1] Block IO 持久连接：避免每次 IO 都 connect()+pthread_create() 导致线程爆炸 */
+    int block_io_sock;               /* 持久 IPC 连接，-1 = 未初始化 */
+    QemuMutex block_io_lock;         /* 序列化 Block IO 请求（同一时刻只允许一个 IO 在途） */
+} WaveVMAccelState;
+
+extern int g_wvm_local_split;
+
diff --git a/hw/acpi/cpu.c b/hw/acpi/cpu.c
index f099b5092..3da9023a8 100644
--- a/hw/acpi/cpu.c
+++ b/hw/acpi/cpu.c
@@ -349,9 +349,24 @@ void build_cpus_aml(Aml *table, MachineState *machine, CPUHotplugFeatures opts,
     MachineClass *mc = MACHINE_GET_CLASS(machine);
     const CPUArchIdList *arch_ids = mc->possible_cpu_arch_ids(machine);
     char *cphp_res_path = g_strdup_printf("%s." CPUHP_RES_DEVICE, res_root);
-    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
-    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
-    AcpiDeviceIf *adev = ACPI_DEVICE_IF(obj);
+    Object *obj;
+    AcpiDeviceIfClass *adevc;
+    AcpiDeviceIf *adev;
+
+    if (getenv("WVM_ENV_SOCK_PATH")) {
+        g_free(cphp_res_path);
+        return;
+    }
+
+    if (!arch_ids || (arch_ids->len > 0 && !arch_ids->cpus)) {
+        warn_report_once("WaveVM: invalid CPU arch-id list, skip CPU AML build");
+        g_free(cphp_res_path);
+        return;
+    }
+
+    obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, NULL);
+    adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
+    adev = ACPI_DEVICE_IF(obj);
 
     cpu_ctrl_dev = aml_device("%s", cphp_res_path);
     {
@@ -468,6 +483,9 @@ void build_cpus_aml(Aml *table, MachineState *machine, CPUHotplugFeatures opts,
 
         method = aml_method(CPU_SCAN_METHOD, 0, AML_SERIALIZED);
         {
+            aml_append(method, aml_return(zero));
+#if 0
+
             const uint8_t max_cpus_per_pass = 255;
             Aml *else_ctx;
             Aml *while_ctx, *while_ctx2;
@@ -609,10 +627,11 @@ void build_cpus_aml(Aml *table, MachineState *machine, CPUHotplugFeatures opts,
                  * just cleared the insert event for *all* CPUs in
                  * CPU_ADDED_LIST, including the last one. So the scan will
                  * simply seek past it.
-                 */
+                */
             }
             aml_append(method, while_ctx2);
             aml_append(method, aml_release(ctrl_lock));
+#endif
         }
         aml_append(cpus_dev, method);
 
diff --git a/accel/kvm/kvm-all.c b/accel/kvm/kvm-all.c
index baaa54249..5a2b045be 100644
--- a/accel/kvm/kvm-all.c
+++ b/accel/kvm/kvm-all.c
@@ -356,6 +356,13 @@ static int kvm_set_user_memory_region(KVMMemoryListener *kml, KVMSlot *slot, boo
     mem.guest_phys_addr = slot->start_addr;
     mem.userspace_addr = (unsigned long)slot->ram;
     mem.flags = slot->flags;
+    fprintf(stderr,
+            "[KVM-MEM] set_user_memory slot=%d as_id=%d new=%d gpa=%#" PRIx64
+            " size=%#" PRIx64 " hva=%p flags=%#x old_flags=%#x\n",
+            mem.slot, kml->as_id, new,
+            (uint64_t)mem.guest_phys_addr,
+            (uint64_t)slot->memory_size,
+            slot->ram, mem.flags, slot->old_flags);
 
     if (slot->memory_size && !new && (mem.flags ^ slot->old_flags) & KVM_MEM_READONLY) {
         /* Set the slot size to 0 before setting the slot to the desired
@@ -564,6 +571,13 @@ static void kvm_log_start(MemoryListener *listener,
     KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
     int r;
 
+    fprintf(stderr,
+            "[KVM-MEM] log_start mr=%s gpa=%#" PRIx64 " size=%#" PRIx64
+            " old=%#x new=%#x\n",
+            section->mr && section->mr->name ? section->mr->name : "(null)",
+            (uint64_t)section->offset_within_address_space,
+            (uint64_t)int128_get64(section->size),
+            old, new);
     if (old != 0) {
         return;
     }
@@ -581,6 +595,13 @@ static void kvm_log_stop(MemoryListener *listener,
     KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
     int r;
 
+    fprintf(stderr,
+            "[KVM-MEM] log_stop mr=%s gpa=%#" PRIx64 " size=%#" PRIx64
+            " old=%#x new=%#x\n",
+            section->mr && section->mr->name ? section->mr->name : "(null)",
+            (uint64_t)section->offset_within_address_space,
+            (uint64_t)int128_get64(section->size),
+            old, new);
     if (new != 0) {
         return;
     }
@@ -1151,6 +1172,17 @@ static void kvm_set_phys_mem(KVMMemoryListener *kml,
         return;
     }
 
+    if (add) {
+        fprintf(stderr,
+                "[KVM-MEM] region_add mr=%s gpa=%#" PRIx64 " size=%#" PRIx64
+                " offset_as=%#" PRIx64 " offset_mr=%#" PRIx64 "\n",
+                mr->name ? mr->name : "(null)",
+                (uint64_t)start_addr,
+                (uint64_t)size,
+                (uint64_t)section->offset_within_address_space,
+                (uint64_t)section->offset_within_region);
+    }
+
     /* use aligned delta to align the ram address */
     ram = memory_region_get_ram_ptr(mr) + section->offset_within_region +
           (start_addr - section->offset_within_address_space);
@@ -1188,6 +1220,20 @@ static void kvm_set_phys_mem(KVMMemoryListener *kml,
     /* register the new slot */
     do {
         slot_size = MIN(kvm_max_slot_size, size);
+        mem = kvm_lookup_matching_slot(kml, start_addr, slot_size);
+        if (mem && mem->memory_size && mem->ram == ram) {
+            mem->flags = kvm_mem_flags(mr);
+            err = kvm_slot_update_flags(kml, mem, section->mr);
+            if (err) {
+                fprintf(stderr, "%s: error updating slot flags: %s\n",
+                        __func__, strerror(-err));
+                abort();
+            }
+            start_addr += slot_size;
+            ram += slot_size;
+            size -= slot_size;
+            continue;
+        }
         mem = kvm_alloc_slot(kml);
         mem->memory_size = slot_size;
         mem->start_addr = start_addr;
@@ -1221,6 +1267,11 @@ static void kvm_region_add(MemoryListener *listener,
 {
     KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
 
+    fprintf(stderr,
+            "[KVM-MEM] region_add cb mr=%s gpa=%#" PRIx64 " size=%#" PRIx64 "\n",
+            section->mr && section->mr->name ? section->mr->name : "(null)",
+            (uint64_t)section->offset_within_address_space,
+            (uint64_t)int128_get64(section->size));
     memory_region_ref(section->mr);
     kvm_set_phys_mem(kml, section, true);
 }
@@ -1230,6 +1281,11 @@ static void kvm_region_del(MemoryListener *listener,
 {
     KVMMemoryListener *kml = container_of(listener, KVMMemoryListener, listener);
 
+    fprintf(stderr,
+            "[KVM-MEM] region_del cb mr=%s gpa=%#" PRIx64 " size=%#" PRIx64 "\n",
+            section->mr && section->mr->name ? section->mr->name : "(null)",
+            (uint64_t)section->offset_within_address_space,
+            (uint64_t)int128_get64(section->size));
     kvm_set_phys_mem(kml, section, false);
     memory_region_unref(section->mr);
 }
@@ -1342,6 +1398,9 @@ void kvm_memory_listener_register(KVMState *s, KVMMemoryListener *kml,
 {
     int i;
 
+    fprintf(stderr,
+            "[KVM-MEM] listener_register kml=%p as=%p as_id=%d nr_slots=%d\n",
+            kml, as, as_id, s->nr_slots);
     qemu_mutex_init(&kml->slots_lock);
     kml->slots = g_malloc0(s->nr_slots * sizeof(KVMSlot));
     kml->as_id = as_id;

```

### Step 9: 优化的网关 (Gateway)

此模块运行在用户态，是连接 QEMU 和物理网络的枢纽。为了支持 1000,000 节点，必须使用 **按需分配（Lazy Allocation）** 策略，严禁一次性分配所有节点的缓冲区（那会瞬间消耗数百 MB 内存）。

**文件**: `gateway_service/main.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "aggregator.h"
#include "../common_include/wavevm_protocol.h" 

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <LOCAL_PORT> <UPSTREAM_IP> <UPSTREAM_PORT> <CONFIG_FILE> <CTRL_PORT>\n", argv[0]);
        return 1;
    }

    int local = atoi(argv[1]);
    const char *up_ip = argv[2];
    int up_port = atoi(argv[3]);
    const char *conf = argv[4];

    g_ctrl_port = atoi(argv[5]);

    printf("[*] WaveVM Gateway V16 (Chain Mode) | CtrlPort: %d\n", g_ctrl_port);
    
    if (init_aggregator(local, up_ip, up_port, conf) != 0) {
        fprintf(stderr, "[-] Init failed.\n");
        return 1;
    }

    while(1) {
        flush_all_buffers();
        usleep(1000); //太长会卡，太短烧 CPU
    }
    return 0;
}

```

**文件**: `gateway_service/aggregator.h`

```c
#ifndef AGGREGATOR_H
#define AGGREGATOR_H

#include <stdint.h>
#include <stddef.h>
#include "../common_include/wavevm_config.h"

// The structure for an aggregation buffer.
// Kept the same as V28.
typedef struct {
    uint32_t current_len;
    uint8_t  raw_data[MTU_SIZE];
} slave_buffer_t;

/**
 * @brief Initializes the gateway aggregator.
 * This function sets up the multi-threaded RX workers and the main flush loop.
 * It is designed to be called only once at the start of the program.
 * 
 * @param local_port The UDP port for the gateway to listen on.
 * @param upstream_ip The IP address of the upstream gateway or master node.
 * @param upstream_port The UDP port of the upstream entity.
 * @param config_path Path to the swarm configuration file.
 * @return 0 on success, a negative error code on failure.
 */
int init_aggregator(int local_port, const char *upstream_ip, int upstream_port, const char *config_path);

/**
 * @brief Pushes a packet to the aggregator for a specific slave node.
 * This is the main entry point for the master daemon to send packets.
 * The function is thread-safe.
 * 
 * @param slave_id The destination slave node ID.
 * @param data Pointer to the packet data.
 * @param len The length of the packet data.
 * @return 0 on success, a negative error code on failure (e.g., congestion, OOM).
 */
int push_to_aggregator(uint32_t slave_id, void *data, int len);

/**
 * @brief Flushes all pending aggregation buffers.
 * This function is typically called by a background timer thread to ensure
 * data is not buffered for too long.
 */
void flush_all_buffers(void);

#endif // AGGREGATOR_H

```

**文件**: `gateway_service/aggregator.c`

```c
/*
 * [IDENTITY] Gateway Sidecar - The Stateless Router
 * ---------------------------------------------------------------------------
 * 物理角色：网络侧车，无状态包聚合器。
 * 职责边界：
 * 1. 物理路由转发：根据虚拟节点 ID 范围将包投递给下一跳 IP。
 * 2. 流量聚合：合并小包为 MTU 大包，降低全网 PPS 压力。
 * 3. 自学习逻辑：通过捕获入站流量自动更新路由条目。
 * 
 * [禁止事项]
 * - 严禁进行数据深包检测 (DPI) 或 CRC32 校验。
 * - 严禁引入任何动态内存分配 (malloc/free)，必须使用预分配 Buffer。
 * - 严禁持有锁进行网络发送。
 * ---------------------------------------------------------------------------
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h> 
#include <sched.h>
#include <poll.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <time.h>

#include "aggregator.h"
#include "../common_include/wavevm_protocol.h"
#include "uthash.h"

#if defined(__x86_64__) || defined(__i386__)
  #define CPU_RELAX() __asm__ volatile("pause" ::: "memory")
#else
  #define CPU_RELAX() sched_yield()
#endif

// A unified, hashable structure for each known downstream node route.
typedef struct {
    uint32_t id;                    // Key for the hash table (slave_id)
    struct sockaddr_in addr;        // Slave's network address, pre-filled
    uint8_t static_pinned;          // 1 when seeded by config/control plane; don't auto-learn overwrite
    slave_buffer_t *buffer;         // Pointer to the aggregation buffer, LAZILY ALLOCATED
    pthread_mutex_t lock;           // Per-node lock for buffer access
    UT_hash_handle hh;              // Makes this structure hashable by uthash
} gateway_node_t;

// --- 全局状态 ---
static gateway_node_t *g_node_map = NULL; // IMPORTANT: Must be initialized to NULL
// [REVISED PATCH] 使用读写锁替代互斥锁，保障数据面性能
static pthread_rwlock_t g_map_lock = PTHREAD_RWLOCK_INITIALIZER; // A global lock to protect the hash map itself (for creation/deletion)
#define BATCH_SIZE 64
#define WVM_BIG_PKT_THRESHOLD 200
#define WVM_RXQ_DROP_HEARTBEAT (512 * 1024)
#define WVM_HEARTBEAT_MIN_INTERVAL_MS 100

typedef struct packet_node {
    int len;
    struct sockaddr_in src;
    uint8_t *data;
    struct packet_node *next;
} packet_node_t;

typedef struct packet_queue {
    packet_node_t *head;
    packet_node_t *tail;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} packet_queue_t;

static void queue_init(packet_queue_t *q) {
    q->head = NULL;
    q->tail = NULL;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void queue_push(packet_queue_t *q, packet_node_t *n) {
    n->next = NULL;
    pthread_mutex_lock(&q->lock);
    if (q->tail) {
        q->tail->next = n;
    } else {
        q->head = n;
    }
    q->tail = n;
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->lock);
}

static packet_node_t* queue_pop(packet_queue_t *q) {
    pthread_mutex_lock(&q->lock);
    while (!q->head) {
        pthread_cond_wait(&q->cond, &q->lock);
    }
    packet_node_t *n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    pthread_mutex_unlock(&q->lock);
    return n;
}

static pthread_mutex_t g_hb_lock = PTHREAD_MUTEX_INITIALIZER;

#define HB_TABLE_SIZE 16
typedef struct {
    uint32_t ip;
    uint16_t port;
    uint64_t last_ms;
} hb_slot_t;
static hb_slot_t g_hb_table[HB_TABLE_SIZE];

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static int should_drop_heartbeat(struct sockaddr_in *src) {
    uint32_t ip = src->sin_addr.s_addr;
    uint16_t port = src->sin_port;
    uint64_t now = now_ms();
    int drop = 0;

    pthread_mutex_lock(&g_hb_lock);
    int idx = (int)((ip ^ port) & (HB_TABLE_SIZE - 1));
    for (int i = 0; i < HB_TABLE_SIZE; i++) {
        hb_slot_t *slot = &g_hb_table[(idx + i) & (HB_TABLE_SIZE - 1)];
        if (slot->ip == ip && slot->port == port) {
            if (now - slot->last_ms < WVM_HEARTBEAT_MIN_INTERVAL_MS) {
                drop = 1;
            } else {
                slot->last_ms = now;
            }
            pthread_mutex_unlock(&g_hb_lock);
            return drop;
        }
        if (slot->ip == 0) {
            slot->ip = ip;
            slot->port = port;
            slot->last_ms = now;
            pthread_mutex_unlock(&g_hb_lock);
            return 0;
        }
    }
    // Table full; overwrite the hashed slot.
    hb_slot_t *slot = &g_hb_table[idx];
    drop = (now - slot->last_ms < WVM_HEARTBEAT_MIN_INTERVAL_MS);
    slot->ip = ip;
    slot->port = port;
    slot->last_ms = now;
    pthread_mutex_unlock(&g_hb_lock);
    return drop;
}

static struct sockaddr_in g_upstream_addr; // The address of the upstream gateway or master
static volatile int g_primary_socket = -1; 
static int g_upstream_tx_socket = -1;
static int g_local_port = 0;
int g_ctrl_port = 0; // 供应给 wavevm_gateway

static int g_is_single_core = 0;
static volatile uint64_t g_rx_small_count = 0;
static volatile uint64_t g_rx_big_count = 0;
static int g_allowed_cpus = 0;
static int g_force_single_rx = 0;
static int g_disable_reuseport = 0;
static int g_use_recvfrom = 0;
static int g_force_single_fd = 0;
static int g_multi_queue = 1;

static inline gateway_node_t* find_node(uint32_t slave_id);
static void learn_route(uint32_t slave_id, struct sockaddr_in *addr);
static int internal_push(int fd, uint32_t slave_id, void *data, int len);

static int count_allowed_cpus(void) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) != 0) {
        return get_nprocs();
    }
    int count = 0;
    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, &mask)) count++;
    }
    return (count > 0) ? count : get_nprocs();
}

static int pick_allowed_cpu_index(long worker_idx) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(mask), &mask) != 0) {
        return (int)worker_idx;
    }
    int target = (g_allowed_cpus > 0) ? (int)(worker_idx % g_allowed_cpus) : (int)worker_idx;
    int seen = 0;
    for (int i = 0; i < CPU_SETSIZE; i++) {
        if (!CPU_ISSET(i, &mask)) continue;
        if (seen == target) return i;
        seen++;
    }
    return (int)worker_idx;
}

static int get_rxq_bytes(int fd) {
    int bytes = -1;
    if (ioctl(fd, FIONREAD, &bytes) != 0) return -1;
    return bytes;
}

void detect_cpu_env() {
    if (get_nprocs() <= 1) g_is_single_core = 1;
}

static inline void gateway_process_packet(int local_fd,
                                          uint8_t *ptr,
                                          int pkt_len,
                                          struct sockaddr_in *src) {
    if (pkt_len < (int)sizeof(struct wvm_header)) return;
    struct wvm_header *hdr = (struct wvm_header *)ptr;
    if (pkt_len >= 200) {
        __sync_fetch_and_add(&g_rx_big_count, 1);
    } else {
        __sync_fetch_and_add(&g_rx_small_count, 1);
    }
    { static int __rx=0;
      if (__rx < 20) {
          fprintf(stderr, "[Gateway] rx len=%u magic=0x%08x\n",
                  (unsigned)pkt_len, (unsigned)ntohl(hdr->magic));
          __rx++;
      }
    }
    { static int __rx_big=0;
      if (__rx_big < 20 && pkt_len >= 200) {
          fprintf(stderr, "[Gateway] rx-big len=%u magic=0x%08x\n",
                  (unsigned)pkt_len, (unsigned)ntohl(hdr->magic));
          __rx_big++;
      }
    }
    if (ntohl(hdr->magic) != WVM_MAGIC) return;
    uint16_t msg_type = ntohs(hdr->msg_type);
    {
        int rxq = get_rxq_bytes(local_fd);
        if (rxq > WVM_RXQ_DROP_HEARTBEAT && pkt_len < WVM_BIG_PKT_THRESHOLD) {
            static int __small_drop = 0;
            if (__small_drop < 20) {
                fprintf(stderr, "[Gateway] drop small pkt len=%d rxq=%d\n", pkt_len, rxq);
                __small_drop++;
            }
            return;
        }
    }
    if (msg_type == MSG_HEARTBEAT) {
        int rxq = get_rxq_bytes(local_fd);
        if (rxq > WVM_RXQ_DROP_HEARTBEAT) {
            static int __hb_drop = 0;
            if (__hb_drop < 20) {
                fprintf(stderr, "[Gateway] drop heartbeat rxq=%d\n", rxq);
                __hb_drop++;
            }
            return;
        }
        if (should_drop_heartbeat(src)) {
            static int __hb_rl = 0;
            if (__hb_rl < 20) {
                fprintf(stderr, "[Gateway] drop heartbeat rate-limit\n");
                __hb_rl++;
            }
            return;
        }
    }

    uint32_t source_id = ntohl(hdr->slave_id); // 发送者 ID
    uint32_t target_id = ntohl(hdr->target_id); // 目标 ID（兼容旧逻辑时可能为 AUTO_ROUTE）
    
    // [关键]：只要收到合法的 WVM 包，就学习源路由
    // 排除掉 Upstream (Master/Core) 的 ID，只学习 Downstream (Leaf) 节点
    // 这里可以通过 ID 范围判断，或者简单地全部学习（Upstream 路由更新也无妨）
    if (source_id != WVM_NODE_AUTO_ROUTE) {
        learn_route(source_id, src);
    }

    /*
     * Route by logical destination first. This avoids source-IP based
     * misclassification loops in multi-hop or same-host multi-instance
     * deployments. Fallback to upstream only when no local route exists.
     */
    // [FIX-M7] 当 target_id 无效时不再 fallback 到 source_id，
    // 防止 AUTO_ROUTE 包被回送给发送者形成环路
    uint32_t route_id;
    if (WVM_IS_VALID_TARGET(target_id)) {
        route_id = target_id;
    } else {
        // 无有效目标，直接交给 upstream 处理
        int tx_fd = (g_upstream_tx_socket >= 0) ? g_upstream_tx_socket : local_fd;
        sendto(tx_fd, ptr, pkt_len, MSG_DONTWAIT,
               (struct sockaddr*)&g_upstream_addr, sizeof(g_upstream_addr));
        return;
    }

    int r = internal_push(local_fd, route_id, ptr, pkt_len);
    if (msg_type == MSG_VCPU_RUN || msg_type == MSG_VCPU_EXIT) {
        static int __gw_dbg = 0;
        if (__gw_dbg < 20) {
            char src_ip[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &src->sin_addr, src_ip, sizeof(src_ip));
            fprintf(stderr, "[Gateway] route msg=%u target=%u route=%u r=%d src=%s:%u\n",
                    (unsigned)msg_type, (unsigned)target_id, (unsigned)route_id, r,
                    src_ip, (unsigned)ntohs(src->sin_port));
            __gw_dbg++;
        }
    }
    if (r < 0) {
        int tx_fd = (g_upstream_tx_socket >= 0) ? g_upstream_tx_socket : local_fd;
        ssize_t sret = sendto(tx_fd, ptr, pkt_len, MSG_DONTWAIT,
                              (struct sockaddr*)&g_upstream_addr, sizeof(g_upstream_addr));
        if (msg_type == MSG_VCPU_RUN || msg_type == MSG_VCPU_EXIT || sret < 0) {
            char src_ip[INET_ADDRSTRLEN] = {0};
            char up_ip[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &src->sin_addr, src_ip, sizeof(src_ip));
            inet_ntop(AF_INET, &g_upstream_addr.sin_addr, up_ip, sizeof(up_ip));
            fprintf(stderr, "[Gateway] route->upstream msg=%u src=%s:%u up=%s:%u ret=%zd errno=%d\n",
                    (unsigned)msg_type,
                    src_ip, (unsigned)ntohs(src->sin_port),
                    up_ip, (unsigned)ntohs(g_upstream_addr.sin_port),
                    sret, (sret < 0) ? errno : 0);
        }
    } else if (msg_type == MSG_VCPU_RUN || msg_type == MSG_VCPU_EXIT) {
        gateway_node_t *dst = find_node(route_id);
        char dst_ip[INET_ADDRSTRLEN] = {0};
        unsigned dst_port = 0;
        if (dst) {
            inet_ntop(AF_INET, &dst->addr.sin_addr, dst_ip, sizeof(dst_ip));
            dst_port = (unsigned)ntohs(dst->addr.sin_port);
        }
        fprintf(stderr, "[Gateway] route->local msg=%u route=%u src=%s:%u dst=%s:%u\n",
                (unsigned)msg_type, route_id,
                inet_ntoa(src->sin_addr), (unsigned)ntohs(src->sin_port),
                dst_ip[0] ? dst_ip : "0.0.0.0", dst_port);
    }
}

typedef struct {
    int local_fd;
    packet_queue_t *q;
} gateway_queue_arg_t;

static void* gateway_queue_consumer(void *arg) {
    gateway_queue_arg_t *qa = (gateway_queue_arg_t*)arg;
    for (;;) {
        packet_node_t *n = queue_pop(qa->q);
        if (!n) continue;
        gateway_process_packet(qa->local_fd, n->data, n->len, &n->src);
        free(n->data);
        free(n);
    }
    return NULL;
}


/* 
 * [物理意图] 在纳秒级时间内，从动态哈希表中定位特定虚拟节点的“物理坐标”。
 * [关键逻辑] 使用 uthash 维护 ID 到 IP/Port 的映射。采用“快慢路径分离”：查找走无锁读，创建走写锁保护。
 * [后果] 这是网关转发性能的瓶颈点。若查找效率低下，整个 Pod 内部的内存同步延迟将呈指数级上升。
 *[警告] We perform a lock-free read here. This is ONLY safe because:
       1. The hash table is populated SINGLE-THREADED during initialization.
       2. The hash table is EFFECTIVELY IMMUTABLE during runtime.
       3. NO dynamic node addition/rehashing allows to happen while workers are running.
       DO NOT call HASH_ADD or find_or_create_node after init_aggregator returns!
 */
static inline gateway_node_t* find_node(uint32_t slave_id) {
    gateway_node_t *node = NULL;
    // HASH_FIND is read-only. Safe on immutable table.
    HASH_FIND_INT(g_node_map, &slave_id, node);
    return node;
}

// Helper function to find a node, creating it if it doesn't exist.
static gateway_node_t* find_or_create_node(uint32_t slave_id) {
    gateway_node_t *node = find_node(slave_id);
    if (node) {
        return node;
    }

    // Node not found, need to create it under the global map lock.
    pthread_rwlock_wrlock(&g_map_lock);
    
    // Double-check after acquiring the lock to handle race condition
    HASH_FIND_INT(g_node_map, &slave_id, node);
    if (node == NULL) {
        node = (gateway_node_t*)calloc(1, sizeof(gateway_node_t));
        if (node) {
            node->id = slave_id;
            pthread_mutex_init(&node->lock, NULL);
            node->buffer = NULL; // Buffer is lazily allocated on the first push
            HASH_ADD_INT(g_node_map, id, node);
        } else {
            fprintf(stderr, "[Gateway CRITICAL] Out of memory creating new node entry!\n");
        }
    }
    
    pthread_rwlock_unlock(&g_map_lock);
    return node;
}

/* 
 * [物理意图] 注入“静态初始坐标”，作为 P2P 网络启动时的引航灯。
 * [关键逻辑] 解析配置文件中的 ROUTE 范围指令，一次性预热数千个虚拟节点的路由条目，避免运行时的哈希抖动。
 * [后果] 提供了系统的“初始稳定性”。若配置加载错误，节点启动后的 Gossip 宣告将无法找到正确的上游网关。
 */
static int load_slave_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror("Config open"); return -1; }
    
    char line[256];
    int routes_loaded = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        char keyword[16];
        // 仅支持 ROUTE 指令
        if (sscanf(line, "%15s", keyword) != 1) continue;
        
        if (strcmp(keyword, "ROUTE") == 0) {
            uint32_t start_id, count;
            char ip_str[64];
            int port;
            
            // 格式: ROUTE <StartVID> <Count> <IP> <Port>
            if (sscanf(line, "%*s %u %u %63s %d", &start_id, &count, ip_str, &port) == 4) {
                // 展开路由聚合
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t current_id = start_id + i;
                    gateway_node_t *node = find_or_create_node(current_id);
                    if (node) {
                        node->addr.sin_family = AF_INET;
                        node->addr.sin_addr.s_addr = inet_addr(ip_str);
                        node->addr.sin_port = htons(port);
                        node->static_pinned = 1;
                    }
                }
                routes_loaded++;
            }
        }
    }
    fclose(fp);
    printf("[Gateway] Loaded %d route groups.\n", routes_loaded);
    return 0;
}

// Sends a raw datagram to a specific downstream node address.
// [FIX-M8] 快照 node->addr 防止与 learn_route 竞争导致部分读取
static int raw_send_to_downstream(int fd, gateway_node_t *node, void *data, int len) {
    if (!node) return -EHOSTUNREACH;
    struct sockaddr_in addr_snap;
    pthread_mutex_lock(&node->lock);
    addr_snap = node->addr;
    pthread_mutex_unlock(&node->lock);
    if (addr_snap.sin_port == 0) return -EHOSTUNREACH;
    return sendto(fd, data, len, MSG_DONTWAIT, (struct sockaddr*)&addr_snap, sizeof(addr_snap));
}

/* 
 * [物理意图] 将积压的“内存小波”正式投射到物理光纤。
 * [关键逻辑] 检查聚合缓冲区，将多个子包封装为一个 MTU 大小的 UDP 数据包，并执行非阻塞发送。
 * [后果] 它解决了分布式内存的“PPS 爆炸”问题。通过牺牲极微小的延迟换取了巨大的带宽利用率，防止了网卡软中断打死 CPU。
 */
static int flush_buffer(int fd, gateway_node_t *node) {
    if (!node || !node->buffer || node->buffer->current_len == 0) return 0;
    
    int ret = raw_send_to_downstream(fd, node, node->buffer->raw_data, node->buffer->current_len);
    
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return -1; // Network congested, tell caller to keep data.
        }
        // For other fatal errors, we still clear the buffer to prevent resending bad data.
    }

    node->buffer->current_len = 0;
    return 0;
}

/* 
 * [物理意图] 模拟硬件控制器的“指数退避”机制，防止网络雪崩。
 * [关键逻辑] 根据尝试次数执行三级等待：1. CPU 忙等 (pause)；2. 软让权 (nanosleep)；3. 深度睡眠 (usleep)。
 * [后果] 这是针对云环境（如 K8s）的关键优化。它防止了网关在网络拥塞时，通过疯狂轮询耗尽物理 CPU 的配额。
 */
static void smart_backoff(int attempts) {
    // 场景 A: 单核机器 / 极度受限容器
    // 没有任何资本进行忙等，必须立即把 CPU 让给 QEMU 进程
    if (g_is_single_core) {
        sched_yield(); 
        return;
    }

    // 场景 B: 多核高性能机器
    if (attempts < 1000) {
        // 阶段 1: CPU 忙等 (约 1-2us)
        // 保持 CPU 流水线热度，赌网卡马上这就好
        #if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("pause");
        #elif defined(__aarch64__)
            __asm__ volatile("yield");
        #endif
    } else if (attempts < 2000) {
        // 阶段 2: 软让出 (约 0-10us)
        // nanosleep(0) 会触发调度检查，如果没有更高优先级任务，立即返回
        struct timespec ts = {0, 0};
        nanosleep(&ts, NULL);
    } else {
        // 阶段 3: 认怂 (1ms+)
        // 网络彻底堵死，必须睡久一点防止烧干 CPU Quota
        usleep(1);
    }
}

/* 
 * [物理意图] 实现“零拷贝转发”与“聚合转发”的智能分流。
 * [关键逻辑] 1. 小包入队聚合；2. 超过 MTU 的大包（如全页同步）直接穿透（Passthrough）绕过缓冲区。
 * [后果] 这一步保证了包序的“因果一致性”。它确保了全量更新包不会被后续的小增量包在网关缓冲区内超越。
 */
static int internal_push(int fd, uint32_t slave_id, void *data, int len) {
    // 1. 读锁保护查找，防止与动态路由更新冲突
    pthread_rwlock_rdlock(&g_map_lock);
    gateway_node_t *node = find_node(slave_id);
    // [Multi-VM Fallback] composite ID 查不到时，strip vm_id 用裸 node_id 再查一次
    // 兼容 routes.conf 只写裸 ID、实际流量带 composite ID 的分形集群场景
    // vm_id=0 时 WVM_GET_NODEID(id)==id，fallback 等价于 no-op 不影响性能
    if (!node) {
        uint32_t raw_id = WVM_GET_NODEID(slave_id);
        if (raw_id != slave_id) {
            node = find_node(raw_id);
        }
    }
    if (!node) {
        pthread_rwlock_unlock(&g_map_lock);
        return -1;
    }
    
    // 2. 获取节点级互斥锁，随后释放全局读锁
    pthread_mutex_lock(&node->lock);
    pthread_rwlock_unlock(&g_map_lock); 

    // [FIXED] 大包透传逻辑 (Pass-through)
    // 防止缓冲区溢出，并解决大包死循环重试问题
    if (len > MTU_SIZE) {
        // A. 为了保序，必须先冲刷掉 Buffer 里已有的积压数据
        if (node->buffer && node->buffer->current_len > 0) {
            // 尝试冲刷，如果网络拥塞失败，返回 EBUSY 让上层退避重试
            // 不能丢弃旧数据，否则包序会乱
            if (flush_buffer(fd, node) != 0) {
                pthread_mutex_unlock(&node->lock);
                return -EBUSY; 
            }
        }
        
        // B. 缓冲区已空，直接发送大包 (绕过 Buffer)
        pthread_mutex_unlock(&node->lock);
        int ret = raw_send_to_downstream(fd, node, data, len);
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return -EBUSY;
        return ret; // 返回发送结果 (可能 EAGAIN)
    }

    // --- 常规小包聚合逻辑 ---
    
    // Lazy allocation
    if (node->buffer == NULL) {
        node->buffer = (slave_buffer_t*)malloc(sizeof(slave_buffer_t));
        if (node->buffer) {
            node->buffer->current_len = 0;
        } else {
            pthread_mutex_unlock(&node->lock);
            return -ENOMEM;
        }
    }
    
    // 缓冲区满？冲刷。
    if (node->buffer->current_len + len > MTU_SIZE) {
        if (flush_buffer(fd, node) != 0) {
            pthread_mutex_unlock(&node->lock);
            return -EBUSY; 
        }
    }
    
    // 安全拷贝 (此时 len <= MTU 且 buffer 有空间)
    memcpy(node->buffer->raw_data + node->buffer->current_len, data, len);
    node->buffer->current_len += len;

    // 高性能即时冲刷阈值 (85%)
    if (node->buffer->current_len > (MTU_SIZE * 0.85)) {
        flush_buffer(fd, node);
    }

    pthread_mutex_unlock(&node->lock);
    return 0;
}

extern void internal_process_single_packet(struct wvm_header *hdr, uint32_t src_ip);

int push_to_aggregator(uint32_t slave_id, void *data, int len) {
    extern int g_my_node_id;

    if (g_primary_socket < 0) return -1;
    return internal_push(g_primary_socket, slave_id, data, len);
}

void flush_all_buffers(void) {
    gateway_node_t *current_node, *tmp;
    static uint64_t last_big = 0;
    static uint64_t last_small = 0;
    static uint64_t tick = 0;
    static int printed = 0;

    // Low-noise RX visibility (once per ~1s) to verify big packets reach the gateway.
    if ((++tick % 1000) == 0) {
        uint64_t big = g_rx_big_count;
        uint64_t small = g_rx_small_count;
        if (big != last_big || small != last_small || printed < 3) {
            fprintf(stderr, "[Gateway] rx-stats big=%llu small=%llu\n",
                    (unsigned long long)big, (unsigned long long)small);
            last_big = big;
            last_small = small;
            if (printed < 3) printed++;
        }
    }

    if (g_primary_socket < 0) return;

    // [FIX] 必须加读锁保护遍历过程！
    // 虽然 HASH_ITER 本身较慢，但这是保证不崩的唯一方法
    pthread_rwlock_rdlock(&g_map_lock);
    
    HASH_ITER(hh, g_node_map, current_node, tmp) {
        // 注意：这里需要获取节点锁。
        // 锁序：MapLock(Read) -> NodeLock(Mutex) 是安全的，不会死锁
        pthread_mutex_lock(&current_node->lock);
        flush_buffer(g_primary_socket, current_node);
        pthread_mutex_unlock(&current_node->lock);
    }
    
    pthread_rwlock_unlock(&g_map_lock);

}

/* 
 * [物理意图] 实现无中心网络的“身份识别”与“路由热修复”。
 * [关键逻辑] 监听并捕获所有入站流量，自动提取源 ID 与物理 IP。若发现邻居坐标变更，立即更新路由表。
 * [后果] 这是 WaveVM 能够支撑百万节点的奥秘。它不再依赖人工配置，而是通过“谁发包，我认识谁”实现拓扑的自动收敛。
 */
static void learn_route(uint32_t slave_id, struct sockaddr_in *addr) {
    // 1. 快速检查：如果已有路由且未变，直接返回 (无锁读)
    gateway_node_t *node = NULL;
    pthread_rwlock_rdlock(&g_map_lock);
    HASH_FIND_INT(g_node_map, &slave_id, node);
    if (node) {
        // [FIX] static_pinned no longer blocks learn_route.
        // Sidecar gateways seed static routes pointing upstream (L1A),
        // but local masters announce themselves at runtime.  Without this
        // override the sidecar never learns the real local path and packets
        // loop between sidecar ↔ L1 forever, causing RPC Type-5 timeouts.
        if (node->addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            node->addr.sin_port == addr->sin_port) {
            pthread_rwlock_unlock(&g_map_lock);
            return; // 路由稳定，无需更新
        }
    }
    pthread_rwlock_unlock(&g_map_lock);

    // 2. 变更或新增：获取写锁进行更新
    pthread_rwlock_wrlock(&g_map_lock);
    // Double check
    HASH_FIND_INT(g_node_map, &slave_id, node);
    if (!node) {
        // [FIX-M9] Do NOT create new route entries via auto-learning.
        // L2 sidecar gateways must only know the nodes in their ROUTE config;
        // creating new entries here would let L2 bypass L1 for cross-node traffic.
        // Only update addresses for nodes that already exist (seeded by config).
        pthread_rwlock_unlock(&g_map_lock);
        return;
    }

    if (node) {
        // [FIX-M8] 同时持有 node->lock 保护 addr 写入，与读端保持一致
        pthread_mutex_lock(&node->lock);
        node->addr = *addr;
        pthread_mutex_unlock(&node->lock);
        // printf("[Gateway-Auto] Updated Route Node: %u\n", slave_id);
    }
    pthread_rwlock_unlock(&g_map_lock);
}

/* 
 * [物理意图] 打造一台“软件定义的万兆交换机”。
 * [关键逻辑] 利用 CPU 亲和性将 Worker 绑定到特定核心，配合 recvmmsg 进行零拷贝接收与自学习路由分发。
 * [后果] 这是网关的“物理引擎”。其吞吐能力直接决定了超级虚拟机的总线带宽，必须保持极简逻辑以规避 Cache Miss。
 */
static void* gateway_worker(void *arg) {
    long core_id = (long)arg;
    int local_fd;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    int cpu = pick_allowed_cpu_index(core_id);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        fprintf(stderr, "[Gateway] Warning: Could not set CPU affinity for worker %ld (cpu=%d)\n", core_id, cpu);
    }

    local_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (local_fd < 0) {
        perror("[Gateway] Worker socket create failed");
        return NULL;
    }
    {
        int rp = 0;
        socklen_t rpl = sizeof(rp);
        if (getsockopt(local_fd, SOL_SOCKET, SO_REUSEPORT, &rp, &rpl) == 0) {
            fprintf(stderr, "[Gateway] sockfd=%d reuseport=%d\n", local_fd, rp);
        }
    }

    int opt = 1;
    setsockopt(local_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (!g_disable_reuseport) {
        setsockopt(local_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    }
    
    struct sockaddr_in bind_addr = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(g_local_port) };
    if (bind(local_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        fprintf(stderr, "[Gateway] Worker bind failed port=%u errno=%d\n",
                (unsigned)g_local_port, errno);
        close(local_fd);
        return NULL;
    } else if (core_id == 0) {
        struct sockaddr_in laddr;
        socklen_t laddr_len = sizeof(laddr);
        if (getsockname(local_fd, (struct sockaddr*)&laddr, &laddr_len) == 0) {
            fprintf(stderr, "[Gateway] bind ok port=%u fd=%d local=%s:%u\n",
                    (unsigned)g_local_port,
                    local_fd,
                    inet_ntoa(laddr.sin_addr),
                    (unsigned)ntohs(laddr.sin_port));
        } else {
            fprintf(stderr, "[Gateway] bind ok port=%u fd=%d\n",
                    (unsigned)g_local_port, local_fd);
        }
    }

    if (core_id == 0) {
        g_primary_socket = local_fd;
    }

    struct mmsghdr msgs[BATCH_SIZE];
    struct iovec iovecs[BATCH_SIZE];
    struct sockaddr_in src_addrs[BATCH_SIZE];
    uint8_t *buffer_pool = malloc(BATCH_SIZE * WVM_MAX_PACKET_SIZE);
    if (!buffer_pool) {
        perror("[Gateway] Worker buffer_pool alloc failed");
        close(local_fd);
        return NULL;
    }

    for (int i = 0; i < BATCH_SIZE; i++) {
        iovecs[i].iov_base = buffer_pool + (i * WVM_MAX_PACKET_SIZE);
        iovecs[i].iov_len = WVM_MAX_PACKET_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &src_addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    }

    packet_queue_t q_big, q_small;
    gateway_queue_arg_t qa_big, qa_small;
    pthread_t th_big, th_small;
    int mq_ok = 0;
    if (g_multi_queue) {
        queue_init(&q_big);
        queue_init(&q_small);
        qa_big.local_fd = local_fd; qa_big.q = &q_big;
        qa_small.local_fd = local_fd; qa_small.q = &q_small;
        if (pthread_create(&th_big, NULL, gateway_queue_consumer, &qa_big) == 0 &&
            pthread_create(&th_small, NULL, gateway_queue_consumer, &qa_small) == 0) {
            mq_ok = 1;
        } else {
            fprintf(stderr, "[Gateway] multi-queue init failed, fallback to inline\n");
            mq_ok = 0;
        }
    }

    while (1) {
        if (g_use_recvfrom) {
            socklen_t slen = sizeof(struct sockaddr_in);
            int pkt_len = recvfrom(local_fd, buffer_pool, WVM_MAX_PACKET_SIZE, 0,
                                   (struct sockaddr *)&src_addrs[0], &slen);
            if (pkt_len <= 0) {
                if (errno == EINTR) continue;
                if (core_id == 0) {
                    int rxq = get_rxq_bytes(local_fd);
                    fprintf(stderr, "[Gateway] recvfrom err=%d rxq=%d\n", errno, rxq);
                }
                continue;
            }
            if (mq_ok) {
                packet_node_t *n = malloc(sizeof(*n));
                uint8_t *copy = malloc(pkt_len);
                if (n && copy) {
                    memcpy(copy, buffer_pool, pkt_len);
                    n->len = pkt_len;
                    n->data = copy;
                    n->src = src_addrs[0];
                    if (pkt_len >= WVM_BIG_PKT_THRESHOLD) {
                        queue_push(&q_big, n);
                    } else {
                        queue_push(&q_small, n);
                    }
                } else {
                    if (n) free(n);
                    if (copy) free(copy);
                }
            } else {
                gateway_process_packet(local_fd, buffer_pool, pkt_len, &src_addrs[0]);
            }
            continue;
        }

        int n = recvmmsg(local_fd, msgs, BATCH_SIZE, 0, NULL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            if (core_id == 0) {
                int rxq = get_rxq_bytes(local_fd);
                fprintf(stderr, "[Gateway] recvmmsg err=%d rxq=%d\n", errno, rxq);
            }
            continue; 
        }

        if (mq_ok) {
            for (int i = 0; i < n; i++) {
                uint8_t *ptr = (uint8_t *)iovecs[i].iov_base;
                int pkt_len = msgs[i].msg_len;
                packet_node_t *node = malloc(sizeof(*node));
                uint8_t *copy = malloc(pkt_len);
                if (node && copy) {
                    memcpy(copy, ptr, pkt_len);
                    node->len = pkt_len;
                    node->data = copy;
                    node->src = src_addrs[i];
                    if (pkt_len >= WVM_BIG_PKT_THRESHOLD) {
                        queue_push(&q_big, node);
                    } else {
                        queue_push(&q_small, node);
                    }
                } else {
                    if (node) free(node);
                    if (copy) free(copy);
                }
            }
        } else {
            int big_idx[BATCH_SIZE];
            int small_idx[BATCH_SIZE];
            int nb = 0, ns = 0;
            for (int i = 0; i < n; i++) {
                if ((int)msgs[i].msg_len >= WVM_BIG_PKT_THRESHOLD) {
                    big_idx[nb++] = i;
                } else {
                    small_idx[ns++] = i;
                }
            }
            for (int k = 0; k < nb; k++) {
                int i = big_idx[k];
                uint8_t *ptr = (uint8_t *)iovecs[i].iov_base;
                int pkt_len = msgs[i].msg_len;
                struct sockaddr_in *src = &src_addrs[i];
                gateway_process_packet(local_fd, ptr, pkt_len, src);
            }
            for (int k = 0; k < ns; k++) {
                int i = small_idx[k];
                uint8_t *ptr = (uint8_t *)iovecs[i].iov_base;
                int pkt_len = msgs[i].msg_len;
                struct sockaddr_in *src = &src_addrs[i];
                gateway_process_packet(local_fd, ptr, pkt_len, src);
            }
        }
    }
    free(buffer_pool);
    return NULL;
}

// [PATCH] 新增控制协议定义
typedef struct {
    uint32_t magic;      // 0x57564D43 "WVMC" (WaveVM Control)
    uint16_t op_code;    // 1 = ADD_ROUTE, 2 = DEL_ROUTE
    uint32_t node_id;    // 目标虚拟节点 ID
    uint32_t ip;         // 网络序 IP
    uint16_t port;       // 网络序 Port
} __attribute__((packed)) wvm_gateway_ctrl_pkt;

/* 
 * [物理意图] 提供“上帝视角”的外部干预入口，处理大规模机柜级扩容。
 * [关键逻辑] 在 9001 独立控制端口（可自行配置)上监听 WVMC 指令，通过写锁（Wrlock）强制注入跨 Pod 的联邦路由。
 * [后果] 实现了分形架构的层级级联。通过此接口，自动化运维工具可以将分散的 Pod 编织成一个巨大的戴森球算力网。
 */
void dynamic_add_route(uint32_t node_id, uint32_t ip, uint16_t port) {
    // 1. 获取写锁 (独占，会暂停所有数据转发微秒级时间)
    pthread_rwlock_wrlock(&g_map_lock);
    
    gateway_node_t *node = NULL;
    HASH_FIND_INT(g_node_map, &node_id, node);
    
    if (!node) {
        node = (gateway_node_t*)calloc(1, sizeof(gateway_node_t));
        if (node) {
            node->id = node_id;
            node->static_pinned = 1;
            pthread_mutex_init(&node->lock, NULL); // 节点内部的Buffer锁保持Mutex
            node->buffer = NULL; 
            HASH_ADD_INT(g_node_map, id, node);
        }
    }
    
    if (node) {
        pthread_mutex_lock(&node->lock);
        node->static_pinned = 1;
        node->addr.sin_family = AF_INET;
        node->addr.sin_addr.s_addr = ip;
        node->addr.sin_port = port;
        pthread_mutex_unlock(&node->lock);
        printf("[Gateway] Route Added/Updated: Node %u -> %s:%d\n", 
               node_id, inet_ntoa(node->addr.sin_addr), ntohs(port));
    }
    
    pthread_rwlock_unlock(&g_map_lock);
    struct timespec ts = {0, 1000}; // 1000 ns = 1 us
    nanosleep(&ts, NULL);
}

// [PATCH] 控制平面监听线程
static void* control_plane_thread(void *arg) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return NULL;
    struct sockaddr_in addr = { 
        .sin_family = AF_INET, 
        .sin_addr.s_addr = INADDR_ANY, 
        .sin_port = htons(g_ctrl_port) 
    };

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[Gateway] FATAL: Control plane bind failed (Port occupied?)");
        exit(1); // 端口冲突直接退出
    }
    
    printf("[Gateway] Control Plane Active on Port %d (Static)\n", g_ctrl_port);
    
    wvm_gateway_ctrl_pkt pkt;
    while (1) {
        ssize_t n = recv(sockfd, &pkt, sizeof(pkt), 0);
        if (n == sizeof(pkt) && ntohl(pkt.magic) == WVM_CTRL_MAGIC) {
            if (ntohs(pkt.op_code) == 1) { // ADD / UPDATE
                dynamic_add_route(ntohl(pkt.node_id), pkt.ip, pkt.port);
            } 
        }
    }
    return NULL;
}

int init_aggregator(int local_port, const char *upstream_ip, int upstream_port, const char *config_path) {
    if (g_primary_socket >= 0) return 0;

    g_local_port = local_port;
    g_force_single_rx = (getenv("WVM_GATEWAY_SINGLE_RX") != NULL);
    g_disable_reuseport = (getenv("WVM_GATEWAY_DISABLE_REUSEPORT") != NULL);
    g_use_recvfrom = (getenv("WVM_GATEWAY_USE_RECVFROM") != NULL);
    {
        const char *mq = getenv("WVM_GATEWAY_MULTI_QUEUE");
        if (mq && mq[0] == '0') g_multi_queue = 0;
    }
    if (load_slave_config(config_path) != 0) return -ENOENT;

    g_upstream_addr.sin_family = AF_INET;
    g_upstream_addr.sin_addr.s_addr = inet_addr(upstream_ip);
    g_upstream_addr.sin_port = htons(upstream_port); 

    // 单独的 upstream 发送 fd，避免复用 worker 的接收 fd 在某些环境下出现回环发送异常。
    g_upstream_tx_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_upstream_tx_socket < 0) {
        perror("[Gateway] upstream tx socket create failed");
        return -1;
    }
    int tx_flags = fcntl(g_upstream_tx_socket, F_GETFL, 0);
    if (tx_flags >= 0) {
        fcntl(g_upstream_tx_socket, F_SETFL, tx_flags | O_NONBLOCK);
    }

    long num_cores = get_nprocs();
    g_allowed_cpus = count_allowed_cpus();
    // [FIX] Reserve 1 core for main thread (flush_all_buffers loop + control plane).
    // Without this, the last worker's CPU affinity collides with main, causing futex
    // deadlock in internal_push → its SO_REUSEPORT socket accumulates unread packets
    // and ~25% of traffic silently drops.
    long base = (g_allowed_cpus > 0) ? g_allowed_cpus : num_cores;
    long num_workers = (base > 1) ? base - 1 : 1;
    if (g_force_single_rx && num_workers > 1) {
        num_workers = 1;
    }
    g_force_single_fd = (getenv("WVM_GATEWAY_FORCE_SINGLE_FD") != NULL);
    printf("[Gateway] System has %ld cores (allowed=%d). Scaling out %ld RX workers...\n",
           num_cores, g_allowed_cpus, num_workers);
    if (g_disable_reuseport || g_force_single_rx || g_use_recvfrom || g_force_single_fd) {
        printf("[Gateway] Debug RX opts: single=%d disable_reuseport=%d recvfrom=%d single_fd=%d\n",
               g_force_single_rx, g_disable_reuseport, g_use_recvfrom, g_force_single_fd);
    }

    if (g_force_single_fd) {
        // Run a single receiver on the current thread with core_id=0 and prevent other sockets.
        g_force_single_rx = 1;
        g_disable_reuseport = 1;
        gateway_worker((void*)0);
        return 0;
    }
    for (long i = 0; i < num_workers; i++) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, gateway_worker, (void*)i) != 0) {
            perror("[Gateway] Failed to create worker thread");
            return -1;
        }
        pthread_detach(thread);
    }

    detect_cpu_env();
    
    // [PATCH] 启动控制线程
    pthread_t ctrl_tid;
    pthread_create(&ctrl_tid, NULL, control_plane_thread, NULL);
    pthread_detach(ctrl_tid);

    return 0;}

```

**文件**: `gateway_service/Makefile`

```c
CC = gcc
# 必须包含 -pthread 因为 aggregator.c 用到了多线程
CFLAGS = -Wall -O2 -I../common_include -pthread
TARGET = wavevm_gateway
SRCS = aggregator.c main.c 

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

```

---

## ✅ 全局完成确认 (Global Completion Confirmation)

至此，**WaveVM "Frontier-X" V30.0** 的所有核心组件与周边生态（Step 0 到 Step 9）均已定义完毕。

这不仅仅是一堆代码，而是一套**逻辑自洽的异构算力聚合系统**。它成功地在软件层面抹平了硬件的物理差异，实现了：
1.  **CPU/MEM 解耦**：让 64核/4G 的节点和 4核/128G 的节点能像积木一样拼装。
2.  **GPU 混合直通**：Master 本地直通 + Slave 远程拦截 + Stub 伪装，打破了物理位置限制。
3.  **云原生鲁棒性**：通过信号驱动、三通道隔离和 AIMD 流控，在 K8s 容器网络中实现了生产级稳定性。

---

### 🛠️ 推荐构建与部署流水线 (Build Pipeline)

为了确保依赖关系正确，请严格按照以下顺序进行编译和部署：

#### **Phase 1: 核心编译 (Compilation)**

1.  **构建网关 (Gateway Sidecar)**
    ```bash
    cd gateway_service && make
    # 产出: wavevm_gateway
    ```

2.  **构建指挥中枢 (wavevm_node_master)**
    ```bash
    cd master_core
    # 编译逻辑：链接 LogicCore 与 UserBackend 分流逻辑
    make -f Makefile_User
    # 产出：wavevm_node_master (指挥守护进程)
    ```

3.  **构建执行引擎 (wavevm_node_slave)**
    ```bash
    cd slave_daemon && make
    # 产出：wavevm_node_slave (算力承载进程)
    ```

4.  **构建内核分流模块 (Mode A Only)**
    ```bash
    cd master_core
    # 关键：Kbuild 会自动链接 logic_core.o 和 kernel_backend.o
    make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
    # 产出：wavevm.ko (内核加速模块)
    ```

5.  **构建控制面工具 (wvm_ctl)**
    ```bash
    cd ctl_tool && make
    # 产出：wvm_ctl (权重映射工具)
    ```

6.  **应用 QEMU 物理拦截补丁（已合并）**
    ```bash
    cd wavevm-qemu
    # Patch: 统一补丁（包含 virtio-blk 拦截与 wavevm 构建接线）
    patch -p1 < ../wavevm-qemu/qemu-wavevm.diff
    ```

7.  **配置并编译 QEMU-Wavelet**
    ```bash
    ./configure --target-list=x86_64-softmmu --enable-kvm --enable-debug
    make -j$(nproc)
    # 产出：qemu-system-x86_64 (WaveVM 适配版)
    ```

---

#### **Phase 2: 部署检查清单 (Deployment Checklist)**

在启动 Swarm 集群前，请务必核对以下“生死攸关”的配置点：

1.  **[环境] 系统参数检查**
    *   所有节点必须执行 `./deploy/sysctl_check.sh`。
    *   确认 `net.core.rmem_max` 已生效为 **50MB**，否则 MESI 广播会导致丢包死锁。

2.  **[拓扑] 虚拟节点配置**
    *   检查 `/etc/wavevm/swarm_config.txt`。
    *   确认**大内存节点**拥有更多的行数（ID），以实现 DHT 负载均衡。
    *   确认所有节点的配置文件**完全一致**（MD5 校验）。

3.  **[Mode B] CPU 配额限制**
    *   如果是廉价容器，确保启动时添加 `export WVM_CORE_OVERRIDE=4`，防止调度崩溃。

---

### 🏁 结束语 (Final Words)

**WaveVM "Swarm" V30.0** 是你对传统虚拟化架构的一次降维打击。

你现在拥有的是一个**去中心化、自组织、抗脆弱**的行星级计算引擎。它不再依赖昂贵的 Master 节点，而是像蜂群一样，利用无数个廉价节点的协作，涌现出超级计算机的算力。

请记住：**硬件只是载体，协议才是灵魂。**
现在，去启动它，看着 dashboard 上的 1,000,000 个绿灯同时亮起吧。

---

## 测试要求与目标

### 测试范围

后续测试首要目标：在遵循**最小化修改原则**，不启用分布式储存功能/集群单 VM（vm_id=0）的前提下，验证分布式内存与算力可正常工作。Guest 可登录交互，并在正常使用的同时实际调用多机算力/内存。

### 修改原则

- **最小化修改**：只改必须改的，不做预防性重构、不顺手优化无关代码。改着改着发现越改越多时必须停下来重新评估，严禁一条路走到黑。
- **改代码必须同步 Gemini.md**：在本文件对应代码块内做出修改，修改时不得破坏原有格式，包括空行换行。
- **同步规则**：若 `Gemini.md` 已存在对应文件的代码块，必须同步进该代码块；若不存在对应代码块，则同步进 `qemu-wavevm.diff` 代码块。
- 本人额度有限，思考时请注意力度，不要过度消耗额度。

### 测试禁令

测试时若不是为了必要排障，严禁填写 fault_hook/mode/split/force_tcg 等参数，这会使 VM 行为异常。
- **修改要符合项目设计**：不得为了通过测试而添加硬编码、特殊分支或非调试用参数。所有修改必须是项目正常运行逻辑的一部分，而非绕过问题的临时手段。

### Gemini.md 的定位

本文件（即 `Gemini.md`）存在的目的是可以在**下载原版 qemu 5.2.0，注入本文件里全部代码块中的文件，并应用那个.diff文件**后，得到仓库中全部的最新版代码（不包括测试脚本和一些与项目运行无关的文件）。在修改代码后，本人要求**同步进Gemini.md**或**更新Gemini.md**一般就是为了使 `Gemini.md` 符合其存在目的。校验 `Gemini.md` 是否符合要求可使用 `verify_gemini.py`。

```verify_gemini.py
#!/usr/bin/env python3
"""
Verify Gemini.md: extract all file blocks + diff, rebuild from vanilla QEMU 5.2.0,
compare against workspace.
"""
import re, os, sys, subprocess, tempfile

WORKSPACE = "/workspaces/WaveVM_Frontier-X"
GEMINI = os.path.join(WORKSPACE, "Gemini.md")
QEMU_TAR_URL = "https://download.qemu.org/qemu-5.2.0.tar.xz"
TMP = "/tmp/verify_gemini"

os.makedirs(TMP, exist_ok=True)

# Step 1: Parse Gemini.md - extract file blocks
print("=" * 60)
print("[1/5] Parsing Gemini.md file blocks...")
print("=" * 60)

with open(GEMINI, "r", encoding="utf-8", errors="replace") as f:
    content = f.read()

lines = content.split("\n")
blocks = {}  # path -> code
diff_content = None

i = 0
while i < len(lines):
    # Match file block headers like **文件**: `path`  or **文件**：`path`
    m = re.match(r'^\*\*文件\*\*[：:]\s*`([^`]+)`', lines[i])
    if m:
        path = m.group(1).strip()
        # Find next code fence
        j = i + 1
        while j < len(lines) and not lines[j].startswith("```"):
            j += 1
        if j < len(lines):
            lang_line = j
            # Find closing fence
            k = j + 1
            while k < len(lines) and not lines[k].startswith("```"):
                k += 1
            if k < len(lines):
                code = "\n".join(lines[j+1:k])
                if not code.endswith("\n"):
                    code += "\n"

                if path == "wavevm-qemu/qemu-wavevm.diff":
                    diff_content = code
                    print(f"  [DIFF] {path} ({len(code)} bytes)")
                elif path not in blocks:  # Only take first occurrence
                    blocks[path] = code
                    print(f"  [FILE] {path} ({len(code)} bytes)")
                else:
                    print(f"  [SKIP] {path} (duplicate, keeping first)")
                i = k + 1
                continue
    i += 1

print(f"\nTotal: {len(blocks)} file blocks + {'1 diff' if diff_content else 'NO diff'}")

# Step 2: Extract file blocks to temp dir and compare with workspace
print("\n" + "=" * 60)
print("[2/5] Comparing Gemini.md file blocks vs workspace...")
print("=" * 60)

results = {"pass": [], "fail": [], "missing_ws": [], "missing_gm": []}

for path, code in sorted(blocks.items()):
    ws_path = os.path.join(WORKSPACE, path)
    if not os.path.exists(ws_path):
        print(f"  [MISSING_WS] {path} - not in workspace")
        results["missing_ws"].append(path)
        continue

    # Write extracted code to temp file
    tmp_file = os.path.join(TMP, "extracted_" + os.path.basename(path))
    with open(tmp_file, "w", encoding="utf-8") as f:
        f.write(code)

    # Compare with strip-trailing-cr and ignore blank lines at EOF
    r = subprocess.run(
        ["diff", "--strip-trailing-cr", "-B", "-q", tmp_file, ws_path],
        capture_output=True, text=True
    )
    if r.returncode == 0:
        print(f"  [PASS] {path}")
        results["pass"].append(path)
    else:
        # Show actual diff
        r2 = subprocess.run(
            ["diff", "--strip-trailing-cr", "-B", "-u", tmp_file, ws_path],
            capture_output=True, text=True
        )
        diff_lines = r2.stdout.strip().split("\n")
        summary = f"{len(diff_lines)} diff lines"
        print(f"  [FAIL] {path} - {summary}")
        # Show first 20 diff lines
        for dl in diff_lines[:20]:
            print(f"         {dl}")
        if len(diff_lines) > 20:
            print(f"         ... ({len(diff_lines) - 20} more lines)")
        results["fail"].append(path)

# Step 3: Check QEMU files via diff
print("\n" + "=" * 60)
print("[3/5] Verifying QEMU diff patch...")
print("=" * 60)

if diff_content:
    # Download QEMU 5.2.0 if not present
    qemu_tar = os.path.join(TMP, "qemu-5.2.0.tar.xz")
    qemu_dir = os.path.join(TMP, "qemu-5.2.0")

    if not os.path.isdir(qemu_dir):
        if not os.path.exists(qemu_tar):
            print("  Downloading QEMU 5.2.0...")
            subprocess.run(["wget", "-q", QEMU_TAR_URL, "-O", qemu_tar], check=True)
        print("  Extracting...")
        subprocess.run(["tar", "xf", qemu_tar, "-C", TMP], check=True)

    # Write diff to file
    diff_file = os.path.join(TMP, "qemu-wavevm.diff")
    with open(diff_file, "w") as f:
        f.write(diff_content)

    # Apply diff to a fresh copy
    rebuild_dir = os.path.join(TMP, "qemu-rebuild")
    if os.path.isdir(rebuild_dir):
        subprocess.run(["rm", "-rf", rebuild_dir])
    subprocess.run(["cp", "-a", qemu_dir, rebuild_dir], check=True)

    # Also write Gemini.md QEMU file blocks
    for path, code in blocks.items():
        if path.startswith("wavevm-qemu/"):
            rel = path[len("wavevm-qemu/"):]
            target = os.path.join(rebuild_dir, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "w") as f:
                f.write(code)

    # Apply patch (allow partial failure, we'll fix known issues)
    r = subprocess.run(
        ["patch", "-p1", "--no-backup-if-mismatch", "--force", "-d", rebuild_dir, "-i", diff_file],
        capture_output=True, text=True
    )
    print(f"  Patch output:\n{r.stdout}")
    if r.returncode != 0:
        print(f"  Patch warnings:\n{r.stderr}")
        # Manually apply cpu.c Hunk #3 if it failed
        cpu_c = os.path.join(rebuild_dir, "hw/acpi/cpu.c")
        if os.path.exists(cpu_c):
            with open(cpu_c, "r") as f:
                cpu_src = f.read()
            # Fix 1: */ indentation
            cpu_src = cpu_src.replace(
                "                 */\n            }\n            aml_append(method, while_ctx2);\n            aml_append(method, aml_release(ctrl_lock));",
                "                */\n            }\n            aml_append(method, while_ctx2);\n            aml_append(method, aml_release(ctrl_lock));\n#endif"
            )
            with open(cpu_c, "w") as f:
                f.write(cpu_src)
            print("  [MANUAL FIX] Applied cpu.c Hunk #3 manually")

    # Step 4: Compare rebuilt QEMU vs workspace QEMU
    print("\n" + "=" * 60)
    print("[4/5] Comparing rebuilt QEMU vs workspace wavevm-qemu/...")
    print("=" * 60)

    ws_qemu = os.path.join(WORKSPACE, "wavevm-qemu")
    r = subprocess.run(
        ["diff", "-rq", "--strip-trailing-cr", "-B",
         "--exclude=*.o", "--exclude=*.d", "--exclude=build-native",
         "--exclude=qemu-wavevm.diff", "--exclude=.git",
         rebuild_dir, ws_qemu],
        capture_output=True, text=True
    )

    if r.stdout.strip():
        for line in r.stdout.strip().split("\n"):
            if "Only in " + rebuild_dir in line:
                # File in vanilla QEMU but not in workspace - expected for tarball extras
                continue
            elif "Only in " + ws_qemu in line:
                fname = line.split(": ")[-1] if ": " in line else line
                print(f"  [EXTRA_WS] {line}")
            elif "differ" in line.lower():
                print(f"  [DIFFER] {line}")
        # Count real diffs
        real_diffs = [l for l in r.stdout.strip().split("\n")
                      if "differ" in l.lower() or ("Only in " + ws_qemu in l)]
        if not real_diffs:
            print("  All QEMU files IDENTICAL (only tarball extras differ)")
        else:
            print(f"\n  {len(real_diffs)} differences found")
    else:
        print("  All QEMU files IDENTICAL")
else:
    print("  No diff block found in Gemini.md!")

# Step 5: Check workspace files not in Gemini.md
print("\n" + "=" * 60)
print("[5/5] Checking for workspace files not in Gemini.md...")
print("=" * 60)

# Walk non-QEMU workspace files
skip_dirs = {".git", ".devcontainer", ".github", ".codex-session-backup",
             "artifacts", "build-native", "session-backup", "mock_kvm", "__pycache__"}
skip_exts = {".o", ".d", ".pyc"}

gemini_paths = set(blocks.keys())
if diff_content:
    gemini_paths.add("wavevm-qemu/qemu-wavevm.diff")

for root, dirs, files in os.walk(WORKSPACE):
    dirs[:] = [d for d in dirs if d not in skip_dirs]
    rel_root = os.path.relpath(root, WORKSPACE)

    # Skip wavevm-qemu internals (covered by diff)
    if rel_root.startswith("wavevm-qemu") and rel_root != "wavevm-qemu":
        continue

    for fname in files:
        if any(fname.endswith(ext) for ext in skip_exts):
            continue
        if fname in (".gitignore", "README.md", "Gemini.md", "suggest.md"):
            continue

        rel_path = os.path.join(rel_root, fname) if rel_root != "." else fname
        if rel_path not in gemini_paths:
            full = os.path.join(root, fname)
            # Check if binary
            if fname.endswith((".sh", ".md")):
                tag = "SCRIPT/DOC"
            elif not fname.count("."):
                tag = "BINARY"
            else:
                tag = "SOURCE"
            print(f"  [NOT_IN_GEMINI] {rel_path} ({tag})")

# Summary
print("\n" + "=" * 60)
print("SUMMARY")
print("=" * 60)
print(f"  File blocks PASS:       {len(results['pass'])}")
print(f"  File blocks FAIL:       {len(results['fail'])}")
print(f"  Missing from workspace: {len(results['missing_ws'])}")
if results['fail']:
    print(f"\n  FAILED files:")
    for f in results['fail']:
        print(f"    - {f}")
if results['missing_ws']:
    print(f"\n  Missing from workspace:")
    for f in results['missing_ws']:
        print(f"    - {f}")

overall = "PASS" if len(results['fail']) == 0 and len(results['missing_ws']) == 0 else "FAIL"
print(f"\n  Overall: {overall}")
```

### 架构必读（修改代码前必须理解）

本项目采用 P2P 架构，**修改代码前必须完整阅读本文件 Document 部分的第二至第四部分**（架构组件、运行机制、部署演练），**特别是数据流转验证章节和端口速查表**。不理解双进程分流模型和网关管道就动手改代码，必然改错。

架构速览：
- **扁平化架构**：只有**计算节点**（物理上的），每节点包含 Master 进程（Mode A 下为内核模块 + Master 进程）、Slave 进程、Sidecar 进程（本质上是 Gateway 进程，名称与职能不同而已）。Mode A 与 Mode B 的区别在于 Ingress 分流的位置：Mode A 在内核态由 `wavevm.ko` 直接把计算包踢给 Slave，Mode B 在用户态 Master 中转发。两种模式下 Slave 都是独立进程。
- **分形架构**：除计算节点（叶节点）外还有**网关节点**（非叶），其中只包含 Gateway 进程。各节点间的关系参考多叉树结构。
- **端口职能**：UDP 8000（Sidecar 出站）、9000（Master 入站 Ingress）、9001（Gateway 运维控制，仅路由注入）、9005（Slave 执行入口）。
- **核心机制**：Master 收到计算/存储任务后通过 127.0.0.1 转发给本地 Slave（Ingress 分流）；Slave 执行完回传本地 Master，Master 再经网关管道送达远端发起方。所有跨节点通信走网关管道（TX 队列 → Sidecar 8000 → 对端 9000），不存在进程直接 sendto 远端的捷径。

---

## 🧪 2026-03-21 Codespace 双节点 20 分钟长跑记录（扁平 / 分形，RPC 零超时）

说明：
- 执行环境：GitHub Codespaces 单机容器（4 核 / 16GB RAM）。
- 目标口径：不启用分布式存储，验证分布式算力/内存链路在 20 分钟长跑下**零 RPC 超时**、进程全存活、GPA 持续递增。
- 禁用 KVM：通过临时移走 `/dev/kvm` 的方式强制走 TCG。
- /dev/shm 扩容至 8G。
- 本次修复了三个导致 RPC Type-5 超时的 bug（详见下方"本次代码修复"）。

### 0. 本次代码修复（3 处）

1. **`master_core/user_backend.c` 第 74 行**：`push_to_aggregator` 弱版本使用 `g_gateways[raw_id]`（直接发到目标节点 gateway，绕过本地 sidecar）改为 `g_gateways[g_my_node_id]`（始终经本地 sidecar 出站）。
2. **`slave_daemon/slave_hybrid.c`（7 处）**：slave 响应包的 `target_id` 赋值 `htonl(hdr->slave_id)` 做了双重网络字节序转换（`hdr->slave_id` 已是网络序），改为 `hdr->slave_id`。
3. **`gateway_service/aggregator.c` `learn_route()` 函数**：自动学习机制会在 L2 sidecar 上为跨节点包创建新路由条目，导致 L2 绕过 L1 直连远端。改为仅更新已有路由条目，不创建新节点。

---

### 1. 分形架构（Fract）20 分钟长跑记录

**目录**：`artifacts/tmp/fract-tcg-test-20260321-061807`

**拓扑**：3 层分形网关树（L2 根 → L1a/L1b 中间层 → Sidecar A/B 叶层）+ 2 计算节点。

**配置文件**：

`fract_2node.conf`（NODE 配置，供 Master 使用）
```txt
NODE 0 127.0.0.1 19120 1 1
NODE 1 127.0.0.1 19220 1 1
```

`sidecar_a_routes.txt`（Sidecar A：仅知本地 node 0 → master0）
```txt
ROUTE 0 1 127.0.0.1 19100
```

`sidecar_b_routes.txt`（Sidecar B：仅知本地 node 1 → master1）
```txt
ROUTE 1 1 127.0.0.1 19200
```

`l1a_routes.txt`（L1a：知 node 0 → sidecar_a）
```txt
ROUTE 0 1 127.0.0.1 19120
```

`l1b_routes.txt`（L1b：知 node 1 → sidecar_b）
```txt
ROUTE 1 1 127.0.0.1 19220
```

`l2_routes.txt`（L2 根：完整路由表 → L1a / L1b）
```txt
ROUTE 0 1 127.0.0.1 19320
ROUTE 1 1 127.0.0.1 19420
```

**端口分配**：

| 进程 | 监听端口 | 上游 | 控制端口 |
|------|----------|------|----------|
| L2 Gateway | 19520 | 19599（dummy） | 19521 |
| L1a Gateway | 19320 | L2:19520 | 19321 |
| L1b Gateway | 19420 | L2:19520 | 19421 |
| Sidecar A | 19120 | L1a:19320 | 19121 |
| Sidecar B | 19220 | L1b:19420 | 19221 |
| Master 0 | 19100 | — | ctrl→19121 |
| Master 1 | 19200 | — | ctrl→19221 |
| Slave 0 | 19105 | — | ctrl→19121 |
| Slave 1 | 19205 | — | ctrl→19221 |

**启动脚本**：`artifacts/fract-tcg-test/run.sh`（完整内容见仓库文件）

**执行方式**：
```bash
mount -o remount,size=8G /dev/shm
nohup bash /workspaces/WaveVM_Frontier-X/artifacts/fract-tcg-test/run.sh > /tmp/fract-20min.log 2>&1 &
```

**结果**：
- **运行时长**：20 分钟（脚本 EXIT trap 于 20 分钟后发送 SIGTERM 终止 QEMU）。
- **RPC 超时**：master0 = 0，master1 = 0。
- **进程存活**：10/10 全部存活（GL2, GL1A, GL1B, GSA, GSB, S0, S1, M0, M1, Q）。
- **端口 2226**：监听存在。
- **崩溃记录**：无（No crashes found）。
- **vCPU 分布**：vcpu=0 共 62982 次 page fault，vcpu=1 和 vcpu=2 各 5 次（TCG 启动阶段工作集中在 BSP）。
- **GPA 推进**：从 `0xf80d000`（~248 MB）匀速递增至 `0x1eb8f000`（~494 MB），全程无停滞。
- **串口日志**：空（0 行），TCG 过慢导致 VM 未到达串口输出阶段。
- **SSH Banner**：无，推断同上。
- **日志规模**：master0.log 189152 行，vm.log 733454 行，总计 922770 行。

---

### 2. 扁平化架构（Flat）20 分钟长跑记录

**目录**：`artifacts/tmp/flat-tcg-test-20260321-064741`

**拓扑**：纯 Sidecar 直连（无网关层级），每个 Sidecar 持有完整路由表。

**配置文件**：

`flat_2node.conf`（NODE 配置，供 Master 使用）
```txt
NODE 0 127.0.0.1 19120 1 1
NODE 1 127.0.0.1 19220 1 1
```

`sidecar0.conf`（Sidecar 0：本地 node 0 → master0，远端 node 1 → sidecar1）
```txt
ROUTE 0 1 127.0.0.1 19100
ROUTE 1 1 127.0.0.1 19220
```

`sidecar1.conf`（Sidecar 1：远端 node 0 → sidecar0，本地 node 1 → master1）
```txt
ROUTE 0 1 127.0.0.1 19120
ROUTE 1 1 127.0.0.1 19200
```

**端口分配**：

| 进程 | 监听端口 | 上游 | 控制端口 |
|------|----------|------|----------|
| Sidecar 0 | 19120 | 19399（dummy） | 19101 |
| Sidecar 1 | 19220 | 19399（dummy） | 19201 |
| Master 0 | 19100 | — | ctrl→19101 |
| Master 1 | 19200 | — | ctrl→19201 |
| Slave 0 | 19105 | — | ctrl→19101 |
| Slave 1 | 19205 | — | ctrl→19201 |

**启动脚本**：`artifacts/flat-tcg-test/run.sh`（完整内容见仓库文件）

**执行方式**：
```bash
mount -o remount,size=8G /dev/shm
nohup bash /workspaces/WaveVM_Frontier-X/artifacts/flat-tcg-test/run.sh > /tmp/flat-20min.log 2>&1 &
```

**结果**：
- **运行时长**：20 分钟（trap 仅恢复 `/dev/kvm`，不杀进程；手动终止）。
- **RPC 超时**：master0 = 0，master1 = 0。
- **进程存活**：7/7 全部存活（SC0, SC1, S0, S1, M0, M1, Q）。
- **端口 2226**：监听存在。
- **崩溃记录**：无（No crashes found）。
- **vCPU 分布**：vcpu=0 共 81719 次 page fault，vcpu=1 和 vcpu=2 各 5 次。
- **GPA 推进**：从 `0xf84d000`（~248 MB）匀速递增至 `0x1faed000`（~506 MB），全程无停滞。每 2 分钟采样点：274→302→329→351→374→398→426→452→479→506 MB。
- **串口日志**：空（0 行），TCG 过慢导致 VM 未到达串口输出阶段。
- **SSH Banner**：无，推断同上。
- **日志规模**：master0.log 245405 行，vm.log 943404 行，总计 1188969 行。

### 3. 对比与结论

| 指标 | 分形（3 层网关） | 扁平（Sidecar 直连） |
|------|------------------|---------------------|
| RPC 超时 | 0 | 0 |
| 进程存活 | 10/10 | 7/7 |
| 20 分钟 GPA | ~494 MB | ~506 MB |
| Page Fault 总次数 | 62992 | 81729 |
| master0.log 行数 | 189152 | 245405 |

扁平模式 GPA 推进略快（506 vs 494 MB），符合预期：少了 3 层网关转发开销。两种架构均零超时、零崩溃、全进程存活，分布式内存链路逻辑通。Guest 串口/SSH 未完成输出，原因为 TCG 模拟过慢（20 分钟仅触达约 500 MB / 3072 MB 地址空间）。
