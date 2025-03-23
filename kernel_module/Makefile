obj-m += simp.o
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

reload:
	- sudo rmmod simp
	sudo insmod simp.ko
