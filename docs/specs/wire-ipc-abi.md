# WaveVM Wire and Local IPC ABI Specification

Status: proposed implementation specification.

Scope: network framing, header validation, byte order, payload bounds, request
identity, QoS, duplicate/retry semantics, local QEMU-to-node-runtime IPC,
node-runtime-to-executor IPC, compatibility rules, and Mode A IOCTL boundary.
This document defines transport contracts only; page state, vCPU state,
membership, placement, and block semantics belong to their owning subsystem
specifications. The admitted manifest and capability profile decide which
participants and ABI features are valid; this document does not make those
admission decisions.

The current ABI is not a stable production ABI. It contains legacy overloads,
fixed-layout host-native local IPC, `SYNC_MAGIC`, raw-ID fallback, and messages
whose payload or idempotency semantics are incomplete. New work must target the
contract below and retain current decoding only as bounded compatibility.

## 1. Framing and Versioning

Every network packet and local IPC record has an explicit versioned envelope.
Receivers validate the full envelope before interpreting message-specific data.

### 1.1 Current Network Header

The current packed `struct wvm_header` contains:

```text
magic, msg_type, payload_len,
slave_id, target_id, req_id/target_gpa,
qos_level, flags, mode_tcg, node_state, epoch, crc32
```

Current multi-byte network fields are network byte order except fields inside
some legacy payloads. That inconsistency is not a valid future contract.

The current header remains a legacy decoder only. New semantic traffic uses the
fixed V1 envelope below. No receiver may infer version from payload length
alone.

### 1.2 Current Local IPC Header

Current QEMU IPC starts with:

```text
wvm_ipc_header_t { uint32_t type; uint32_t len; }
```

The current local header remains a compatibility framing prefix. New local IPC
uses the same semantic envelope as network traffic, optionally preceded by a
four-byte big-endian stream frame length. Local Unix sockets, shared memory,
an in-process call, and a kernel ioctl may use different transport mechanics,
but they must expose the same fields and validation result. Connection arrival
order is never identity: QEMU explicitly registers `SYNC` and `ASYNC_PUSH`
roles before using a channel.

### 1.3 WVM Envelope V1

All integer fields are unsigned big-endian values. The fixed V1 header is a
packed 164-byte sequence with no compiler padding. `operation_id` and digests
are raw bytes in network order.

| Offset | Type | Field | Required meaning |
| --- | --- | --- | --- |
| 0 | `u32` | `magic` | ASCII `WVM1` (`0x57564d31`). |
| 4 | `u16` | `protocol_version` | Exactly `1` for this layout. |
| 6 | `u16` | `header_bytes` | Exactly `164`. |
| 8 | `u16` | `message_type` | Registry-defined data/control/local operation. |
| 10 | `u16` | `flags` | Typed non-overlapping flags; unknown set bits reject. |
| 12 | `u32` | `payload_bytes` | Bytes following this header in this frame/fragment. |
| 16 | `u32` | `vm_id` | Logical namespace; zero only for explicitly legacy/control traffic. |
| 20 | `u64` | `vm_incarnation` | Required for all nonlegacy VM operations. |
| 28 | `u64` | `manifest_generation` | Candidate/admitted manifest generation; zero only for cluster registration. |
| 36 | `u32` | `origin_physical_node_id` | Registered source host identity. |
| 40 | `u64` | `origin_runtime_instance_id` | Fresh source runtime/process identity. |
| 48 | `u8[16]` | `operation_id` | Nonzero 128-bit operation/control transaction identity. |
| 64 | `u64` | `delivery_attempt_id` | Mutable forwarding attempt; begins at one for a semantic operation. |
| 72 | `u64` | `route_scope_id` | Required for routed VM traffic; zero for non-route control records. |
| 80 | `u64` | `topology_revision` | Topology revision of the selected route snapshot. |
| 88 | `u64` | `route_generation` | Generation within the route scope. |
| 96 | `u8[32]` | `route_snapshot_digest` | SHA-256 digest of the full route snapshot; zero only when no route applies. |
| 128 | `u8[32]` | `semantic_payload_digest` | SHA-256 of the unfragmented semantic payload, excluding forwarding metadata. |
| 160 | `u32` | `crc32c` | CRC32C over bytes 0-159 and the frame payload bytes. |

