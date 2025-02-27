ccflags-y += -I$(TOP_DIR)/minidump
ccflags-y += -I$(TOP_DIR)/memory_dump_v21
obj-m += memory_dump_v21.o

all: clean modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

%:
	$(MAKE) -C $(KDIR) INSTALL_MOD_PATH=$(KDIR) M=$(PWD) $@
