#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of FLOPPY is used by 'build'.
#

ROOT_DEV = /dev/hdb1

#
# uncomment this if you want kernel profiling: the profile_shift is the
# granularity of the profiling (5 = 32-byte granularity)
#

#PROFILING = -DPROFILE_SHIFT=2

#
# uncomment the correct keyboard:
#
# The value of KBDFLAGS should be or'ed together from the following
# bits, depending on which features you want enabled.
# 0x80 - Off: the Alt key will set bit 7 if pressed together with
#             another key.
#        On:  the Alt key will NOT set the high bit; an escape
#             character is prepended instead.
# The least significant bits control if the following keys are "dead".
# The key is dead by default if the bit is on.
# 0x01 - backquote (`)
# 0x02 - accent acute
# 0x04 - circumflex (^)
# 0x08 - tilde (~)
# 0x10 - dieresis (umlaut)

KEYBOARD = -DKBD_FINNISH -DKBDFLAGS=0
# KEYBOARD = -DKBD_FINNISH_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_US -DKBDFLAGS=0
# KEYBOARD = -DKBD_GR -DKBDFLAGS=0
# KEYBOARD = -DKBD_GR_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_FR -DKBDFLAGS=0
# KEYBOARD = -DKBD_FR_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_UK -DKBDFLAGS=0
# KEYBOARD = -DKBD_DK -DKBDFLAGS=0
# KEYBOARD = -DKBD_DK_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKBD_DVORAK -DKBDFLAGS=0
# KEYBOARD = -DKBD_SG -DKBDFLAGS=0
# KEYBOARD = -DKBD_SG_LATIN1 -DKBDFLAGS=0x9F
# KEYBOARD = -DKDB_NO

#
# comment this line to turn off keyboard NUM LOCK
#

NUM_LOCK = -DKBD_NUMERIC_LOCK

#
# comment this line if you don't want the emulation-code
#

MATH_EMULATION = -DKERNEL_MATH_EMULATION

#
# standard CFLAGS
#

CFLAGS =-Wall -O6 -fomit-frame-pointer

#
# if you want the ram-disk device, define this to be the
# size in blocks.
#

#RAMDISK = -DRAMDISK=512

AS86	=as86 -0 -a
LD86	=ld86 -0

#
# If you want to preset the SVGA mode, uncomment the next line and
# set SVGA_MODE to whatever number you want.
# Set it to -DSVGA_MODE=NORMAL_VGA if you just want the EGA/VGA mode.
# The number is the same as you would ordinarily press at bootup.
#
#SVGA_MODE=	-DSVGA_MODE=1

AS	=as
LD	=ld
HOSTCC	=gcc -static
CC	=gcc -DKERNEL
MAKE	=make
CPP	=$(CC) -E
AR	=ar

ARCHIVES	=kernel/kernel.o mm/mm.o fs/fs.o net/net.o
FILESYSTEMS	=fs/minix/minix.o fs/ext/ext.o fs/msdos/msdos.o fs/proc/proc.o
DRIVERS		=kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a \
		 kernel/blk_drv/scsi/scsi.a
MATH		=kernel/math/math.a
LIBS		=lib/lib.a
SUBDIRS		=kernel mm fs net lib

KERNELHDRS	=/usr/src/linux/include

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

all:	Version Image

lilo: Image
	if [ -f /vmlinux ]; then mv /vmlinux /vmlinux.old; fi
	dd if=Image of=/vmlinux
	/etc/lilo/lilo -b /dev/hda /vmlinux

linuxsubdirs: dummy
	@for i in $(SUBDIRS); do (cd $$i && echo $$i && $(MAKE)) || exit; done

Version:
	@./makever.sh
	@echo \#define UTS_RELEASE \"0.97.pl5-`cat .version`\" > tools/version.h
	@echo \#define UTS_VERSION \"`date +%D`\" >> tools/version.h
	@echo \#define LINUX_COMPILE_TIME \"`date +%T`\" >> tools/version.h
	@echo \#define LINUX_COMPILE_BY \"`whoami`\" >> tools/version.h
	@echo \#define LINUX_COMPILE_HOST \"`hostname`\" >> tools/version.h

