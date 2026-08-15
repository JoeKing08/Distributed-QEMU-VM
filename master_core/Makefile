CC = gcc
CFLAGS = -Wall -O2 -msse4.2 -I../common_include -pthread
TARGET = wavevm_node_master
SRCS = logic_core.c user_backend.c main_wrapper.c \
	../common_include/wavevm_resources.c \
	../common_include/wavevm_sha256.c \
	../common_include/wavevm_canonical.c \
	../common_include/wavevm_identity.c \
	../common_include/wavevm_manifest.c \
	../common_include/wavevm_lifecycle.c \
	../common_include/wavevm_control.c \
	../common_include/wavevm_route_delivery.c \
	../common_include/wavevm_runtime_gate.c \
	../common_include/wavevm_runtime_dispatch.c \
	../common_include/wavevm_executor_abi.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
