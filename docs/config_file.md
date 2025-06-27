---
title: "Configuration file"
date: 2022-03-15T18:00.00+01:00
lastmod: 2025-02-05
---

# Configuration file

The `zest.cfg` file contains default configuration values. The format is a INI-style key-value text file. It is divided in two sections, **[main]** and **[floppy]**.

The different possible settings are listed below. For file path names, the SD card mount point is `/sdcard`, so if you need to address for instance a file named `foo.msa` from a `floppy` directory which resides in the root directory of the SD card, the full path name would be `/sdcard/floppy/foo.msa`.

## [main] section

 - `mono`: boolean setting (true/false). If true, zeST starts in monochrome high resolution mode.
 - `extended_video_modes`: boolean setting (true/false). If true, extra resolution modes become available to software running on zeST. See the [dedicated section](/drivers/#extended-video-modes) for more details.
 - `mem_size`: memory size. Possible values are: 256K, 512K, 1M, 2M, 2.5M, 4M, 8M, 14M.
 - `wakestate`: GLUE wakestate setting. Possible values are 1, 2, 3, 4.
 - `shifter_wakestate`: Shifter wakestate setting. Possible values are 0 and 1.
 - `rom_file`: full pathname to the default ROM image file.

## [floppy] section

 - `floppy_a`: full pathname for the floppy image file in drive A. If you don't want to have a floppy inserted at boot, just leave this field empty. You can also set it to a directory name with the `/` suffix to set a default image search directory.
 - `floppy_a_enable`: this is a true/false toggle to determine if floppy drive A is available. It is enabled by default.
 - `floppy_a_write_protect`: this is a true/false toggle to determine if inserted floppy disk in drive A is write protected or not.
 - `floppy_b`: same as `floppy_a`, but for drive B.
 - `floppy_b_enable`: same as `floppy_a`, but for drive B. It is disabled by default.
 - `floppy_b_write_protect`: same as `floppy_a`, but for drive B.

## [hdd] section

 - `image`: full pathname for a ACSI hard disk image file. You can mount an Atari-formatted SD card (for instance coming from an Ultrasatan drive) by plugging a USB SD card reader and using the device file name (typically, something like `/dev/sda`) as the image file.

## [keyboard] section

 - `right_alt_is_altgr`: boolean setting (true/false). If true, the keyboard's right Alt key is treated differently from left Alt. See the [PC keyboard support](/drivers/#pc-keyboard-support) section below for more details. If false, both Alt keys are mapped to the ST Alternate key.

## [jukebox] section

 - `enabled`: boolean setting (true/false). If true, jukebox mode is enabled and a new image file is chosen at each new period.
 - `path`: The path name of the floppy image files directory for jukebox mode.
 - `timeout`: Integer value. This is the jukebox period in seconds.



