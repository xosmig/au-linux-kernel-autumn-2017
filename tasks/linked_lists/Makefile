MOD_NAME := linked_lists

ifneq ($(KERNELRELEASE),)
	obj-m := $(MOD_NAME).o
	$(MOD_NAME)-objs := module.o stack.o
else
	KDIR := ../../linux/
	VM_PATH := ../../../vm/
all:
	$(MAKE) -C $(KDIR) M=$$PWD
	cp $(MOD_NAME).ko $(VM_PATH)/share/

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean
endif
