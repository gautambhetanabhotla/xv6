K=kernel
U=user
# All build artifacts (objects, kernel image, mkfs, fs.img) go here.
B=build
# Host directory tree copied verbatim into the xv6 file system by mkfs.
# User programs are linked into $(FS)/bin.
FS=fs

OBJS = \
  $B/$K/entry.o \
  $B/$K/start.o \
  $B/$K/console.o \
  $B/$K/printk.o \
  $B/$K/uart.o \
  $B/$K/kalloc.o \
  $B/$K/spinlock.o \
  $B/$K/string.o \
  $B/$K/main.o \
  $B/$K/vm.o \
  $B/$K/proc.o \
  $B/$K/swtch.o \
  $B/$K/trampoline.o \
  $B/$K/trap.o \
  $B/$K/syscall.o \
  $B/$K/sysproc.o \
  $B/$K/bio.o \
  $B/$K/fs.o \
  $B/$K/log.o \
  $B/$K/sleeplock.o \
  $B/$K/file.o \
  $B/$K/pipe.o \
  $B/$K/exec.o \
  $B/$K/sysfile.o \
  $B/$K/kernelvec.o \
  $B/$K/plic.o \
  $B/$K/virtio_disk.o

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX = 

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-elf-'; \
	elif riscv64-none-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-none-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64
MIN_QEMU_VERSION = 7.2

CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

CFLAGS = -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += -march=rv64gc
CFLAGS += -std=gnu99
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free
CFLAGS += -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -I.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

KERNEL = $B/$K/kernel

$(KERNEL): $(OBJS) $K/kernel.ld
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $@ $(OBJS)
	$(OBJDUMP) -S $@ > $B/$K/kernel.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $B/$K/kernel.sym

$B/$K/%.o: $K/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$B/$K/%.o: $K/%.S
	@mkdir -p $(@D)
	$(CC) -march=rv64gc -g -c -o $@ $<

tags: $(OBJS)
	etags kernel/*.S kernel/*.c

ULIB = $B/$U/ulib.o $B/$U/usys.o $B/$U/printf.o $B/$U/umalloc.o $B/$U/env.o

$B/$U/%.o: $U/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$B/$U/usys.S: $U/usys.pl
	@mkdir -p $(@D)
	perl $U/usys.pl > $@

$B/$U/usys.o: $B/$U/usys.S
	$(CC) $(CFLAGS) -c -o $@ $<

$(FS)/bin/%: $B/$U/%.o $(ULIB) $U/user.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $B/$U/$*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $B/$U/$*.sym

$(FS)/bin/forktest: $B/$U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $B/$U/forktest.o $B/$U/ulib.o $B/$U/usys.o
	$(OBJDUMP) -S $@ > $B/$U/forktest.asm

MKFS = $B/mkfs/mkfs

$(MKFS): mkfs/mkfs.c $K/fs.h $K/param.h $K/stat.h
	@mkdir -p $(@D)
	gcc -Wall -Wno-unknown-attributes -I. -o $@ mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: $B/$U/%.o

UPROGS=\
	$(FS)/bin/cat\
	$(FS)/bin/echo\
	$(FS)/bin/forktest\
	$(FS)/bin/grep\
	$(FS)/bin/init\
	$(FS)/bin/kill\
	$(FS)/bin/ln\
	$(FS)/bin/ls\
	$(FS)/bin/mkdir\
	$(FS)/bin/rm\
	$(FS)/bin/sh\
	$(FS)/bin/stressfs\
	$(FS)/bin/usertests\
	$(FS)/bin/grind\
	$(FS)/bin/wc\
	$(FS)/bin/zombie\
	$(FS)/bin/logstress\
	$(FS)/bin/forphan\
	$(FS)/bin/dorphan\
	$(FS)/bin/sync\
	$(FS)/bin/syscount\
	$(FS)/bin/alarmtest\
	$(FS)/bin/cowtest\
	$(FS)/bin/symlinktest\
	$(FS)/bin/readlink\
	$(FS)/bin/envtest\
	$(FS)/bin/envcheck\

# usertests expects README.md in the root directory.
$(FS)/README.md: README.md
	@mkdir -p $(@D)
	cp $< $@

# Everything under $(FS)/ (except symlinks, which may dangle on the host).
# Directories are included so that adding/removing files triggers a rebuild.
FSFILES := $(shell find $(FS) ! -type l 2>/dev/null)

FSIMG = $B/fs.img

# Make sure fs/root (the home directory) exists. Order-only, so its
# timestamp doesn't force image rebuilds on its own.
$(FS)/root:
	mkdir -p $@

$(FSIMG): $(MKFS) $(UPROGS) $(FS)/README.md $(FSFILES) | $(FS)/root
	$(MKFS) $@ $(FS)

-include $(wildcard $B/$K/*.d $B/$U/*.d)

clean:
	rm -rf $B $(FS)/bin $(FS)/README.md .gdbinit \
	*.tex *.dvi *.idx *.aux *.log *.ind *.ilg

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 3
endif

QEMUOPTS = -machine virt -bios none -kernel $(KERNEL) -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=$(FSIMG),if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: check-qemu-version $(KERNEL) $(FSIMG)
	$(QEMU) $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/; s|kernel/kernel|$(KERNEL)|" < $^ > $@

qemu-gdb: $(KERNEL) .gdbinit $(FSIMG)
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

print-gdbport:
	@echo $(GDBPORT)

QEMU_VERSION := $(shell $(QEMU) --version | head -n 1 | sed -E 's/^QEMU emulator version ([0-9]+\.[0-9]+)\..*/\1/')
check-qemu-version:
	@if [ "$(shell echo "$(QEMU_VERSION) >= $(MIN_QEMU_VERSION)" | bc)" -eq 0 ]; then \
		echo "ERROR: Need qemu version >= $(MIN_QEMU_VERSION)"; \
		exit 1; \
	fi

.PHONY: fmt clean qemu qemu-gdb print-gdbport check-qemu-version
fmt:
	clang-format -i $(wildcard kernel/*.[ch] user/*.[ch] mkfs/*.c)
