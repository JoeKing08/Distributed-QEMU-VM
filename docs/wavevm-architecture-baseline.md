# WaveVM Architecture Baseline

Status: normative architecture baseline for long-term maintenance.
Scope: this document defines architectural authority, boundaries, invariants, and
first-version decisions. It does not claim that every item is already implemented,
and it is not a substitute for subsystem implementation specifications.

The WaveVM design documentation has three levels:

1. This baseline defines what the system is and which component owns each class
   of decision.
2. `wavevm-refactor-roadmap.md` defines dependency order and migration gates.
3. Subsystem specifications define exact structures, state machines, APIs,
   message semantics, lock ordering, failure behavior, and executable tests.

The following subsystem specifications must exist before the corresponding
runtime is substantially rewritten:

- `specs/memory-consistency.md`
- `specs/vcpu-handoff.md`
- `specs/identity-routing.md`
- `specs/wire-ipc-abi.md`
- `specs/runtime-manifest-lifecycle.md`
- `specs/resource-placement-admission.md`
- `specs/capability-fault-engines.md`
- `specs/kernel-accelerator.md`
- `specs/storage-device-authority.md`

Until a subsystem specification is accepted, current code remains the source of
truth for current behavior, while this document constrains the allowed target
direction. Missing detail in this document is not permission to invent a second
semantic path.

## 1. Project Goal

WaveVM is a distributed QEMU platform that presents a single guest VM backed by CPU and memory resources from multiple physical nodes.

The long-term goal is not only to boot a demo guest. The goal is a maintainable distributed virtualization substrate with these properties:

- The guest keeps using a normal virtual machine interface.
- Physical CPU and memory can be pooled across nodes.
- The same high-level semantics apply to KVM and TCG.
- The same high-level semantics apply to flat and fractal topologies.
- The same cluster can host multiple independent VMs.
- Restricted environments can run without a kernel module.
- Kernel help, when available, accelerates the same semantics rather than defining another product.

WaveVM should be treated as a distributed QEMU, not as a collection of test-specific forwarding hacks.

## 2. Non-Goals

The following are not acceptable long-term directions:

- Hardcoding node IDs, ports, vm IDs, CPU counts, memory sizes, or route entries to pass one test.
- Adding production-path command-line switches whose only purpose is to hide an implementation bug.
- Making KVM and TCG two unrelated products.
- Making Mode A and Mode B two independent control planes.
- Letting the kernel module own global VM lifecycle semantics.
- Requiring a custom host kernel as a baseline feature.
- Requiring KVM as the only supported execution backend.
- Replacing WaveVM's sidecar/master/slave model with direct QEMU-to-QEMU routing merely because another project does that.

## 3. Canonical Architecture

The canonical WaveVM architecture separates control plane from per-VM data
plane:

```text
User CLI / API
    |
    v
User-space cluster control plane
    |  manifest, placement, reservation, lifecycle
    v
Per-node VM runtimes

QEMU frontend
    |
    | local IPC / shared memory / accelerator hooks
    v
User-space node runtime
    |
    | local dispatch
    v
Local slave execution runtime
    |
    | sidecar/gateway fabric
    v
Remote node runtime / remote slave
```

The user-space control plane owns cluster resource inventory, VM identity and
incarnation allocation, resource planning, host placement, admission, and VM
lifecycle. The per-VM user-space master owns the runtime semantics delegated by
the admitted manifest: VM-scoped routing, page consistency, and CPU/memory
dispatch. Both are user-space semantic authorities in non-overlapping domains.

The kernel module, if present, is an accelerator. It must not become a second semantic authority.

## 4. Core Components

### 4.1 User-Space Cluster Control Plane

This is a logical role. It may initially be implemented by an evolved control
tool and node launcher; the architecture does not require a new distributed
service before Phase 4 specifications are complete.

Responsibilities:

- Maintain registered physical capacities and capabilities.
- Allocate `vm_id` and VM incarnation.
- Validate a versioned VM request or launch manifest.
- Plan vCPU, memory, host, device, and control-plane overhead placement.
- Reserve resources and coordinate prepare/start/commit/abort.
- Generate the admitted per-node runtime manifests.
- Report structured lifecycle and admission failures.

Non-responsibilities:

