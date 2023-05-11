M=$(PWD)

obj-m += minidump.o
minidump-y   += msm_minidump.o minidump_log.o

INSPATH ?= $(KDIR)

all: clean modules

clean:
	$(MAKE) -C $(KDIR) M=$(M) clean

%:
	$(MAKE) -C $(KDIR) INSTALL_MOD_PATH=$(INSPATH) M=$(M) $@
