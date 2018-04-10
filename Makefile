NOW = $(shell date '+%Y%m%d.%H%M')
SYS = $(shell uname)

OFLAGS = -O3 -fomit-frame-pointer -DNDEBUG
CFLAGS = -Wall -Wno-comment -g $(OFLAGS)
CC32 = $(CC) -m32

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

.SUFFIXES:
.SECONDARY:

all : eval

run : all
	rlwrap ./eval

status : .force
	@echo "SYS is $(SYS)"

eval :
	cd bootstrap/c ; make

stage-% : build/stage-%/eval
	@true >/dev/null

build/stage-%/eval :
	echo Building stage-$*
	rm -rf ./build/stage-$*
	mkdir -p build
	git clone --branch stage-$* . ./build/stage-$*
	cd ./build/stage-$* ; make

check-maru : eval
# FIXME: maru-nfibs.k is not checked into the repo?
#	./eval ir-gen-c.k maru.k maru-nfibs.k
	./eval ir-gen-c.k maru.k maru-gc.k
	./eval ir-gen-c.k maru.k maru-test.k

check-marux : eval
# FIXME: maru-nfibs.k is not checked into the repo?
#	./eval ir-gen-x86.k maru.k maru-nfibs.k
	./eval ir-gen-x86.k maru.k maru-gc.k
	./eval ir-gen-x86.k maru.k maru-test.k

test-maru : eval
	mkdir -p build
	./eval ir-gen-c.k maru.k maru-test.k	> ./build/test.c && cc -fno-builtin -g -o ./build/test ./build/test.c -ldl && ./build/test 32
# FIXME: maru-nfibs.k is not checkedto in the repo?
#	./eval ir-gen-c.k maru.k maru-nfibs.k	> ./build/test.c && cc -fno-builtin -g -o ./build/test ./build/test.c -ldl && ./build/test 32
	./eval ir-gen-c.k maru.k maru-gc.k	> ./build/test.c && cc -fno-builtin -g -o ./build/test ./build/test.c -ldl && ./build/test 32

# FIXME test2.c and maru-test2.k are not checked into the repo?
test2-maru : stage-1
	cd ./build/stage-1/ ; ./eval2 ../../ir-gen-x86.k ../../maru.k ../../maru-test2.k > ../../build/test.s
	cc -fno-builtin -g -o ../../build/test2 ../../build/test2.c ../../build/test.s
	./build/test2 15

# FIXME test2.c and maru-test3.k are not checked into the repo?
test3-maru : eval2
	./eval2 ir-gen-x86.k maru.k maru-test3.k > test.s && cc -m32 -fno-builtin -g -o test3 test.s && ./test3

maru-check : eval .force
	./eval -g ir-gen-x86.k maru.k maru-check.k > ./build/maru-check.s
	cc -m32 -o ./build/maru-check ./build/maru-check.s
	./build/maru-check

maru-check-c : eval .force
	./eval ir-gen-c.k maru.k maru-check.k > ./build/maru-check.c
	cc -o ./build/maru-check ./build/maru-check.c -ldl
	./build/maru-check

maru-label : eval .force
	./eval ir-gen-x86.k maru.k maru-label.k > ./build/maru-label.s
	cc -m32 -o ./build/maru-label ./build/maru-label.s
	./build/maru-label

maru-label-c : eval .force
	./eval ir-gen-c.k maru.k maru-label.k > ./build/maru-label.c
	cc -o ./build/maru-label ./build/maru-label.c -ldl
	./build/maru-label

NFIBS=40

maru-bench : eval .force
##	cc -O2 -fomit-frame-pointer -mdynamic-no-pic -o nfibs nfibs.c
	cc -O2 -fomit-frame-pointer -o nfibs nfibs.c
# FIXME: maru-nfibs.k is not checked in the repo?
#	./eval ir-gen-x86.k maru.k maru-nfibs.k > maru-nfibs.s
##	cc -O2 -fomit-frame-pointer -mdynamic-no-pic -o maru-nfibs maru-nfibs.s
	cc -O2 -fomit-frame-pointer -o maru-nfibs maru-nfibs.s
	time ./nfibs $(NFIBS)
	time ./nfibs $(NFIBS)
	time ./maru-nfibs $(NFIBS)
	time ./maru-nfibs $(NFIBS)

debug : .force
	$(MAKE) OFLAGS="-O0"

debuggc : .force
	$(MAKE) CFLAGS="$(CFLAGS) -DDEBUGGC=1"

profile : .force
	$(MAKE) clean eval CFLAGS="$(CFLAGS) -O3 -fno-inline-functions -DNDEBUG"
#	shark -q -1 -i ./eval emit.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l eval.l > test.s
	shark -q -1 -i ./eval repl.l test-pepsi.l