The complete semantic operation key is `{vm_id, vm_incarnation,
origin_physical_node_id, origin_runtime_instance_id, operation_id}`. An origin
runtime creates `operation_id` values from a nonwrapping 128-bit sequence or a
cryptographically random 128-bit source; a new runtime instance must never
reuse its predecessor's `origin_runtime_instance_id`. UDP source address and
port are transport evidence only, never this identity.

`delivery_attempt_id`, `route_scope_id`, topology revision, route generation,
and route digest are forwarding metadata. They are excluded from
`semantic_payload_digest`; route refresh may replace them without changing the
semantic deduplication key, request bytes, or operation result.

### 1.4 Canonical Records, Size Limits, and Fragmentation

Manifest, route snapshot, and control payloads use canonical WVM-TLV records:

```text
CanonicalRecord {
    u16 schema_version;
    u16 record_type;
    u32 body_bytes;
    CanonicalField fields[];      // strictly ascending field_tag, no duplicate
}

CanonicalField {
    u16 field_tag;
    u16 field_flags;
    u32 value_bytes;
    u8[value_bytes] value;
}
```

All scalar field values use the same big-endian encoding as the envelope.
Nested records use canonical bytes; lists are ordered by their explicit key.
The exact record-type registry, tag numbers, scalar widths, list keys,
cross-field constraints, self-digest rule, and interoperation fixtures are
normative in `canonical-record-schema.md`. Unknown required fields and
duplicate/out-of-order tags reject. A `manifest_digest` or
`route_snapshot_digest` is SHA-256 over the complete canonical record,
including its header, with only the documented self-digest field zeroed during
calculation.

V1 limits are protocol safety limits, not hidden test parameters:

| Item | Limit |
| --- | --- |
| Local stream/in-process frame payload | 4 MiB maximum. |
| Logical network payload | 1 MiB maximum. |
| Network datagram/frame | 1280 bytes maximum, including V1 header. |
| Fragment data | 1024 bytes maximum. |
| Fragments per logical payload | 1024 maximum. |
| Concurrent incomplete reassemblies | 64 per VM incarnation and 8 MiB aggregate per origin. |
| Reassembly lifetime | 5 seconds; expiry returns/drops with bounded `FRAGMENT_EXPIRED` diagnostics. |

When `FLAG_FRAGMENTED` is set, the frame payload begins with this 40-byte
fragment prefix followed by at most 1024 bytes of logical payload:

```text
FragmentPrefix {
    u32 logical_payload_bytes;
    u16 fragment_index;
    u16 fragment_count;
    u32 fragment_offset;
    u8[28] reserved_zero;
}
```

The reassembly key is the complete semantic operation key plus message type and
delivery attempt. Receivers validate bounds, nonoverlap, full logical digest,
and all fragments before dispatch. No path relies on IP fragmentation. A
message that exceeds the limits fails with `PAYLOAD_TOO_LARGE` unless a selected
reliable transport with an explicitly negotiated larger limit is documented in
the capability profile.

## 2. Common Validation Rules

Before decoding payload fields, every receiver verifies:

1. Exact V1 header length, declared length, protocol limits, and no integer
   overflow in header-plus-payload calculation.
2. Magic, protocol version, known message type, and known flag combination.
3. CRC32C/integrity check before state mutation; fragmented messages additionally
   validate their completed SHA-256 semantic payload digest.
4. VM namespace/incarnation, manifest generation, origin physical/runtime
   identity, and nonzero operation ID where the operation requires them.
5. Full route snapshot key/digest and local accepted generation where routed
   delivery applies. A refreshed route is forwarding metadata, not a new
   semantic operation.
6. Source and target role validity; a gateway routes but does not become a
   destination executor.
7. Payload-specific canonical-record structure, minimum/maximum length, and
   exact field schema before access.

Malformed, unknown-version, wrong-namespace, or impossible-length traffic is
dropped with bounded diagnostics. It must not reach `logic_core`, QEMU RAM,
KVM, TCG, a block backend, or a kernel cache.

All new network payload fields use a documented fixed byte order. Packed C
structures may be an implementation of a fixed ABI only after their field
sizes, alignment, padding, and endian conversion are explicitly specified.
Host-native local IPC is allowed only when both endpoints negotiate the same
build ABI; its eventual stable replacement must be portable.

## 3. Operation Identity and Response Rules

