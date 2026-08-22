#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "wavevm_canonical.h"
#include "wavevm_control_plane.h"
#include "wavevm_control_service.h"

#define MIB (1024ULL * 1024ULL)

struct authorization_context {
    struct wvm_member_key actor;
    unsigned int transport_calls;
    unsigned int membership_calls;
};

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "control-service test: %s\n", message);
        return -1;
    }
    return 0;
}

static int member_key_equal(const struct wvm_member_key *left,
                            const struct wvm_member_key *right)
{
    return left && right && left->role_type == right->role_type &&
           left->role_id == right->role_id &&
           left->instance_id == right->instance_id;
}

static int authenticate_actor(void *opaque, int stream_fd,
                              struct wvm_member_key *actor, char *error,
                              size_t error_len)
{
    struct authorization_context *context = opaque;

    (void)stream_fd;
    (void)error;
    (void)error_len;
    if (!context || !actor) {
        return -1;
    }
    *actor = context->actor;
    context->transport_calls++;
    return 0;
}

static int authorize_self(
    void *opaque, enum wvm_membership_controller_authorization_action action,
    const struct wvm_member_key *actor, const struct wvm_member_key *subject,
    char *error, size_t error_len)
{
    struct authorization_context *context = opaque;

    (void)action;
    (void)error;
    (void)error_len;
    if (!context || !member_key_equal(actor, subject) ||
        !member_key_equal(actor, &context->actor)) {
        return -1;
    }
    context->membership_calls++;
    return 0;
}

static void fill_endpoint(struct wvm_endpoint *endpoint)
{
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->data_transport = WVM_DATA_TRANSPORT_UDP;
    endpoint->data_address_bytes = 4;
    endpoint->data_address[0] = 192;
    endpoint->data_address[1] = 0;
    endpoint->data_address[2] = 2;
    endpoint->data_address[3] = 17;
    endpoint->data_port = 19120;
    endpoint->control_transport = WVM_CONTROL_TRANSPORT_TLS_TCP;
    endpoint->control_port = 19121;
}

static void fill_node(struct wvm_node_record *node)
{
    memset(node, 0, sizeof(*node));
    node->physical_node_id = 17;
    node->node_instance_id = 101;
    node->failure_domain_id = 3;
    fill_endpoint(&node->control_endpoint);
    fill_endpoint(&node->sidecar_endpoint);
    node->role_bits = 1;
    node->pod_id = 1;
    node->local_vnode_count = 16;
    node->inventory.physical_node_id = node->physical_node_id;
    node->inventory.node_instance_id = node->node_instance_id;
    node->inventory.failure_domain_id = node->failure_domain_id;
    node->inventory.inventory_revision = 1;
    node->inventory.registered_vcpu_slots = 8;
    node->inventory.registered_memory_bytes = 16 * MIB;
    node->inventory.reserved_host_cpu_slots = 1;
    node->inventory.reserved_host_memory_bytes = MIB;
    node->inventory.reserved_gateway_cpu_slots = 1;
    node->inventory.reserved_gateway_memory_bytes = MIB;
    node->inventory.allocatable_vcpu_slots = 6;
    node->inventory.allocatable_memory_bytes = 14 * MIB;
    memset(node->inventory.storage_capabilities_digest, 0x11,
           sizeof(node->inventory.storage_capabilities_digest));
    memset(node->inventory.accelerator_fault_capabilities_digest, 0x12,
           sizeof(node->inventory.accelerator_fault_capabilities_digest));
    memset(node->inventory.exclusive_resource_inventory_digest, 0x13,
           sizeof(node->inventory.exclusive_resource_inventory_digest));
    node->capability.physical_node_id = node->physical_node_id;
    node->capability.node_instance_id = node->node_instance_id;
    node->capability.profile_generation = 1;
    memset(node->capability.profile_digest, 0x21,
           sizeof(node->capability.profile_digest));
    node->desired_membership_state = WVM_MANIFEST_MEMBER_ACTIVE;
    node->observed_health_state = WVM_MEMBERSHIP_HEALTHY;
    node->membership_revision = 1;
    node->topology_revision = 1;
}

static void write_be32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t read_be32(const uint8_t bytes[4])
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static int write_full(int fd, const uint8_t *bytes, size_t byte_count)
{
    size_t offset = 0;

    while (offset < byte_count) {
        ssize_t written = write(fd, bytes + offset, byte_count - offset);

        if (written <= 0) {
            return -1;
        }
        offset += (size_t)written;
    }
    return 0;
}

