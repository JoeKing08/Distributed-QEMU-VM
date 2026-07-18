CC = gcc
CFLAGS = -Wall -O2 -msse4.2 -I../common_include -pthread
TARGET = wavevm_node_master
SRCS = logic_core.c user_backend.c main_wrapper.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