- It must not participate in every page fault or vCPU RPC.
- It must not make gateway or kernel caches semantic authorities.
- It must not infer successful VM startup from process survival alone.

### 4.2 QEMU Frontend

The QEMU frontend is based on QEMU 5.2.0 in the current project.

Responsibilities:

- Present the guest-visible machine, devices, RAM, vCPUs, NUMA layout, disk, and network.
- Implement the WaveVM accelerator glue.
- Export and import vCPU state for remote execution.
- Route remote memory and CPU operations through the local node runtime.
- Keep KVM and TCG differences behind accelerator-specific code.

Non-responsibilities:

- QEMU must not hardcode physical cluster topology.
- QEMU must not become the global control plane.
- QEMU must not directly route around sidecar/master/slave fabric for production traffic.

### 4.3 User-Space Master Runtime

The user-space master runtime is the canonical authority for one local VM instance on a physical node.

Responsibilities:

- Consume the admitted per-node manifest and compatibility configuration during
  migration.
- Hold the VM and node identity assigned by the control plane.
- Own CPU route table and memory placement table for that VM.
- Own page directory and version state in Mode B baseline.
- Dispatch local CPU, memory, and storage operations to the local slave.
- Send all cross-node traffic through the local sidecar/gateway path.
- Provide QEMU IPC endpoint for the local frontend.
- Provide a stable surface for optional accelerators.

Long-term requirement:

- Every VM instance must have its own master runtime state, even if multiple VMs run on the same physical node.

### 4.4 Slave Runtime

The slave runtime is the local execution and storage worker for one VM instance on one physical node.

Responsibilities:

- Execute remote vCPU slices under KVM or TCG.
- Own local KVM vCPU objects when KVM is available.
- Spawn helper QEMU TCG instances when using TCG slave execution.
- Apply and return memory updates needed by remote execution.
- Handle storage operations assigned to the physical node.
- Return precise errors to the master.

Non-responsibilities:

- Slave must not become the cluster control plane.
- Slave must not communicate cross-node directly except through the designed local master/sidecar path.
- Slave must not create or truncate another VM's shared memory backing.

### 4.5 Gateway / Sidecar Fabric

The gateway fabric is the only production cross-node network path.

Responsibilities:

- Route packets by composite target identity.
- Support flat and fractal topology.
- Isolate VM traffic by composite ID and explicit route table entries.
- Avoid auto-learning behavior that overwrites static production routes.
- Preserve low-latency handling for synchronous RPC messages such as VCPU_RUN/VCPU_EXIT.

Non-responsibilities:

- Gateway must not inspect guest semantics beyond routing and scheduling priority.
- Gateway must not synthesize VM lifecycle decisions.

### 4.6 Optional Kernel Accelerator

The kernel module is not a separate Mode A product. It is an optional data-plane accelerator for the user-space canonical path.

Allowed responsibilities:

- Fast page dirty capture.
- Fast page invalidation or page wakeup.
- Fast local forwarding to the local slave.
- Fast packet TX/RX when it preserves user-space semantics.
- Optional low-overhead wait queues for blocking memory or vCPU operations.

Forbidden responsibilities:

- Owning global vm_id authority.
- Owning resource planning.
- Owning host placement.
- Owning multi-VM lifecycle.
- Owning a separate copy of cluster topology as the source of truth.
- Owning another independent logic_core state machine unless that state is explicitly per-VM and synchronized by user-space control.

Current caveat:

- The current `wavevm.ko` has module-global state. That makes it unsuitable for multiple concurrent VM instances on the same physical host unless redesigned.

## 5. Mode Model

### 5.1 Current Terminology

Historically:

- Mode A means `wavevm.ko` is loaded and `/dev/wavevm` is available.
- Mode B means the system runs without the kernel module.

This terminology remains useful for deployment, but it must not imply two independent architectures.

### 5.2 Long-Term Rule

Mode B is the canonical semantic path.

Mode A is Mode B plus optional kernel acceleration.

This means:

```text
Correctness first exists in Mode B.
Mode A may accelerate correctness-preserving operations.
Mode A must never define different correctness semantics.
```

### 5.3 Practical Consequence

Any new feature must first answer:

- What is the Mode B behavior?
- What is the KVM behavior?
- What is the TCG behavior?
- What optional kernel acceleration can speed it up without changing the behavior?