Every operation that can mutate guest-visible state has a nonzero 128-bit
`operation_id` scoped to `{vm_id, vm_incarnation, origin_physical_node_id,
origin_runtime_instance_id}`. The current `req_id` is its compatibility
predecessor. A receiver keys deduplication and result lookup on that complete
tuple plus operation class and semantic payload digest.

| Operation class | Identity rule | Duplicate behavior |
| --- | --- | --- |
| Read/fault lookup | Same semantic key/digest for retry. | Return the same or a newer authoritative result. |
| Versioned memory commit | Same semantic key/digest for retry. | Return cached completion; reject same key with different payload. |
| `VCPU_RUN` | One handoff sequence per `{VM, vCPU}` plus one semantic key. | Query/wait or replay cached `VCPU_EXIT`; never execute twice. |
| Block write/flush | Same semantic key for retry and ordered write sequence. | Return cached completion only after required backend completion. |
| Membership/route/manifest control | One controller transaction operation ID. | Idempotently acknowledge the recorded state and record digest. |

An ACK/result must echo the complete semantic/control operation key, include a
typed status, and carry the result fields required by that operation. A one-byte
generic ACK or `SYNC_MAGIC` cannot be a stable completion signal for a memory,
vCPU, block, route, reservation, or lifecycle operation.

## 4. Network Message Families

The following table defines target direction and status. Exact payload schemas
belong to the referenced specifications.

| Family | Current messages | Direction | Contract owner |
| --- | --- | --- | --- |
| Memory read | `MSG_MEM_READ`, `MSG_MEM_ACK` | node runtime -> directory -> node runtime | `memory-consistency.md` |
| Memory commit/push | `MSG_COMMIT_DIFF`, `MSG_PAGE_PUSH_DIFF`, `MSG_PAGE_PUSH_FULL`, `MSG_FORCE_SYNC` | client/directory as specified | `memory-consistency.md` |
| Legacy coherence | `MSG_MEM_WRITE`, `MSG_INVALIDATE`, `MSG_DOWNGRADE`, `MSG_FETCH_AND_INVALIDATE`, `MSG_WRITE_BACK` | Compatibility only until mapped | `memory-consistency.md` |
| vCPU handoff | `MSG_VCPU_RUN`, `MSG_VCPU_EXIT` | origin node runtime -> destination node runtime/executor -> origin | `vcpu-handoff.md` |
| Interrupt | `MSG_VFIO_IRQ` and local IRQ IPC | executor/device -> node runtime -> QEMU authority | `vcpu-handoff.md` and `storage-device-authority.md` |
| Block I/O | `MSG_BLOCK_READ`, `MSG_BLOCK_WRITE`, `MSG_BLOCK_FLUSH`, `MSG_BLOCK_ACK` | QEMU authority -> node runtime -> assigned storage executor -> return | `storage-device-authority.md` |
| Membership/view | `MSG_HEARTBEAT`, `MSG_VIEW_PULL`, `MSG_VIEW_ACK`, `MSG_NODE_ANNOUNCE` | Diagnostic/compatibility only | `cluster-membership-topology-lifecycle.md` |
| Gateway control | Current `WVMC` add/update packet | Control plane -> gateway | `cluster-membership-topology-lifecycle.md` |

`MSG_PING` and `SYNC_MAGIC` are legacy compatibility mechanisms. They cannot
serve as typed vCPU fences, route commits, or authoritative membership changes.

### 4.1 Control Transport and Result Record

Control RPCs use the same V1 envelope over a reliable, ordered control channel
that is independent of the gateway being drained or replaced. A remote control
channel may be a configured authenticated stream transport; same-host control
may use a protected Unix socket. A data-plane UDP next hop is never the sole
way to obtain a control ACK for replacing that next hop.

Every control request uses one header `operation_id` as its transaction ID and
one canonical request record. The response uses `CTRL_RESULT` (`0x01ff`) and
this fixed 72-byte payload:

```text
ControlResultV1 {
    u16 status_code;
    u16 recorded_state;
    u32 result_flags;
    u8[16] in_reply_to_operation_id;
    u8[32] record_digest;
    u64 applied_revision;
    u64 expiry_or_retention_deadline;
}
```

`status_code=SUCCESS` means the receiver durably recorded the stated result,
not merely that it queued work. A duplicate request with the same operation ID
and semantic digest replays the same result. The same ID with a different
digest is `OPERATION_ID_CONFLICT`.

