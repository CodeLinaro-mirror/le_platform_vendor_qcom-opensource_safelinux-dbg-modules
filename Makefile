INSPATH ?= $(KDIR)
TOP_DIR := $(PWD)
export TOP_DIR

obj-m := minidump/
obj-m += kaslr_store/
ifndef CONFIG_QCOM_MEMORY_DUMP_V2
obj-m += memory_dump_v2/
endif
obj-m += nhlos_log/

all: clean modules

KERNEL_VERSION ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VERSION)/build

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

%:
	$(MAKE) -C $(KDIR) M=$(PWD) $@