A feature that only works because `wavevm.ko` owns hidden state is not a baseline feature.

## 6. Execution Backends

### 6.1 KVM

KVM is a hardware-accelerated execution backend.

KVM-specific responsibilities:

- Use KVM vCPU ioctls to load and run remote vCPU state.
- Use KVM dirty logging or equivalent mechanisms for guest RAM writes.
- Avoid `mprotect(PROT_NONE)` on KVM RAM paths where it causes EPT violation storms or invalid host mappings.
- Preserve LAPIC, MP state, vCPU events, TSC, and interrupt delivery state across remote execution.

KVM must still obey the same WaveVM CPU route table, memory placement table, and consistency model.

### 6.2 TCG

TCG is the required no-KVM execution backend.

TCG-specific responsibilities:

- Export and import TCG CPU state.
- Preserve guest-visible interrupt state, LAPIC state, and halted/runnable state.
- Maintain remote execution handoff boundaries.
- Handle memory faults and dirty synchronization in user space.
- Avoid assuming a hardware MMU or KVM dirty log exists.

TCG is not an optional toy path. It is required for restricted containers, no-KVM cloud instances, and development environments.

### 6.3 Shared Semantics Between KVM and TCG

KVM and TCG must share these semantics:

- Same guest VM resource definition.
- Same vCPU index to physical-node route mapping.
- Same memory chunk to physical-node placement mapping.
- Same page ownership/version policy.
- Same handoff consistency boundary.
- Same guest-visible machine layout.
- Same error reporting semantics.

Implementation may differ only at the execution and memory-trap layer.

The exact handoff ABI is defined by `specs/vcpu-handoff.md`. That specification
must define field ownership, export/import ordering, vCPU exclusion, interrupt
merging, timeout behavior, and duplicate-request handling before the handoff
path is substantially changed.

V1 supports a homogeneous execution backend inside one VM:

- KVM frontend to KVM slave execution.
- TCG frontend to TCG slave execution.

Mode A and Mode B may still be mixed between nodes because kernel acceleration
is independent of the execution backend. KVM-to-TCG or TCG-to-KVM vCPU handoff
is not a V1 capability. It must fail capability negotiation rather than attempt
an implicit conversion.

## 7. Multi-VM Model

### 7.1 VM Identity

WaveVM uses composite IDs for network-visible node identity:

```text
31        24 23                  0
+----------+----------------------+
|  vm_id   |       node_id        |
+----------+----------------------+
```

The identity terms are distinct:

- `vm_id` is the current 8-bit wire namespace allocated to a VM. It is not a
  durable identity by itself.
- `vm_incarnation` is a generation created for each successful VM creation. It
  prevents delayed traffic from an old VM lifetime entering a new VM that
  reuses the same `vm_id`.
- `physical_node_id` identifies a registered resource-providing host in the
  cluster.
- `vnode_id` identifies a DHT/routing slot inside one VM namespace. A physical
  node may own multiple vnodes.
- `primary_vnode` is the designated vnode used to identify a physical node in
  code paths that currently accept only one raw node ID. It is not the physical
  node ID.
- `WVM_INSTANCE_ID` is a local runtime discriminator used to derive socket and
  file names. It may differ between physical nodes participating in the same VM
  and must not be used as a network identity.
- A process instance ID is diagnostic identity for one process lifetime. It is
  not a VM, node, or routing identity.
- A network-visible composite ID is the current encoding of `{vm_id,
  vnode_id}` in `slave_id` or `target_id`.

V1 identity rules:

- Every network route and packet belongs to exactly one VM namespace.
- Network paths must carry composite IDs, except legacy `vm_id=0` traffic.
- DHT-internal tables may use raw vnode IDs only where the enclosing per-VM
  runtime context makes the namespace unambiguous.
- Physical node IDs and vnode IDs occupy different semantic domains even when
  their numeric values happen to match.
- `WVM_INSTANCE_ID` must never be treated as an alias for `vm_id`.
- The current wire header has no `vm_incarnation` field. Until the wire ABI is
  extended, a nonzero `vm_id` must not be reused while old routes, packets, or
  processes from its previous lifetime can still exist.
