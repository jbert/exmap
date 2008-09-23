# Kernel modules
obj-m += exmap.o
obj-m += swapout.o
# These flags are for the kernel module(s) only
#EXTRA_CFLAGS=-g -O0
DOC=exmap.html Exmap.html

all: kernel_modules userspace doc

doc: $(DOC)

kernel_modules:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

userspace:
	make -f Makefile.user

test:
	make -f Makefile.user test

# Tell make to recognise these suffices in the following implicit rule
.SUFFIXES: .html .pl .pm

# How to make html docs from perl files.
.pl.html:
	pod2html --infile $< --outfile $@

.pm.html:
	pod2html --infile $< --outfile $@


clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	make -f Makefile.user clean
	rm -f $(DOC) pod2htm*