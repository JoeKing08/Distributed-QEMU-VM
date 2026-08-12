CC = gcc
CFLAGS = -Wall -O2 -I../common_include
TARGET = wvm_ctl

all: $(TARGET)

$(TARGET): main.c ../common_include/wavevm_resources.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
