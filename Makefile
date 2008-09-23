# Kernel module
obj-m += exmap.o
#EXTRA_CFLAGS=-g -O0

# User land (e.g. tests and supporting code)
TS_OBJ = mapper.o
EXES += mapper
OBJS += $(TS_OBJ)

SA_OBJ = sharedarray.o
SHLIBS += libsharedarray.so
OBJS += $(SA_OBJ)

SM_OBJ = sharedarraymain.o
EXES += sharedarray
OBJS += $(SM_OBJ)

AL_OBJ = allocer.o
EXES += allocer
OBJS += $(AL_OBJ)

ML_OBJ = memload.o
EXES += memload
OBJS += $(ML_OBJ)

MI_OBJ = mapit.o
EXES += mapit
OBJS += $(MI_OBJ)

EXTRA_DEL_FILES += *~

TESTS +=  test-range.pl test-elf.pl test-exmap.pl

all: exmap.ko $(EXES) $(SHLIBS)

test: $(TESTS) $(EXES) $(SHLIBS)
	perl runtests.pl $(TESTS)

exmap.ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mapper: $(TS_OBJ)
	$(CC) -o mapper $(TS_OBJ)

allocer: $(AL_OBJ)
	$(CC) -o allocer $(AL_OBJ)

memload: $(ML_OBJ)
	$(CC) -o memload $(ML_OBJ)

mapit: $(MI_OBJ)
	$(CC) -o mapit $(MI_OBJ)

libsharedarray.so: $(SA_OBJ)
	$(CC) -shared -o libsharedarray.so $(SA_OBJ)

sharedarray: $(SM_OBJ) libsharedarray.so
	$(CC) -o sharedarray $(SM_OBJ) -L. -lsharedarray

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f $(OBJS) $(EXES) $(SHLIBS) $(EXTRA_DEL_FILES)