| Code | Status |
| --- | --- |
| `0` | `SUCCESS` |
| `1` | `INVALID_ENVELOPE` |
| `2` | `INVALID_RECORD` |
| `3` | `UNAUTHORIZED_ROLE` |
| `4` | `STALE_INSTANCE` |
| `5` | `ELIGIBILITY_FENCE_STALE` |
| `6` | `PRECONDITION_FAILED` |
| `7` | `OPERATION_ID_CONFLICT` |
| `8` | `NOT_FOUND` |
| `9` | `RESULT_EXPIRED` |
| `10` | `BACKPRESSURE` |
| `11` | `UNSUPPORTED` |
| `12` | `INTERNAL_FAILURE` |

### 4.2 Normative Control Message Schemas

Control payloads are `CanonicalRecord`s. `ID16`, `Digest32`, `Endpoint`,
`MemberKey`, `SnapshotKey`, and every referenced record have the exact schema
in `canonical-record-schema.md`. Fields listed in order use ascending canonical
tags starting at one; no listed field is optional unless marked optional.

| Type | Message | Mandatory request fields | Preconditions | Success result / retention |
| --- | --- | --- | --- | --- |
| `0x0101` | `REGISTER_MEMBER` | `MemberKey`, `hosting_physical_node_id:u32`, `failure_domain_id:u64`, roles, `Endpoint`, capacity record, capability `Digest32`, requested topology record | `vm_id=0`, fresh member instance, authenticated registrar | Registered desired-state record; retained through member removal/quarantine. |
| `0x0102` | `CORDON` | `MemberKey`, expected `membership_revision:u64`, reason code | Exact member revision and control authorization | New membership/eligibility revision; retained through removal/quarantine. |
| `0x0103` | `DRAIN` | `MemberKey`, expected revision, dependency report digest, requested drain kind | No unacknowledged conflicting control transaction | Recorded drain plan/rejection; retained through completion. |
| `0x0201` | `PREPARE_RESERVATION` | `admission_tx_id:ID16`, candidate `Digest32`, eligibility-fence `Digest32`, plan `Digest32`, expected `MemberKey`, resource reservation record, `prepared_expiry:u64` | Member/capability/fence still eligible and local capacity/name lease free | Prepared reservation ID/digest/expiry; retained until abort or expiry if no activation fence. |
| `0x0202` | `COMMIT_RESERVATION` | `admission_tx_id`, candidate digest, eligibility-fence digest, `activation_fence:ID16`, reservation ID | Matching unexpired prepared record and durable activation decision | Committed reservation digest; retained through VM retirement. |
| `0x0203` | `ABORT_RESERVATION` | `admission_tx_id`, candidate digest, reservation ID, reason | No matching activation fence, or explicit compensating-teardown record | Released/teardown-pending result; retained through cleanup horizon. |
| `0x0301` | `PREPARE_MANIFEST` | `admission_tx_id`, full candidate manifest record/digest, eligibility-fence digest, required `SnapshotKey`, local role record, reservation IDs | Exact member instance, route snapshot, capability profile, and prepared reservation | Prepared local-manifest record/expiry; retained until abort/activation. |
| `0x0302` | `ACTIVATE_MANIFEST` | `admission_tx_id`, candidate digest, `activation_fence`, required snapshot key, reservation IDs | Matching prepared record and unchanged fence | Activated local-manifest result; retained through VM retirement. |
| `0x0303` | `ABORT_MANIFEST` | `admission_tx_id`, candidate digest, reason | No activation, or compensating teardown after activation | Removed/teardown-pending result; retained through cleanup horizon. |
| `0x0304` | `QUERY_TX` | target `operation_id:ID16`, candidate digest optional, activation fence optional | Caller may query only a role/transaction it is authorized to inspect | Recorded transaction state/digest/expiry; retained as the target record requires. |
| `0x0401` | `ROUTE_PREPARE` | `SnapshotKey`, snapshot canonical record, predecessor key optional, `RequiredAckSet` record/digest, eligibility-fence digest, operation-retention horizon | All ACK-set entries are surviving eligible members; departing/failed gateway absent from required set | Prepared snapshot result; retained until activate/abort. |
| `0x0402` | `ROUTE_COMMIT` | `SnapshotKey`, route transaction ID, required-ACK-set digest | Matching prepared snapshot and persisted required ACK set | Activated snapshot result; retained through predecessor retirement. |
| `0x0403` | `ROUTE_RETIRE` | retiring `SnapshotKey`, successor key optional, route transaction ID | No active operation reference and query/retry horizon complete, or typed terminal outcome recorded | Retired snapshot result; retained through VM namespace quarantine. |
| `0x0501` | `REJOIN` | new `MemberKey`, prior physical role ID optional, capability digest, endpoint, recovery evidence | Fresh registration only; no implicit use of old instance identity | `VALIDATING` membership result. It never rebinds a running V1 VM. |
| `0x0502` | `RECOVERY_REBIND` | old/new `MemberKey`, manifest/snapshot digests, reservation proof, memory/vCPU/storage recovery proof | Not supported in V1 without a separately accepted recovery specification | `UNSUPPORTED` in V1; retained as audit evidence. |

