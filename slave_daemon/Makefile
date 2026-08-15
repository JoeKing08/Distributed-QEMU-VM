CC = gcc
CFLAGS = -Wall -O3 -msse4.2 -I../common_include -pthread 
TARGET = wavevm_node_slave
SRCS = slave_hybrid.c slave_vfio.c \
	../common_include/wavevm_sha256.c \
	../common_include/wavevm_canonical.c \
	../common_include/wavevm_identity.c \
	../common_include/wavevm_manifest.c \
	../common_include/wavevm_lifecycle.c \
	../common_include/wavevm_runtime_gate.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
