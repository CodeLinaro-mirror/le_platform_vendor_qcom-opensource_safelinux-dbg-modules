M=$(PWD)

INSPATH ?= $(KDIR)

EXMOD_TOPDIR := $(M)
export EXMOD_TOPDIR

all: clean modules

clean:
	$(MAKE) -C $(KDIR) M=$(M) clean

%:
	$(MAKE) -C $(KDIR) INSTALL_MOD_PATH=$(INSPATH) M=$(M) $@