- The lifecycle and wire specifications must add incarnation validation before
  immediate `vm_id` reuse is supported. The existing node `epoch` field must
  not silently be repurposed as VM incarnation.

### 7.2 Per-VM Runtime Isolation

Each VM instance must have isolated:

- `vm_id`
- `vm_incarnation` once the protocol carries it
- `WVM_INSTANCE_ID`
- QEMU IPC socket path
- `WVM_SHM_FILE`
- master ingress port
- slave execution port
- gateway/sidecar route namespace or route keys
- QEMU monitor/serial/SSH forwarding ports
- log directory
- temp directory
- resource reservation
- page directory state
- vCPU route table
- memory placement table

No two VMs may share the same RAM backing file unless they are explicitly implementing a future shared-memory feature. Accidental SHM sharing is a correctness bug.

The generated runtime manifest is the source of all local names. Components
must not independently derive incompatible socket, SHM, port, or log names from
only a raw node ID.

### 7.3 Host Placement

A VM's host node is the physical node that runs the authoritative QEMU frontend for that VM.

Current limitation:

- Host placement is implicitly derived from the first vCPU placement block.

Long-term rule:

- Host placement must be explicit.
- The control plane must be able to accept a VM creation request on any physical node and then choose the host node.
- The host node does not have to be the node where the request was submitted.
- Host placement must account for QEMU/control-plane overhead, not only guest vCPU and guest RAM.

### 7.4 Admission Control

Creating a VM must be an atomic admission operation.

Admission must check:

- Total requested vCPUs fit in available cluster CPU capacity.
- Total requested memory fits in available cluster memory capacity.
- Per-node reservations do not exceed registered capacities.
- Host node has enough overhead headroom.
- Required ports and SHM names are available.
- Required accelerator capabilities exist if the VM requires them.
- Rollback is possible if any node fails to start its local components.

Static planning alone is not enough for long-term multi-VM operation.

## 8. Resource Model

### 8.1 Physical Node Registration

Each physical node registers capacity, not a mandatory NUMA boundary for the guest.

Registered capacity includes:

- CPU capacity available to WaveVM.
- Memory capacity available to WaveVM.
- Optional DHT/gateway weight.
- Capabilities such as KVM, TCG, kernel accelerator, storage, GPU, and network features.

The registered capacity is a scheduling input. It is not the same as automatic ownership of every resource by every VM.

### 8.2 VM Resource Request

A VM should request resources in user-facing terms:

- vCPU count
- memory size
- optional placement policy such as compact or spread
- optional host placement constraints
- optional accelerator requirement
- optional consistency policy
- optional device/storage requirements

Users should not be forced to manually select NUMA nodes for ordinary VM creation.

### 8.3 Placement Policy

Compact policy:

- Prefer fewer physical nodes.
- Reduce cross-node communication.
- Good for latency-sensitive workloads.
- Can concentrate multiple VM hosts unless host placement is separately balanced.

Spread policy:

- Balance resource usage across nodes.
- Increase fault isolation and use more aggregate bandwidth.
- May increase cross-node memory and vCPU traffic.

Neither policy replaces admission control or host placement.

### 8.4 Guest NUMA Presentation

Physical resource placement and guest-visible NUMA topology are separate decisions.

WaveVM may present:

- A flat guest topology for compatibility.
- A NUMA topology matching physical placement.
- A synthetic topology optimized for the guest OS scheduler.

The guest should not be forced into physical-node NUMA unless that is explicitly chosen.

## 9. Consistency Model

Current implementation baseline:

- User-space `page_meta_t` currently represents a directory entry with a page
  version, subscriber/copyset state, and page data.
- It does not currently contain a complete explicit MESI-style page-state enum
  or a documented exclusive-owner field.
- Existing message names such as invalidate, downgrade, and write-back do not
  by themselves prove that a complete MESI state machine exists.

V1 must first formalize the semantics of the existing directory, monotonically
versioned pages, and subscriber/copyset model in
`specs/memory-consistency.md`. An explicit owner or page-state field may be
added only when that specification demonstrates why it is required and defines
its transitions. Documentation and code must not claim stronger coherence than
the accepted contract actually provides.

### 9.1 Required Concepts

WaveVM must define memory consistency in terms of explicit concepts:

