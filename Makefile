CROSS ?= m68k-xelf-
CC68 = $(CROSS)gcc
HOSTCC ?= cc
CPPFLAGS += -Iinclude
CFLAGS68 = -m68000 -O2 -std=c99 -Wall -Wextra -Werror -fno-strict-aliasing
HOSTFLAGS = -O2 -std=c99 -Wall -Wextra -Werror -fno-strict-aliasing
OUT = build/memtest68.x
BOOTFLAGS = $(CFLAGS68) -ffreestanding -fno-builtin -fno-common -fno-stack-protector
BOOTOBJ = build/boot/start.o build/boot/main.o build/boot/format.o build/boot/low_probe.o build/boot/runtime.o build/boot/memtest.o build/boot/memtest68k.o
LIBGCC = $(shell $(CC68) -m68000 -print-libgcc-file-name)
LIB68 = $(dir $(shell $(CC68) -print-file-name=libx68kiocs.a))

.PHONY: all fd hds test clean check
all: $(OUT) build/memtest68-fd.xdf build/memtest68-hd.hds

build:
	mkdir -p build

build/boot:
	mkdir -p build/boot

build/boot/%.o: boot/%.c include/memtest.h boot/format.h boot/probe.h | build/boot
	$(CC68) $(CPPFLAGS) $(BOOTFLAGS) -c -o $@ $<

build/boot/%.o: boot/%.S boot/probe.h | build/boot
	$(CC68) -m68000 -c -o $@ $<

build/boot/memtest68k.o: src/memtest68k.S | build/boot
	$(CC68) -m68000 -c -o $@ $<

build/boot/memtest.o: src/memtest.c src/memtest68k.h include/memtest.h | build/boot
	$(CC68) $(CPPFLAGS) $(BOOTFLAGS) -c -o $@ $<

build/boot/ipl.elf: build/boot/ipl.o
	$(CROSS)ld -Ttext=0x2000 -e _ipl -o $@ $<

build/boot/ipl.bin: build/boot/ipl.elf
	$(CROSS)objcopy -O binary $< $@

build/boot/standalone.elf: $(BOOTOBJ) boot/standalone.ld
	$(CROSS)ld -T boot/standalone.ld -Map=build/boot/standalone.map -o $@ $(BOOTOBJ) -L$(LIB68) -lx68kiocs $(LIBGCC)

build/boot/standalone.bin: build/boot/standalone.elf
	$(CROSS)objcopy -O binary $< $@

build/memtest68-fd.xdf: build/boot/ipl.bin build/boot/standalone.bin tools/make_fd.py
	python3 tools/make_fd.py build/boot/ipl.bin build/boot/standalone.bin $@

fd: build/memtest68-fd.xdf

build/boot/hds_ipl.elf: build/boot/hds_ipl.o
	$(CROSS)ld -Ttext=0x2000 -e _ipl -o $@ $<

build/boot/hds_ipl.bin: build/boot/hds_ipl.elf
	$(CROSS)objcopy -O binary $< $@

build/boot/hds_main.o: boot/main.c boot/format.h boot/probe.h include/memtest.h | build/boot
	$(CC68) $(CPPFLAGS) $(BOOTFLAGS) -DBOOT_HDS -c -o $@ $<

build/boot/hds_standalone.elf: $(filter-out build/boot/main.o,$(BOOTOBJ)) build/boot/hds_main.o build/boot/hds_restart.o boot/standalone.ld
	$(CROSS)ld -T boot/standalone.ld -Map=build/boot/hds_standalone.map -o $@ $(filter-out build/boot/main.o,$(BOOTOBJ)) build/boot/hds_main.o build/boot/hds_restart.o -L$(LIB68) -lx68kiocs $(LIBGCC)

build/boot/hds_standalone.bin: build/boot/hds_standalone.elf
	$(CROSS)objcopy -O binary $< $@

build/memtest68-hd.hds: build/boot/hds_ipl.bin build/boot/hds_standalone.bin tools/make_hds.py tools/make_fd.py
	python3 tools/make_hds.py build/boot/hds_ipl.bin build/boot/hds_standalone.bin $@

hds: build/memtest68-hd.hds

$(OUT): src/main.c src/memtest.c src/memtest68k.h include/memtest.h build/boot/memtest68k.o | build
	$(CC68) $(CPPFLAGS) $(CFLAGS68) -o $@ src/main.c src/memtest.c build/boot/memtest68k.o

build/test_memtest: tests/test_memtest.c src/memtest.c include/memtest.h | build
	$(HOSTCC) $(CPPFLAGS) $(HOSTFLAGS) -fsanitize=address,undefined -g -o $@ tests/test_memtest.c src/memtest.c

build/test_boot_format: tests/test_boot_format.c boot/format.c boot/format.h | build
	$(HOSTCC) $(HOSTFLAGS) -Iboot -fsanitize=address,undefined -g -o $@ tests/test_boot_format.c boot/format.c

test: build/test_memtest build/test_boot_format
	./build/test_memtest
	./build/test_boot_format
	python3 -B tests/test_fd_image.py
	python3 -B tests/test_hds_image.py

check: all fd test

clean:
	rm -rf build

build/memtest_reference.o: src/memtest.c include/memtest.h | build
	$(CC68) $(CPPFLAGS) $(CFLAGS68) -DMT_PORTABLE -Dmt_run=ref_mt_run -Dmt_run_ex=ref_mt_run_ex -Dmt_test_names=ref_test_names -c -o $@ $<

build/test_native.x: tests/test_native.c src/memtest.c src/memtest68k.h include/memtest.h build/memtest_reference.o build/boot/memtest68k.o | build
	$(CC68) $(CPPFLAGS) $(CFLAGS68) -o $@ tests/test_native.c src/memtest.c build/memtest_reference.o build/boot/memtest68k.o

build/test_engine68.x: tests/test_memtest.c src/memtest.c src/memtest68k.h include/memtest.h build/boot/memtest68k.o | build
	$(CC68) $(CPPFLAGS) $(CFLAGS68) -o $@ tests/test_memtest.c src/memtest.c build/boot/memtest68k.o

RUN68 ?= run68
.PHONY: test-native
test-native: build/test_native.x build/test_engine68.x
	$(RUN68) build/test_engine68.x
	$(RUN68) build/test_native.x

build/bench_engine.o: tools/bench_engine.c include/memtest.h | build
	$(CC68) $(CPPFLAGS) $(BOOTFLAGS) -c -o $@ $<

build/bench_engine.elf: build/boot/start.o build/bench_engine.o build/boot/low_probe.o build/boot/memtest.o build/boot/memtest68k.o boot/standalone.ld
	$(CROSS)ld -T boot/standalone.ld -o $@ $(filter %.o,$^) -L$(LIB68) -lx68kiocs $(LIBGCC)

build/bench_engine.bin: build/bench_engine.elf
	$(CROSS)objcopy -O binary $< $@

build/bench-engine.xdf: build/boot/ipl.bin build/bench_engine.bin tools/make_fd.py
	python3 tools/make_fd.py build/boot/ipl.bin build/bench_engine.bin $@
