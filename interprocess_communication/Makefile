obj-m += message_slot.o

KDIR := /lib/modules/$(shell uname -r)/build

#all: modules user
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

user: message_sender message_reader

TARGET = message_slot.ko
SRC = message_slot.c

message_sender: message_sender.c message_slot.h
	gcc -O3 -Wall -std=c11 $< -o $@

message_reader: message_reader.c message_slot.h
	gcc -O3 -Wall -std=c11 $< -o $@

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f message_sender message_reader