#
# Module Makefile for DM510
#

# Change this if you keep your files elsewhere
ROOT = ..
KERNELDIR = ${ROOT}/linux-4.15
PWD = $(shell pwd)


obj-m += dm510_dev.o

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) LDDINC=$(KERNELDIR)/include ARCH=um modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions
	rm test1
	rm test2
	rm test3

tests:
	gcc moduletest.c -o test1
	gcc moduletest_ioctl.c -o test2
	gcc moduletest_open.c -o test3
