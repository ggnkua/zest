---
title: "Hard disk management"
date: 2022-08-14T16:00.00+02:00
lastmod: 2026-01-01
---

# Hard disk management

zeST supports the emulation of ACSI hard disks.
It also allows setting up a “GEMDOS drive”.

Hard disks are materialised by *image files*, which contain the raw disk
content.
Just like any physical disk, image files may contain multiple partitions, which
can be formatted using any desired filesystem, however the usual filesystem on
Atari TOS is DOS.
zeST supports using up to 8 disk image files simultaneously.

Image files are raw binary files whose file names have a `.img` extension.

The GEMDOS drive allows you to map a directory in the [Linux system](/doc/linux)
as a virtual disk unit on the Atari.

From the zeST menu, in the *Hard disks* submenu, you can choose the image files
for the different ACSI IDs (0 to 7), as well as define the directory that will
be mapped as the GEMDOS drive.

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


## The GEMDOS drive

The GEMDOS drive allows you to define a directory in the [Linux system](/doc/linux)
whose contained files will be present as a disk drive on the Atari system.
This way, files are directly accessible both on Linux and the Atari, making
file exchange easier between systems, allowing the use of networked drives, etc.

The GEMDOS drive is materialised as a specific ACSI drive unit, that will use
the first ACSI ID that is not assigned to a disk image file.

It requires a specific driver to work, that is called the [GEMDOS drive stub](/doc/drivers#gemdos-drive-stub).
This driver is automatically executed when the GEMDOS drive is set as the boot drive,
or can be placed on your boot drive's `AUTO` folder.

### Booting on the GEMDOS drive

The GEMDOS drive is a special ACSI bootable device.
It will actually boot when the following conditions are fulfilled:

 - zeST is using a ROM that supports booting on ACSI drives. This excludes EmuTOS that uses its own disk driver and does not run bootsectors.
 - There are no bootable drives assigned to ACSI IDs prior to the first unassigned one.
The simplest, safest way is to not assign a HDD image file to ACSI ID 0, so the GEMDOS drive will use ID 0 and will be the first bootable drive.

### File name restrictions

The zeST GEMDOS drive rules for file naming are rather straightforward:

 - File names must respect the 8.3 DOS name formatting. Files whose names do not respect this formatting are ignored and will not be visible on the Atari.
 - No restrictions in lower/upper case names. New files will be created with a name in lower case.
 - If two different files have names that map to the same upper case name, only one of them will be accessible on the Atari.
 - Not being a restriction *per se*, but in the current state of the implementation it is safer to avoid non-ASCII characters in file names.
