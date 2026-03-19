INSPATH ?= $(KDIR)
TOP_DIR := $(M)
export TOP_DIR

obj-m := minidump/
obj-m += kaslr_store/

# Enable Memory dump driver v2.1 only for SA8797P based on the config
# CONFIG_QCOM_MEMORY_DUMP_V21. Currently, config CONFIG_QCOM_MEMORY_DUMP_V2
# is disabled for GEN4 and GEN5 auto targets. It is s now safe to enable
# OOT Memory dump driver v2 which won't conflict with In-Tree
# Memory dump driver.
ifdef CONFIG_QCOM_MEMORY_DUMP_V21
obj-m += memory_dump_v21/
else
obj-m += memory_dump_v2/
endif

obj-m += nhlos_log/

all: clean modules

KERNEL_VERSION ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_VERSION)/build

clean:
	$(MAKE) -C $(KDIR) M=$(M) clean

%:
	$(MAKE) -C $(KDIR) M=$(M) $@
