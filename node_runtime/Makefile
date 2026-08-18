CC ?= gcc
CFLAGS ?= -Wall -O2 -msse4.2 -std=gnu11 -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?= -I../common_include -pthread
LDFLAGS ?= -pthread

TARGET = ../wavevm_node_runtime

MASTER_OBJS = master_logic.o master_user.o master_main.o
SLAVE_OBJS = slave_hybrid.o slave_vfio.o
COMMON_OBJS = resources.o sha256.o canonical.o identity.o runtime_names.o manifest.o lifecycle.o runtime_gate.o runtime_dispatch.o executor_abi.o envelope.o memory.o vcpu_handoff.o vcpu_handoff_cache.o x86_context.o local_memory.o route_runtime.o executor_bridge.o vcpu_coordinator.o vcpu_service.o memory_service.o ingress.o directory_store.o kvm_page_cache.o control.o route_delivery.o

all: $(TARGET)

$(TARGET): main.o $(MASTER_OBJS) $(SLAVE_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

master_logic.o: ../master_core/logic_core.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

master_user.o: ../master_core/user_backend.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

master_main.o: ../master_core/main_wrapper.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -Dmain=wavevm_master_main -c $< -o $@

slave_hybrid.o: ../slave_daemon/slave_hybrid.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -Dmain=wavevm_slave_main -c $< -o $@

slave_vfio.o: ../slave_daemon/slave_vfio.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

resources.o: ../common_include/wavevm_resources.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

sha256.o: ../common_include/wavevm_sha256.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

canonical.o: ../common_include/wavevm_canonical.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

identity.o: ../common_include/wavevm_identity.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

runtime_names.o: ../common_include/wavevm_runtime_names.c ../common_include/wavevm_lifecycle.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

manifest.o: ../common_include/wavevm_manifest.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

lifecycle.o: ../common_include/wavevm_lifecycle.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

runtime_gate.o: ../common_include/wavevm_runtime_gate.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

runtime_dispatch.o: ../common_include/wavevm_runtime_dispatch.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

executor_abi.o: ../common_include/wavevm_executor_abi.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

envelope.o: ../common_include/wavevm_envelope.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

memory.o: ../common_include/wavevm_memory.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

vcpu_handoff.o: ../common_include/wavevm_vcpu_handoff.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

vcpu_handoff_cache.o: ../common_include/wavevm_vcpu_handoff_cache.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

x86_context.o: ../common_include/wavevm_x86_context.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

local_memory.o: ../common_include/wavevm_local_memory.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

route_runtime.o: ../common_include/wavevm_route_runtime.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

executor_bridge.o: executor_bridge.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

vcpu_coordinator.o: vcpu_coordinator.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

vcpu_service.o: vcpu_service.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

memory_service.o: memory_service.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

ingress.o: ingress.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

directory_store.o: directory_store.c directory_store.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

kvm_page_cache.o: kvm_page_cache.c kvm_page_cache.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

control.o: ../common_include/wavevm_control.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

route_delivery.o: ../common_include/wavevm_route_delivery.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) main.o $(MASTER_OBJS) $(SLAVE_OBJS) $(COMMON_OBJS)

.PHONY: all clean