- Page state.
- Page owner or directory owner.
- Page version.
- Copyset or subscriber set.
- Dirty state.
- Handoff boundary.
- Acquire/fetch/release behavior.
- Invalidation or downgrade behavior.
- Error and timeout behavior.

These concepts must exist independently of whether KVM, TCG, Mode A, or Mode B is used.

The consistency specification is implementation-blocking. At minimum it must
define page states or equivalent predicates, every message transition,
authority and lock ordering, version/epoch rules, concurrent writers,
subscriber expiry, duplicate/reordered/delayed packets, timeout recovery, and
the exact barriers provided by each dirty synchronization policy.

### 9.2 Handoff Boundary

Remote vCPU execution is a causal boundary.

Before a vCPU state is handed to a remote execution node:

- Required CPU state must be serialized.
- Required interrupt/LAPIC/timer state must be serialized or explicitly retained locally.
- Dirty memory needed for correctness must be visible to the remote node.
- Pending invalidations that affect the remote slice must not be lost.

After remote execution returns:

- Returned CPU state must be merged without erasing locally delivered interrupts.
- Dirty memory updates produced by the remote node must be visible to the authoritative directory path.
- Errors must be propagated to QEMU rather than being hidden as successful execution.

### 9.3 Dirty Memory Policy

Dirty synchronization policy must be explicit.

Examples:

- Strong mode: flush every dirty page boundary or every configured single update.
- Batched mode: flush after N dirty events or a time threshold.
- Handoff mode: flush at vCPU migration/handoff boundaries.
- Lazy push mode: push asynchronously but force fetch on version gap.

A knob such as dirty-batch-size must not accidentally change correctness. It should only change when synchronization occurs under a documented policy.

For V1, `dirty-batch-size=1` means that each captured dirty update is submitted
to the consistency path without waiting to accumulate a second dirty update.
It does not by itself mean that every guest store is synchronously visible on
all nodes, and it does not replace acquire, invalidation, acknowledgement, or
handoff barriers. The formal specification must state which acknowledgement
completes that submission before the option may be advertised as strong
consistency.

### 9.4 Fault Handling

Mode B may use `mprotect`/SIGSEGV where that is the best available portable mechanism.

Long-term options:

- Prefer userfaultfd when the environment allows it.
- Keep SIGSEGV/mprotect as fallback for restricted hosts.
- Avoid requiring UFFD as a baseline because some cloud/container environments disable it.

KVM paths must avoid host page protection patterns that break EPT/KVM behavior.

### 9.5 Version Gaps

If a node observes a version gap:

- It may reorder within a bounded window.
- It must fall back to fetch/acquire if the gap cannot be closed.
- It must not blindly apply out-of-order updates that violate page version monotonicity.

## 10. Network and Routing Model

### 10.1 Production Cross-Node Rule

All cross-node production traffic must go through the sidecar/gateway fabric.

Allowed path:

```text
local component -> local master/sidecar -> gateway fabric -> remote sidecar/master -> remote slave if needed
```

Forbidden path:

```text
local QEMU/slave/master -> arbitrary remote slave directly
```

Exception:

- Explicit diagnostic tools may open direct test connections, but they must not become production runtime dependencies.

### 10.2 Routing Table Ownership

Routing table truth belongs to the user-space control plane.

The gateway executes routing. It does not invent placement.

The kernel accelerator may cache routing data, but cached state must be derived from user-space truth and must be safely invalidated or replaced.

### 10.3 VM Route Namespace

V1 route lookup rules are strict:

- Route configuration and runtime tables must preserve the complete composite
  `{vm_id, vnode_id}` key for every nonzero `vm_id`.
- If lookup of a nonzero composite target fails, routing must fail explicitly.
  It must not strip `vm_id` and retry with a raw vnode ID.
- Raw-node fallback is allowed only for legacy `vm_id=0` configuration and
  traffic.
- Flat and fractal gateways must preserve the same VM namespace at every level;
  hierarchy is not a namespace boundary.
- Route-table updates must be generation-based and atomically published. A
  packet is routed using one complete generation, never a partially updated
  table.
- `WVM_NODE_AUTO_ROUTE` is a sentinel, not an identity. Its resolution must
  produce a VM-scoped composite target before cross-node transmission.