cg : eval .force
	./eval codegen5.l | tee test.s
	as test.s
	ld  --build-id --eh-frame-hdr -m elf_i386 --hash-style=both -dynamic-linker /lib/ld-linux.so.2 -o test /usr/lib/gcc/i486-linux-gnu/4.4.5/../../../../lib/crt1.o /usr/lib/gcc/i486-linux-gnu/4.4.5/../../../../lib/crti.o /usr/lib/gcc/i486-linux-gnu/4.4.5/crtbegin.o -L/usr/lib/gcc/i486-linux-gnu/4.4.5 -L/usr/lib/gcc/i486-linux-gnu/4.4.5 -L/usr/lib/gcc/i486-linux-gnu/4.4.5/../../../../lib -L/lib/../lib -L/usr/lib/../lib -L/usr/lib/gcc/i486-linux-gnu/4.4.5/../../.. a.out -lgcc --as-needed -lgcc_s --no-as-needed -lc -lgcc --as-needed -lgcc_s --no-as-needed /usr/lib/gcc/i486-linux-gnu/4.4.5/crtend.o /usr/lib/gcc/i486-linux-gnu/4.4.5/../../../../lib/crtn.o
	./test

bootstrapped-eval : build/bootstrapped-eval
	@true >/dev/null

build/bootstrapped-eval : emit.l eval.l stage-1
# we could do it this way also, but in the current setup (require "osdefs.k") would not find the stage-1 osdefs.k, so we need to cd there instead
#	$(TIME) ./build/stage-1/eval1 -b ./build/stage-1/boot.l -O emit.l eval.l > ./build/test.s
	cd ./build/stage-1/ ; $(TIME) ./eval1 -O ../../emit.l ../../eval.l > ../bootstrapped-eval.s
	$(CC32) -c -o ./build/bootstrapped-eval.o ./build/bootstrapped-eval.s
	size ./build/bootstrapped-eval.o
	$(CC32) -o ./build/bootstrapped-eval ./build/bootstrapped-eval.o

time : .force stage-1
	cd ./build/stage-1/ ; $(TIME) ./eval1 -O ../../emit.l ../../eval.l ../../eval.l ../../eval.l ../../eval.l ../../eval.l > /dev/null

test2 : stage-1 bootstrapped-eval .force
	cd ./build/stage-1/ ; $(TIME) ../../build/bootstrapped-eval -O boot.l ../../emit.l ../../eval.l > ../../build/re-bootstrapped-eval.s
	diff ./build/bootstrapped-eval.s ./build/re-bootstrapped-eval.s

time2 : .force stage-1
	ln -sf ./stage-1/osdefs.k ./build/osdefs.k
	cd ./build/ ; $(TIME) ./bootstrapped-eval ./stage-1/boot.l ../emit.l ../eval.l ../eval.l ../eval.l ../eval.l ../eval.l > /dev/null

# FIXME: test-eval.l is not checked into the repo?
test-eval : bootstrapped-eval .force
	$(TIME) ./build/bootstrapped-eval test-eval.l

# FIXME: boot-emit.l is not checked into the repo?
test-boot : bootstrapped-eval .force
	$(TIME) ./build/bootstrapped-eval boot-emit.l

# FIXME: ./emit.l ?!
test-emit : eval .force
	./emit.l test-emit.l | tee test.s && $(CC32) -c -o test.o test.s && size test.o && $(CC32) -o test test.o && ./test

build/peg.l : eval parser.l peg-compile.l peg-boot.l peg.g
	./eval parser.l peg-compile.l peg-boot.l > ./build/peg.l

test-repl : eval build/peg.l .force
	./eval repl.l test-repl.l

# FIXME this seems to get into an endless loop, or just runs longer than half an hour?
test-peg : eval build/peg.l .force
	$(TIME) ./eval parser.l ./build/peg.l test-peg.l > ./build/peg.n
	$(TIME) ./eval parser.l ./build/peg.n test-peg.l > ./build/peg.m
	diff ./build/peg.n ./build/peg.m

test-compile-grammar :
	./eval compile-grammar.l test-dc.g > ./build/test-dc.g.l
	./eval compile-dc.l test.dc

# FIXME this dies both all three eval.c versions
test-compile-irgol : eval build/irgol.g.l .force
#	cd ./build/stage-1/ ; ln -sf ../../build/irgol.g.l . ; ./eval1 ../../compile-irgol.l ../../test.irgol > ../../build/test.c
	./eval compile-irgol.l test.irgol > ./build/test.c
	$(CC32) -fno-builtin -g -o ./build/test ./build/test.c
	@echo
	./build/test

build/irgol.g.l : eval compile-tpeg.l build/tpeg.l irgol.g
	./eval compile-tpeg.l irgol.g > ./build/irgol.g.l

# FIXME: irgol.k is not checked into the repo?
test-irgol : eval .force
	./eval irgol.k | tee ./build/test.c
	$(CC32) -fno-builtin -g -o ./build/test ./build/test.c
	@echo
	./build/test

test-compile-irl : stage-1 build/irl.g.l .force
	cd ./build/stage-1/ ; ln -sf ../../build/irl.g.l . ; ./eval1 ../../compile-irl.l ../../test.irl > ../../build/test.c
