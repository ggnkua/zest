# Makefile - Generate zest host program
#
# Copyright (c) 2020-2026 Francois Galea <fgalea at free.fr>
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# You can set the path to your local buildroot toolchain by uncommenting the
# following line and setting the correct path.
#export PATH:=$(HOME)/src/buildroot-2024.02.9/output/host/bin:$(PATH)

TOOLCHAIN=arm-linux-
CC=$(TOOLCHAIN)gcc
CXX=$(TOOLCHAIN)g++
LD=$(TOOLCHAIN)ld
AR=$(TOOLCHAIN)ar

TARGETS=zeST
CFLAGS=-g -Wall -Os
LDLIBS=`pkg-config --libs inih`
LDFLAGS=-s
AS=vasmm68k_mot

all: $(TARGETS)

clean:
	rm -f *.o *.bin *.prg *.img gdboot_img.c $(TARGETS)

zeST: setup.o floppy.o floppy_img.o acsi.o ikbd.o sil9022a.o osd.o menu.o input.o config.o listview.o misc.o font.o infomsg.o midi.o gemdos.o gdboot_img.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

setup.o: setup.c config.h setup.h

config.o: config.c config.h

menu.o: menu.c listview.h menu.h misc.h config.h setup.h floppy.h acsi.h midi.h

osd.o: osd.c osd.h

floppy.o: floppy.c floppy.h floppy_img.h acsi.h midi.h config.h

floppy_img.o: floppy_img.c floppy_img.h

ikbd.o: ikbd.c input.h menu.h config.h infomsg.h setup.h

listview.o: listview.c listview.h input.h osd.h font.h misc.h

misc.o: misc.c misc.h

font.o: font.c font.h

acsi.o: acsi.c acsi.h gemdos.h config.h

gemdos.o: gemdos.c gemdos.h config.h

midi.o: midi.c midi.h config.h

infomsg.o: infomsg.c infomsg.h osd.h setup.h font.h config.h

%.prg: %.s
	$(AS) -m68000 -Ftos -tos-flags=7 -nosym $< -o $@

gdboot.bin: gdboot.s
	$(AS) -m68000 -Fbin -pic $< -o $@
	truncate -s 510 $@
	od --endian=big -An -tu2 $@|awk '{for(i=1;i<=NF;i++) sum+=$$i} END {printf "%04X",(70196-sum%65536)%65536}'|xxd -r -p>>$@

gdboot.img: gdboot.bin gdstub.prg
	cat $^ > $@
	truncate -s 2048 $@

gdboot_img.c: gdboot.img
	xxd -i $< > $@
