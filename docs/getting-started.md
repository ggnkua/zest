---
title: "Getting started"
date: 2022-03-15T18:00.00+01:00
lastmod: 2025-08-14
---

This page explains how to get zeST up and running for your FPGA board.

# Download

The latest build of zeST can be downloaded by clicking [this link](https://zest.sector1.fr/download/zeST-20250814.tar.xz).

You can also access older releases by browsing the [download directory](https://zest.sector1.fr/download/).

The binary archive contains builds for the Microphase Z7-Lite board, 7010 and 7020 versions, as well as the TE0726 "ZynqBerry".

Please note that zeST also supports the MyIR Z-Turn 7020 board. For now the build is not available in the binary archive, but if you own this board and want the build to be included in the archive, just [let me know](https://zest.sector1.fr/support/).

# Installation

If you just acquired a brand new ZynqBerry, there are [specific first-time-only setup instructions](/doc/zynqberry_setup) you need to follow first.
For the other supported boards, all the necessary installation instructions follow.

To install zeST on your board, just extract all the files from the archive into the root directory of a FAT32-formatted micro-SD card, and also copy the `boot.bin` file specific to your board (from the `boards` directory) to that same root directory.

Then, copy some of the floppy or hard disk image files you want to use to the SD card (either in the root directory or in a subdirectory). Supported floppy file formats are : ST, MSA, and MFM (zeST's [internal format](/posts/floppy_emulation/)). If present, a `hdd.img` hard disk image file will be mounted as the default ACSI hard disk.

If you want you can include your preferred ROM image file, that must be ST-compatible.
zeST supports ST-compatible TOS ROM versions: 1.00 to 1.04, 2.06 and any version of [EmuTOS](https://emutos.sourceforge.io/).
If no custom ROM file is provided, zeST falls back to a default EmuTOS ROM (UK version, 256KB).

Now insert the SD card into the board's SD card drive.

You should also at least connect a standard USB keyboard on the board's USB host port to be able to do anything with the ST.
You can also use a USB keyboard/mouse combo, or separate keyboard and mouse connected through a USB hub.

Of course you will also need to connect the board to a HDMI screen.

After following these steps, your board is ready to boot zeST.
You can follow the [general use instructions](/doc/general_use/) to learn how to use zeST efficiently.
