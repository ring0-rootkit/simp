obj-m += grtp_mod.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

reload:
	- sudo rmmod grtp_mod
	sudo insmod grtp_mod.ko