#	./eval compile-irl.l test.irl > ./build/test.c
	$(CC) -fno-builtin -g -o ./build/test ./build/test.c
	@echo
	./build/test

build/irl.g.l : eval compile-tpeg.l build/tpeg.l irl.g
	./eval compile-tpeg.l irl.g > ./build/irl.g.l

# FIXME this fails with: string-length: non-String argument: ()
test-ir : eval .force
	./eval test-ir.k > test.c
	$(CC32) -fno-builtin -g -o test test.c
	@echo
	./test

build/tpeg.l : eval tpeg.g compile-peg.l
	$(TIME) ./eval compile-peg.l tpeg.g > ./build/tpeg.l

test-tpeg.l : build/tpeg.l stage-1 compile-tpeg.l
#	-test -f tpeg.l && cp tpeg.l tpeg.l.$(NOW)
#	mv tpeg.l.new tpeg.l
	$(TIME) ./eval compile-tpeg.l tpeg.g > ./build/tpeg.ll
	sort ./build/tpeg.l > ./build/tpeg.l.sorted
	sort ./build/tpeg.ll > ./build/tpeg.ll.sorted
	diff ./build/tpeg.l.sorted ./build/tpeg.ll.sorted

test-mach-o : eval .force
	./eval32 test-mach-o.l
	@echo
	size a.out
	chmod +x a.out
	@echo
	./a.out

test-elf : eval .force
	./eval32 test-elf.l
	@echo
	size a.out
	chmod +x a.out
	@echo
	./a.out

test-assembler : eval .force
	./eval32 assembler.k

test-recursion2 : eval
	./eval compile-grammar.l test-recursion2.g > test-recursion2.g.l
	./eval compile-recursion2.l test-recursion2.txt

test-main : eval32 .force
	$(TIME) ./eval32 test-main.k
	chmod +x test-main
	$(TIME) ./test-main hello world

test-main2 : eval32 .force
	$(TIME) ./eval32 test-pegen.k save.k test-pegen
	chmod +x test-pegen
	$(TIME) ./test-pegen

build/cpp.g.l : cpp.g tpeg.l
	./eval compile-tpeg.l $< > $@

test-cpp : eval build/cpp.g.l .force
	./eval compile-cpp.l cpp-small-test.c

build/osdefs.g.l : osdefs.g build/tpeg.l
	./eval compile-tpeg.l $< > $@

%.osdefs.k : %.osdefs build/osdefs.g.l
	./eval compile-osdefs.l $< > $<.c
	cc -o $<.exe $<.c
	./$<.exe > $@.new
	mv $@.new $@
	rm -f $<.exe $<.c

OSDEFS = $(wildcard *.osdefs) $(wildcard net/*.osdefs)
OSKEFS = $(OSDEFS:.osdefs=.osdefs.k)

osdefs : build/osdefs.g.l $(OSKEFS) .force

profile-peg : .force
	$(MAKE) clean eval CFLAGS="-O3 -fno-inline-functions -g -DNDEBUG"
	shark -q -1 -i ./eval parser.l peg.n test-peg.l > peg.m

NILE = ../nile
GEZIRA = ../gezira

libs : libnile.$(SO) libgezira.$(SO)

libnile.$(SO) : .force
	$(CC) -I$(NILE)/runtimes/c -O3 -ffast-math -fPIC -fno-common $(SOCFLAGS) -o $@ $(NILE)/runtimes/c/nile.c

libgezira.$(SO) : .force
	$(CC) -I$(NILE)/runtimes/c -O3 -ffast-math -fPIC -fno-common $(SOCFLAGS) -o $@ $(GEZIRA)/c/gezira.c $(GEZIRA)/c/gezira-image.c

stats : .force
	cat boot.l emit.l | sed 's/.*debug.*//;s/;.*//' | sort -u | wc -l
	cat eval.l | sed 's/.*debug.*//;s/;.*//' | sort -u | wc -l
	cat boot.l emit.l eval.l | sed 's/.*debug.*//;s/;.*//' | sort -u | wc -l

clean : .force
	cd bootstrap/c ; make clean
	rm -rf build
	rm -f *~ *.o main eval eval32 gceval *.s mkosdefs *.exe *.$(SO)
	rm -f test-main test-pegen
	rm -rf *.dSYM *.mshark

#----------------------------------------------------------------

FILES = Makefile \
	wcs.c buffer.c chartab.h eval.c gc.c gc.h \
	boot.l emit.l eval.l test-emit.l \
	parser.l peg-compile.l peg-compile-2.l peg-boot.l peg.l test-peg.l test-repl.l \
	repl.l repl-2.l mpl.l sim.l \
	peg.g

DIST = maru-$(NOW)
DEST = ckpt/$(DIST)

dist : .force
	mkdir -p $(DEST)
	cp -p $(FILES) $(DEST)/.
	$(SHELL) -ec "cd ckpt; tar cvfz $(DIST).tar.gz $(DIST)"

.force :
