SYS = $(shell uname)

OFLAGS = -O3 -fomit-frame-pointer -DNDEBUG
CFLAGS = -Wall -Wno-comment -g $(OFLAGS)
CC32 = $(CC) -m32

# this is a KLUDGE that seems to be needed on my machine:
# 3.11-2-amd64 #1 SMP Debian 3.11.10-1 (2013-12-04) x86_64 GNU/Linux
CC32 += -I /usr/include/i486-linux-gnu/

ifeq ($(findstring MINGW32,$(SYS)),MINGW32)
LIBS = -lm -lffi libw32dl.a
TIME =
else
LIBS = -lm -lffi -ldl
TIME = time
endif

ifeq ($(findstring Darwin,$(SYS)),Darwin)
SO = dylib
SOCFLAGS = -dynamiclib -Wl,-headerpad_max_install_names,-undefined,dynamic_lookup,-flat_namespace
else
SO = so
SOCFLAGS = -shared -msse -msse2
endif

all : eval eval32

prepare :
	mkdir -p build

eval : prepare eval.c gc.c gc.h buffer.c chartab.h wcs.c osdefs.k
	$(CC) -g $(CFLAGS) -o build/eval eval.c $(LIBS)
	ln -sf bootstrap/c/build/eval ../../

eval32 : eval.c gc.c gc.h buffer.c chartab.h wcs.c
	$(CC32) -g $(CFLAGS) -o build/eval32 eval.c $(LIBS)
	ln -sf bootstrap/c/build/eval32 ../../

gceval : prepare eval.c libgc.c buffer.c chartab.h wcs.c osdefs.k
	$(CC) -g $(CFLAGS) -DLIB_GC=1 -o build/gceval eval.c $(LIBS) -lgc
	ln -sf bootstrap/c/build/gceval ../../

debug : .force
	$(MAKE) OFLAGS="-O0"

debuggc : .force
	$(MAKE) CFLAGS="$(CFLAGS) -DDEBUGGC=1"

profile : .force
	$(MAKE) clean eval CFLAGS="$(CFLAGS) -O3 -fno-inline-functions -DNDEBUG"
#	shark -q -1 -i ./eval emit.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l > test.s
	shark -q -1 -i ./eval repl.l test-pepsi.l

osdefs.k : mkosdefs
	./build/mkosdefs > build/$@
	ln -sf bootstrap/c/build/osdefs.k ../../

mkosdefs : mkosdefs.c
	$(CC) -o build/$@ $<

clean : .force
	rm -f *~ *.o eval eval32 gceval mkosdefs osdefs.k *.exe *.$(SO)
	rm -rf build

.force :
