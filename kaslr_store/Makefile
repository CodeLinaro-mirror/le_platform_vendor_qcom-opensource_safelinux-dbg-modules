M=$(PWD)

obj-m += kaslr_store.o

INSPATH ?= $(KDIR)

all: clean modules

clean:
	$(MAKE) -C $(KDIR) M=$(M) clean

%:
	$(MAKE) -C $(KDIR) INSTALL_MOD_PATH=$(INSPATH) M=$(M) $@
