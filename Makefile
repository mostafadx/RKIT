obj-m += rkit.o

# Define variables for each object file
rkit-y := rkit_core.o rkit_client.o rkit_server.o rkit_common.o

# Specify the paths to the kernel headers
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Define build target
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
