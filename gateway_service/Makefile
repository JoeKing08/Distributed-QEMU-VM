CC = gcc
# 必须包含 -pthread 因为 aggregator.c 用到了多线程
CFLAGS = -Wall -O2 -I../common_include -pthread
TARGET = wavevm_gateway
SRCS = aggregator.c main.c \
	../common_include/wavevm_sha256.c \
	../common_include/wavevm_canonical.c \
	../common_include/wavevm_identity.c \
	../common_include/wavevm_manifest.c \
	../common_include/wavevm_lifecycle.c \
	../common_include/wavevm_control.c \
	../common_include/wavevm_runtime_gate.c \
	../common_include/wavevm_envelope_v1.c \
	../common_include/wavevm_route_runtime.c \
	../common_include/wavevm_route_control.c \
	../common_include/wavevm_route_delivery.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