The fallback currently present in `gateway_service/aggregator.c` is a migration
compatibility behavior, not the target contract. It must be restricted or
removed when the identity/routing specification is implemented.

### 10.4 Static vs Learned Routes

Static configured routes must not be overwritten by auto-learning.

Auto-learning may be used for diagnostics or dynamic discovery only when it cannot invalidate explicit routes.

Learned routes must be scoped by VM namespace and incarnation. Until
incarnation exists in the wire and route ABI, automatic route learning must not
make immediate `vm_id` reuse appear safe.

## 11. Storage and Device Model

Storage and device state are part of the guest-visible machine and must be treated as correctness-critical.

Rules:

- Device authority must be explicit.
- Remote execution must not assume device state magically follows the vCPU.
- MMIO/PIO/APIC/IOAPIC paths must have explicit forwarding or local emulation rules.
- Block write errors must reach the guest path.
- Flush operations must be real flushes unless explicitly configured otherwise for unsafe testing.

Long-term device distribution may remain incomplete, but the architecture must not hide that gap.

## 12. Environment Capability Model

WaveVM should detect host capabilities and choose the best safe path.

Capabilities include:

- KVM availability.
- TCG availability.
- Kernel accelerator availability.
- userfaultfd availability.
- Allowed mprotect behavior.
- Huge pages.
- NUMA information.
- Network transport features.
- Root privileges.

Capability selection rule:

```text
If a faster capability is absent, fall back to a slower correct path.
If no correct path exists, fail early with a clear reason.
```

Do not silently replace a required feature with a workaround that changes semantics.

## 13. Mode A Repositioning

Current Mode A is too large semantically. It contains kernel-side logic that overlaps user-space master responsibilities.

Target Mode A shape:

```text
User-space master owns semantics.
Kernel module accelerates selected data-plane operations.
```

The kernel accelerator should expose capabilities such as:

- `WVM_CAP_DIRTY_CAPTURE`
- `WVM_CAP_FAST_INVALIDATE`
- `WVM_CAP_FAST_LOCAL_FORWARD`
- `WVM_CAP_WAITQUEUE_WAKEUP`
- `WVM_CAP_FAST_PACKET_TX`

The user-space master decides which capabilities to use.

## 14. Multi-VM and Kernel Accelerator

V1 chooses an explicit per-VM kernel context, not an implicit per-open context.
One VM may legitimately have several processes and file descriptors using
`/dev/wavevm`, including QEMU, master, slave, and control tooling. Creating an
unrelated VM context for every `open()` would split state that must be shared.

The kernel ABI must therefore provide explicit create/attach semantics:

- A create operation creates one context identified by VM identity and returns
  an unforgeable kernel context handle.
- Additional file descriptors explicitly attach to that context after identity
  and permission validation.
- Every mmap, ioctl, request ID, route cache, mapping, dirty tracker, and page
  metadata lookup is resolved through the attached context.
- Closing one file descriptor releases only its reference. The context is
  destroyed after lifecycle teardown and the final reference.
- Module-global workers may remain only when their queues carry a context
  reference and cannot mix request namespaces.

The exact handle representation, ioctl numbers, permissions, reference model,
and teardown lock order belong in the kernel-accelerator specification.

Target invariants:

- No module-global VM identity.
- No module-global route table as semantic truth.
- No module-global memory mapping pointer that can be overwritten by another VM.

Until explicit contexts exist, Mode A must be treated as
single-VM-per-physical-node. Admission must reject a second concurrent Mode A
VM rather than allowing global state to be overwritten.

## 15. Invariants

The following invariants should guide future code changes:

- Mode B is the semantic baseline.
- Kernel code accelerates, it does not define correctness.
- KVM and TCG share high-level semantics.
- Flat and fractal topologies share high-level semantics.
- Multi-VM isolation is required by default.
- A VM creation request is not a placement decision.
- Host placement is explicit, not inferred from node ID ordering.
- Resource capacity does not include hidden control-plane overhead unless accounted for.
- Cross-node production traffic uses the gateway fabric.
- Test scripts may constrain topology, but production code must not hardcode those constraints.

## 16. V1 Decisions and Deferred Work

The following defaults are architectural decisions for the first maintainable
implementation. Subsystem specifications must fill in their algorithms and
ABIs without choosing contradictory alternatives.

