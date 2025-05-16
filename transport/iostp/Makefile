


KERNELDIR := /home/xiao/project/vehicle-system/kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
CURRENT_PATH := $(shell pwd)

ccflags-y += -I"$(CURRENT_PATH)/../include"


obj-m += transport.o

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) modules  

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURRENT_PATH) clean    