`RequiredAckSet` is a canonical ordered list of `MemberKey`, expected endpoint,
role, and snapshot key. It includes only surviving eligible node runtimes and
gateways that must install a successor before normal traffic can use it, plus a
new gateway being activated. A departing `DRAINING`, `FAILED`, or `REMOVED`
gateway may send optional drain status but is never a required successor ACK.

### 4.3 Manifest and Route Records

`PREPARE_MANIFEST` carries the exact `CandidateVmManifest` schema from
`canonical-record-schema.md`. Its digest covers every semantic field and uses
the documented self-digest preimage rule.

`ROUTE_PREPARE` carries the exact `RouteSnapshot` schema from
`canonical-record-schema.md`. No receiver accepts a field-by-field route update
as a V1 route transaction.

## 5. Local IPC and Executor ABI

The canonical local message families are:

| Current type | Target semantic operation | Required response |
| --- | --- | --- |
| `WVM_IPC_TYPE_REGISTER` | Bind QEMU connection role and identity. | Registration ACK with accepted versions/roles. |
| `WVM_IPC_TYPE_MEM_FAULT` | Request an authoritative page through local node runtime. | Typed page/status response. |
| `WVM_IPC_TYPE_MEM_WRITE` / `COMMIT_DIFF` | Submit captured dirty data. | Versioned commit result when required by fence policy. |
| `WVM_IPC_TYPE_COMMIT_DIFF_SYNC` | Complete a handoff-scoped memory fence. | Fence result tied to operation sequence. |
| `WVM_IPC_TYPE_CPU_RUN` | Request local/remote vCPU handoff. | `wvm_ipc_cpu_run_ack` successor with exact status and context. |
| `WVM_IPC_TYPE_IRQ` / `INVALIDATE` | Deliver a typed guest event or correction. | Explicit delivery/backpressure result where guest progress depends on it. |
| `WVM_IPC_TYPE_BLOCK_IO` | Submit block request to device authority. | Typed block completion. |
| `PUSH_BARRIER` / `PUSH_BARRIER_ACK` | Compatibility barrier. | Replace with typed VM-scoped fence record. |
| `RPC_PASSTHROUGH` | Transitional wrapper only. | Must declare embedded type/length/version; no blind forwarding. |

The node-runtime-to-executor ABI is a local variant of these operations. It
must include VM identity/incarnation, manifest and route generation, operation
identity, target executor role, cancellation state, ordering/fence reference,
and typed completion. An in-process executor may use a function call, but it
must not lose those checks or turn normal traffic into a global synchronous
critical section.

The executor ABI carries the candidate manifest digest, the full required
route-snapshot key (scope, topology revision, route generation, and snapshot
digest), and the activation fence. It is a single admitted-format ABI; older
frames are rejected at decode time rather than retained as a compatibility
path.

The local registration payload is a canonical `LocalRegisterV1` record with
these mandatory fields: `connection_role:u16`, `vm_id:u32`,
`vm_incarnation:u64`, `manifest_generation:u64`, candidate/admitted manifest
digest, `local_runtime_instance_id:u64`, caller process instance ID,
capability-profile digest, and requested local endpoint name. The registration
result is a `ControlResultV1`-compatible status plus accepted role and local
connection ID. No local QEMU/executor connection may send semantic traffic
until this registration is accepted.

For an IOCTL, a 24-byte kernel-context prefix
`{context_handle:u64, attach_capability:u8[16]}` precedes the V1 envelope.
The kernel validates the context/manifest role before parsing the envelope;
the prefix does not replace VM/incarnation/operation identity fields.

