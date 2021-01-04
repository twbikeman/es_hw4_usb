CFILES := main.c sub.c
obj-m := hello.o
hello-objs := $(CFILES: .c=.o)
all:

clean:
