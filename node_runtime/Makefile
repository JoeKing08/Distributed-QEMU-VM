CC ?= gcc
CFLAGS ?= -Wall -O2 -msse4.2 -std=gnu11 -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?= -I../common_include -pthread
LDFLAGS ?= -pthread

TARGET = ../wavevm_node_runtime

MASTER_OBJS = master_logic.o master_user.o master_main.o
SLAVE_OBJS = slave_hybrid.o slave_vfio.o
COMMON_OBJS = resources.o sha256.o canonical.o identity.o manifest.o lifecycle.o runtime_gate.o runtime_dispatch.o executor_abi.o executor_bridge.o control.o route_delivery.o

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

executor_bridge.o: executor_bridge.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

control.o: ../common_include/wavevm_control.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

route_delivery.o: ../common_include/wavevm_route_delivery.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) main.o $(MASTER_OBJS) $(SLAVE_OBJS) $(COMMON_OBJS)

.PHONY: all clean