## 6. QoS, Size, and Backpressure

`qos_level` is a scheduling hint, not a correctness exemption. The current
fast handling of vCPU run/exit and control ACKs is retained as a normal-path QoS
rule. It must not let a message bypass identity, checksum, idempotency, or
ordering validation.

The V1 size/fragmentation limits are defined in Section 1.4. Each capability
profile additionally publishes actual local queue capacities and transport
limits bounded by those maxima. Per-message rules are:

- Control, route, registration, vCPU run/exit, required memory fence, and
  durable block completion are non-droppable. Queue saturation yields a typed
  bounded backpressure/error result.
- Independent page diffs and non-fence HINT pushes may batch/coalesce only when
  page version order remains valid. They may transition a page/subscriber to
  `RESYNC`/`STALE`; they may not be silently dropped while marked valid.
- Reordering is allowed only between independent operation keys. An executor or
  device queue preserves its specified vCPU/virtqueue order domain.
- Fragment/reassembly pressure rejects new frames before exceeding V1 bounds;
  it never evicts a partially received semantic message and reports success.

The current `WVM_MAX_PACKET_SIZE` and `MTU_SIZE` are implementation constants,
not sufficient ABI documentation. A full 4 KiB page or large context must not
depend on accidental IP fragmentation as a correctness mechanism.

## 7. Mode A IOCTL Boundary

Mode A is an accelerator implementation of the same local ABI, not another
distributed protocol. Current IOCTLs are classified as follows:

| IOCTL | Target role |
| --- | --- |
| `IOCTL_WVM_REMOTE_RUN` | Optional context-bound acceleration of the vCPU handoff contract. |
| `IOCTL_SET_MEM_LAYOUT` | Per-VM QEMU RAM registration cache. |
| `IOCTL_WAIT_IRQ` | Per-VM local interrupt wait/notification. |
| `IOCTL_SET_GATEWAY`, route, placement, VM ID, epoch updates | Derived manifest/route cache refresh only; never truth injection. |
| `IOCTL_RPC_SYNC_ACK` | Remove or give a typed owner before stable ABI claim; no current handler exists. |

Every ioctl and mmap must resolve an attached `wvm_kernel_ctx` with VM identity,
incarnation, manifest generation, and lifetime validation. The ioctl number and
payload layout are not stable until `kernel-accelerator.md` accepts them.

## 8. Compatibility and Migration

1. Inventory all current message producers and consumers before changing an
   existing numeric message type.
2. Add protocol-version negotiation and typed error decoding before emitting a
   next-format packet on a mixed deployment.
3. Introduce new envelopes in parallel with legacy readers; never reinterpret
   a legacy payload based solely on a flag or a guessed size.
4. Move memory, vCPU, block, and membership operations to typed completions.
5. Retire `SYNC_MAGIC`, raw-ID fallback, untyped one-byte ACKs, and blind
   passthrough only after all corresponding contract tests pass.

## 9. Acceptance Tests

- Every supported message has a decoder test for truncated, oversized,
  unknown-version, bad-CRC, invalid-flag, and namespace-mismatch input.
- A local QEMU connection cannot send semantic traffic before successful role
  registration.
- A packet with a nonzero VM ID cannot fall back to a raw route lookup.
- Duplicate memory commits, vCPU runs, block writes, and route prepares return
  their documented idempotent result.
- A payload larger than one safe datagram follows the declared fragmentation or
  transport rule without accidental IP-fragmentation dependence.
- Every V1 network/local/control decoder rejects a wrong header length, origin
  identity, snapshot digest, canonical field order, missing mandatory field,
  duplicate operation ID with changed digest, and oversized fragment.
- Coordinator crash/restart after each `PREPARE_*`, `ACTIVATE_*`,
  `COMMIT_RESERVATION`, `ABORT_*`, `ROUTE_*`, `CORDON`, and `DRAIN` message
  replays one recorded result; it cannot create an orphaned reservation or a
  partially published route.
- A failed/departing gateway is never awaited as a required successor route ACK;
  every survivor ACK is received over the independent control path or the
  operation returns a bounded degraded/failed result.
- QoS queues preserve high-priority vCPU/control delivery under data load while
  retaining bounded backpressure and validation.
- KVM, TCG, Mode A, and Mode B use equivalent local-operation envelopes even
  when their capture/acceleration mechanics differ.
