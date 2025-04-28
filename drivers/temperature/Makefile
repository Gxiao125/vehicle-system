KERNEL_DIR ?= ../../kernel/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

obj-m += can_temp_sensor.0

all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean



