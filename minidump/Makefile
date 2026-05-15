M=$(PWD)

CFLAGS_minidump_log.o += -include $(KDIR)/include/linux/sched/cputime.h \
			-include $(KDIR)/kernel/sched/sched.h \
			-include $(KDIR)/kernel/time/tick-internal.h
obj-m += minidump.o
minidump-y   += msm_minidump.o minidump_log.o minidump_memory.o

INSPATH ?= $(KDIR)

all: clean modules

clean:
	$(MAKE) -C $(KDIR) M=$(M) clean

%:
	$(MAKE) -C $(KDIR) INSTALL_MOD_PATH=$(INSPATH) M=$(M) $@