- A user-facing CLI or API submits a versioned VM request containing requested
  resources, policy, and constraints. After admission, the control plane
  generates an immutable versioned launch manifest containing resolved
  identities, placement, capabilities, and per-node runtime configuration.
  Handwritten scripts are not the semantic authority for either object.
- The admitted plan contains an explicit `host_node`. The planner chooses it
  from capable nodes; it is neither inferred from the request origin nor from
  the first numeric vnode.
- Host overhead is an explicit per-node reservation separate from guest vCPU
  and RAM. An implementation may begin with conservative configured defaults,
  but it may not pretend the overhead is zero.
- VM startup is coordinated as prepare, start, commit, or abort. V1 is
  all-or-nothing: a partially started VM is not reported as running, and every
  resource created before failure has a named cleanup owner.
- The node accepting a V1 create request acts as lifecycle coordinator for that
  operation. It may select a different `host_node` for QEMU. Coordinator crash
  recovery is not durable in V1; reservations use bounded leases, and
  participants clean up uncommitted resources after lease expiry.
- Nonzero VM route namespaces use strict composite keys at every flat or
  fractal gateway level. Raw-ID fallback is legacy behavior for `vm_id=0` only.
- The default guest topology is flat for compatibility. A placement-aware or
  synthetic NUMA topology is an explicit manifest choice and must not alter
  physical placement ownership.
- `dirty-batch-size=1` has the limited submission meaning defined in Section
  9.3. Strong consistency is advertised only after the consistency
  specification defines and the implementation supplies the required ACK and
  handoff barriers.
- Sending `VCPU_RUN` and accepting completion of `VCPU_EXIT` are handoff
  boundaries. The handoff specification determines the exact memory scope and
  barrier sequence; neither backend may skip it as an optimization.
- UFFD is an optional fault engine. SIGSEGV/mprotect remains the restricted-host
  fallback where valid, and KVM dirty logging remains a distinct capability.
- The kernel accelerator uses the explicit per-VM context model from Section
  14. Until implemented, Mode A is single-VM-per-physical-node.
- The authoritative QEMU frontend owns the V1 guest device model. Remote vCPU
  execution returns MMIO/PIO and unsupported device exits to that authority.
  A remote block data service may execute I/O, but ordering, flush completion,
  and guest-visible errors remain governed by the QEMU-host device authority.
- The current 8-bit wire `vm_id` permits at most 256 simultaneous wire
  namespaces cluster-wide. Mode B has no smaller architectural per-host count;
  admission is constrained by reserved resources and exclusive local names.
  Immediate ID reuse is unsupported until incarnation is carried and checked.

The following remain future design work rather than V1 alternatives:

- Cross-backend KVM-to-TCG or TCG-to-KVM vCPU handoff.
- Live migration of the QEMU frontend or dynamic replanning of a running VM.
- Highly available or durable lifecycle coordination after coordinator failure.
- Automatic selection of an optimized synthetic guest NUMA topology.
- Distributed ownership of arbitrary device models beyond the defined remote
  block primitives.
- A wider VM namespace after the current 8-bit wire ABI is retired.
- A tested scale limit lower than the protocol limit, based on measured file
  descriptors, ports, memory overhead, and gateway capacity.

## 17. Relation to GiantVM

GiantVM is a useful architectural reference for distributed KVM DSM concepts, especially page ownership, versioning, copysets, and explicit interrupt/device forwarding.

However, GiantVM is not WaveVM's baseline architecture:

- GiantVM is KVM-only.
- GiantVM requires a custom host kernel.
- GiantVM uses direct QEMU-to-QEMU routing.
- GiantVM does not solve distributed TCG.
- GiantVM does not provide WaveVM's intended mode-B restricted-environment path.

WaveVM may borrow design discipline, not hard dependencies.

## 18. Immediate Architectural Decision

The project should stop expanding Mode A as a separate implementation.

Future work should proceed as:

```text
1. Specify shared consistency, handoff, identity, routing, and lifecycle semantics.
2. Implement those semantics first through the canonical Mode B authority.
3. Make KVM and TCG obey the same contracts through backend-specific mechanics.
4. Add optional kernel acceleration only where it preserves those contracts.
5. Keep multi-VM, placement, admission, and lifecycle in user-space control plane.
```
