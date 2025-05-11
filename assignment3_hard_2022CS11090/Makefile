CONFIG_MODULE_SIG=n
CONFIG_MODULE_SIG_ALL=n
obj-m := smartbdev.o 
# make -C /lib/modules/$(uname -r)/build M=$PWD

all:
	# $(shell uname -r)
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

#  sudo insmod ./smartbdev.ko 
# modinfo smartbdev.ko | grep vermagic