static int read_full(int fd, uint8_t *bytes, size_t byte_count)
{
    size_t offset = 0;

    while (offset < byte_count) {
        ssize_t received = read(fd, bytes + offset, byte_count - offset);

        if (received <= 0) {
            return -1;
        }
        offset += (size_t)received;
    }
    return 0;
}

static int connect_service(const char *socket_path)
{
    struct sockaddr_un address;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        close(fd);
        return -1;
    }
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
    if (connect(fd, (const struct sockaddr *)&address,
                (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                            strlen(socket_path) + 1U)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int exchange_registration(
    const char *socket_path, const uint8_t *payload, size_t payload_bytes,
    const uint8_t operation_id[WVM_IDENTITY_ID_BYTES],
    struct wvm_membership_control_result *result_out)
{
    struct wvm_envelope request;
    struct wvm_envelope response;
    uint8_t prefix[4];
    uint8_t response_prefix[4];
    uint8_t frame[WVM_CONTROL_TRANSPORT_DEFAULT_MAX_FRAME_BYTES];
    uint8_t response_frame[WVM_ENVELOPE_HEADER_BYTES +
                           WVM_MEMBERSHIP_CONTROL_RESULT_BYTES];
    size_t frame_bytes = 0;
    uint32_t response_bytes;
    char error[256] = {0};
    int fd;
    int status = -1;

    fd = connect_service(socket_path);
    if (fd < 0) {
        return -1;
    }
    memset(&request, 0, sizeof(request));
    request.message_type = WVM_ENVELOPE_MSG_REGISTER_MEMBER;
    request.origin_physical_node_id = 17;
    request.origin_runtime_instance_id = 101;
    memcpy(request.operation_id, operation_id, sizeof(request.operation_id));
    request.delivery_attempt_id = 1;
    request.payload = payload;
    request.payload_bytes = payload_bytes;
    wvm_envelope_semantic_digest(payload, payload_bytes,
                                 request.semantic_payload_digest);
    if (wvm_envelope_encode(&request, WVM_ENVELOPE_TRANSPORT_LOCAL, frame,
                            sizeof(frame), &frame_bytes, error,
                            sizeof(error)) != 0 ||
        frame_bytes > UINT32_MAX) {
        goto out;
    }
    write_be32(prefix, (uint32_t)frame_bytes);
    if (write_full(fd, prefix, sizeof(prefix)) != 0 ||
        write_full(fd, frame, frame_bytes) != 0 ||
        read_full(fd, response_prefix, sizeof(response_prefix)) != 0) {
        goto out;
    }
    response_bytes = read_be32(response_prefix);
    if (response_bytes < WVM_ENVELOPE_HEADER_BYTES ||
        response_bytes > sizeof(response_frame) ||
        read_full(fd, response_frame, response_bytes) != 0 ||
        wvm_envelope_decode(response_frame, response_bytes,
                            WVM_ENVELOPE_TRANSPORT_LOCAL, &response, error,
                            sizeof(error)) != 0 ||
        response.message_type != WVM_ENVELOPE_MSG_CTRL_RESULT ||
        response.payload_bytes != WVM_MEMBERSHIP_CONTROL_RESULT_BYTES ||
        wvm_membership_control_result_decode(response.payload, result_out) !=
            0 ||
        memcmp(result_out->in_reply_to_operation_id, operation_id,
               sizeof(result_out->in_reply_to_operation_id)) != 0) {
        goto out;
    }
    status = 0;
out:
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return status;
}

int main(void)
{
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char membership_path[128];
    char control_path[128];
    struct wvm_control_plane plane;
    struct wvm_control_plane_entry entries[2];
    struct wvm_membership_controller_member_entry members[2];
    struct wvm_membership_controller_route_entry routes[2];
    struct wvm_membership_dependency dependencies[2];
    struct wvm_membership_control_operation operations[4];
    struct wvm_control_plane_membership_config membership_config;
    struct wvm_control_service_config service_config;
    struct wvm_control_service service;
    struct authorization_context authorization;
    struct wvm_node_record node;
    struct wvm_membership_control_result first_result;
    struct wvm_membership_control_result replay_result;
    uint8_t node_bytes[8192];
    uint8_t operation_id[WVM_IDENTITY_ID_BYTES] = {0};
    size_t node_byte_count = 0;
    char error[256] = {0};
    struct stat socket_stat;
    int result = 1;

    snprintf(socket_path, sizeof(socket_path), "/tmp/wavevm-service-%ld.sock",
             (long)getpid());
    snprintf(membership_path, sizeof(membership_path),
             "/tmp/wavevm-service-membership-%ld", (long)getpid());
    snprintf(control_path, sizeof(control_path),
             "/tmp/wavevm-service-control-%ld", (long)getpid());
    unlink(socket_path);
    unlink(membership_path);
    unlink(control_path);
    memset(&authorization, 0, sizeof(authorization));
    authorization.actor.role_type = WVM_MANIFEST_ROLE_NODE_RUNTIME;
    authorization.actor.role_id = 17;
    authorization.actor.instance_id = 101;
    fill_node(&node);
    if (wvm_node_record_encode(&node, node_bytes, sizeof(node_bytes),
                               &node_byte_count, error, sizeof(error)) != 0) {
        goto out;
    }
    memset(&membership_config, 0, sizeof(membership_config));
    membership_config.members = members;
    membership_config.member_capacity = 2;
    membership_config.routes = routes;
    membership_config.route_capacity = 2;
    membership_config.dependencies = dependencies;
    membership_config.dependency_capacity = 2;
    membership_config.operations = operations;
    membership_config.operation_capacity = 4;
    membership_config.membership_journal_path = membership_path;
    membership_config.control_journal_path = control_path;
    membership_config.authorize = authorize_self;
    membership_config.authorize_context = &authorization;
    wvm_control_plane_init(&plane, entries, 2);
    if (wvm_control_plane_configure_membership(&plane, &membership_config,
                                               error, sizeof(error)) != 0 ||
        wvm_control_plane_open_membership(&plane, error, sizeof(error)) != 0) {
        goto close_plane;
    }
    memset(&service_config, 0, sizeof(service_config));
    service_config.plane = &plane;
    service_config.socket_path = socket_path;
    service_config.socket_mode = S_IRUSR | S_IWUSR;
    service_config.listen_backlog = 4;
    service_config.local_physical_node_id = 900;
    service_config.local_runtime_instance_id = 901;
    service_config.authenticate = authenticate_actor;
    service_config.authenticate_opaque = &authorization;
    if (wvm_control_service_init(&service, &service_config, error,
                                 sizeof(error)) != 0 ||
        wvm_control_service_start(&service, error, sizeof(error)) != 0 ||
        expect(stat(socket_path, &socket_stat) == 0,
               "start protected control-plane listener") != 0) {
        goto destroy_service;
    }
    operation_id[WVM_IDENTITY_ID_BYTES - 1] = 1;
    if (expect(exchange_registration(socket_path, node_bytes, node_byte_count,
                                     operation_id, &first_result) == 0,
               "apply membership request through control service") != 0 ||
        expect(first_result.status_code == WVM_MEMBERSHIP_CONTROL_SUCCESS &&
                   first_result.recorded_state == WVM_MANIFEST_MEMBER_PENDING,
               "return durable typed result") != 0 ||
        expect(wvm_membership_controller_find(&plane.membership_controller,
                                              &authorization.actor) != NULL,
               "update authoritative membership state") != 0 ||
        expect(authorization.transport_calls == 1 &&
                   authorization.membership_calls == 1,
               "authenticate stream and authorize first operation") != 0 ||
        expect(wvm_control_service_stop(&service, error, sizeof(error)) == 0,
               "stop only the listener") != 0 ||
        expect(stat(socket_path, &socket_stat) != 0 && errno == ENOENT,
               "remove listener while retaining the durable plane") != 0 ||
        expect(plane.membership_open,
               "listener shutdown keeps membership authority open") != 0 ||
        expect(wvm_control_service_start(&service, error, sizeof(error)) == 0,
               "restart listener against same open plane") != 0 ||
        expect(exchange_registration(socket_path, node_bytes, node_byte_count,
                                     operation_id, &replay_result) == 0,
               "replay membership request through restarted listener") != 0 ||
        expect(memcmp(&first_result, &replay_result, sizeof(first_result)) ==
                   0,
               "replay exact durable control result") != 0 ||
        expect(authorization.transport_calls == 2 &&
                   authorization.membership_calls == 1,
               "replay reauthenticates transport without mutating authority") !=
                   0) {
        goto destroy_service;
    }
    result = 0;

destroy_service:
    wvm_control_service_destroy(&service);
close_plane:
    wvm_control_plane_close(&plane);
out:
    unlink(socket_path);
    unlink(membership_path);
    unlink(control_path);
    if (result != 0 && error[0] != '\0') {
        fprintf(stderr, "control-service test: %s\n", error);
    }
    return result;
}
