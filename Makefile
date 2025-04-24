INSPATH ?= $(KDIR)
TOP_DIR := $(PWD)
export TOP_DIR

obj-m := minidump/
obj-m += kaslr_store/

# Build out of tree memory dump driver when kernel in-tree memory
# dump driver disabled. OOT memory dump driver conflict with kernel
# in-tree memory dump driver, we can only choose one.
ifndef CONFIG_QCOM_MEMORY_DUMP_V2
obj-m += memory_dump_v2/
obj-m += memory_dump_v21/
endif

obj-m += xbl_log/

all: clean modules

KERNEL_VERSION ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VERSION)/build

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

%:
	$(MAKE) -C $(KDIR) M=$(PWD) $@
