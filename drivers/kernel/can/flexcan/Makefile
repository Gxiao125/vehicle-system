# drivers/kernel/can/flexcan/Makefile

obj-m += flexcan_filter.o
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

