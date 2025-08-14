---
title: "Hard disk management"
date: 2022-08-14T16:00.00+02:00
lastmod: 2025-08-14
---

# Hard disk management

zeST supports the emulation of ACSI hard disks.
Hard disks are materialised by *image files*, which contain the raw disk
content.
Just like any physical disk, image files may contain multiple partitions, which
can be formatted using any desired filesystem, however the usual filesystem on
Atari TOS is DOS.
zeST supports using up to 8 disk image files simultaneously.

Image files are raw binary files whose file names have a `.img` extension.

From the zeST menu, in the *Hard disks* submenu, you can choose the image files
for the different ACSI IDs (0 to 7) .
You can “unplug” a disk by highlighting the corresponding menu entry then
pressing **Delete** or **Backspace**.


## Hard disk image installation

There are different ways of getting started with disk image files.

### From an emulator

Software emulators such as Hatari or Steem SSE also support disk image files.
You can copy the image files you are using directly to your zeST SD card.
Just make sure their file name have a `.img` extension.



### Creating a blank disk image

From the [Linux command line](/doc/linux), you can directly create a blank file
of any size on the SD card with the command:

    cd /sdcard
    dd of=file.img count=0 bs=1M seek=<size-in-MiB>

This file can then be mounted in zeST; then you will have to partition it using
your favourite disk driver's partitioning utility.

### Creating a new, partitioned and formatted drive

zeST embeds a `make-hdd-image` disk image creation, partitioning and formatting
script that you can use from the [Linux command line](/doc/linux).

This script can create an image file containing one or more formatted partitions.

The script arguments are the name of the new file to be created, and the list
of sizes, in MiB (1024*1024 bytes) of the different partitions to create.
The image file size is then the sum of all partitions sizes.

For instance, to create a disk image with partitions of 32, 60 and 90 MiB, type
the commands:

    cd /sdcard
    make-hdd-image file.img 32 60 90

Note that if the first partition size is lower or equal to 32 megabytes, it
will be formatted in an Atari-specific, AHDI-compatible way.
