# Build kernel module (message_slot.ko)
obj-m += message_slot.o

# Path to the Linux kernel build directory
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: modules user

# Kernel module build using kernel's build system
modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules




# User-space programs
user: message_sender message_reader

CFLAGS = -O3 -Wall -std=c11

message_sender: message_sender.c message_slot.h
	gcc $(CFLAGS) $< -o $@

message_reader: message_reader.c message_slot.h
	gcc $(CFLAGS) $< -o $@

# Clean both kernel module and user binaries
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f message_sender message_reader
