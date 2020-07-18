obj-m:=main.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	
clean:
	rm -r *.mod *.symvers *.mod *.mod.c *.mod.o *.order