Image: boot/bootsect boot/setup tools/system tools/build
	cp tools/system system.tmp
	strip system.tmp
	tools/build boot/bootsect boot/setup system.tmp $(ROOT_DEV) > Image
	rm system.tmp
	sync

disk: Image
	dd bs=8192 if=Image of=/dev/fd0

tools/build: tools/build.c
	$(HOSTCC) $(CFLAGS) \
	-o tools/build tools/build.c

boot/head.o: boot/head.s

tools/version.o: tools/version.c tools/version.h

init/main.o: init/main.c
	$(CC) $(CFLAGS) $(PROFILING) -c -o $*.o $<

tools/system:	boot/head.o init/main.o tools/version.o linuxsubdirs
	$(LD) $(LDFLAGS) -M boot/head.o init/main.o tools/version.o \
		$(ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(MATH) \
		$(LIBS) \
		-o tools/system > System.map

boot/setup: boot/setup.s
	$(AS86) -o boot/setup.o boot/setup.s
	$(LD86) -s -o boot/setup boot/setup.o

boot/setup.s:	boot/setup.S include/linux/config.h
	$(CPP) -traditional $(SVGA_MODE) boot/setup.S -o boot/setup.s

boot/bootsect.s:	boot/bootsect.S include/linux/config.h
	$(CPP) -traditional boot/bootsect.S -o boot/bootsect.s

boot/bootsect:	boot/bootsect.s
	$(AS86) -o boot/bootsect.o boot/bootsect.s
	$(LD86) -s -o boot/bootsect boot/bootsect.o

fs: dummy
	$(MAKE) linuxsubdirs SUBDIRS=fs

mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=mm

kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=kernel

clean:
	rm -f Image System.map tmp_make core boot/bootsect boot/setup \
		boot/bootsect.s boot/setup.s init/main.s
	rm -f init/*.o tools/system tools/build boot/*.o tools/*.o
	for i in $(SUBDIRS); do (cd $$i && $(MAKE) clean); done

backup: clean
	cd .. && tar cf - linux | compress - > backup.Z
	sync

depend dep:
	sed '/\#\#\# Dependencies/q' < Makefile > tmp_make
	for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done >> tmp_make
	cp tmp_make Makefile
	for i in $(SUBDIRS); do (cd $$i && $(MAKE) dep) || exit; done

dummy:

### Dependencies:
init/main.o : init/main.c /usr/src/linux/include/stdarg.h /usr/src/linux/include/time.h \
  /usr/src/linux/include/asm/system.h /usr/src/linux/include/asm/io.h /usr/src/linux/include/linux/types.h \
  /usr/src/linux/include/linux/fcntl.h /usr/src/linux/include/linux/config.h /usr/src/linux/include/linux/config_rel.h \
  /usr/src/linux/include/linux/config_ver.h /usr/src/linux/include/linux/config.dist.h \
  /usr/src/linux/include/linux/sched.h /usr/src/linux/include/linux/head.h /usr/src/linux/include/linux/fs.h \
  /usr/src/linux/include/linux/limits.h /usr/src/linux/include/linux/wait.h /usr/src/linux/include/linux/dirent.h \
  /usr/src/linux/include/linux/vfs.h /usr/src/linux/include/linux/minix_fs_sb.h \
  /usr/src/linux/include/linux/ext_fs_sb.h /usr/src/linux/include/linux/msdos_fs_sb.h \
  /usr/src/linux/include/linux/mm.h /usr/src/linux/include/linux/kernel.h /usr/src/linux/include/linux/signal.h \
  /usr/src/linux/include/linux/time.h /usr/src/linux/include/linux/param.h /usr/src/linux/include/linux/resource.h \
  /usr/src/linux/include/linux/tty.h /usr/src/linux/include/linux/termios.h /usr/src/linux/include/linux/unistd.h 
