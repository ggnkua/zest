---
title: "Drivers"
date: 2025-06-08
---

zeST provides specific drivers to improve the use of zeST.
They consist in Atari binary files to be placed in the `AUTO` folder of your boot disk.
They are located in the zeST binary archive, in the `drivers` subfolder.

## Extended video modes

When (and only when) the `extended_video_modes` option is enabled, zeST includes additional, nonstandard screen modes.
There is one for each of low and medium resolutions.

Technically, those extended modes are made available to programs by enabling bit 2 of the ST's resolution hardware register (at address `$ff8260`).

You can have the GEM desktop use these modes by copying the supplied `extmod.prg` driver program into the `AUTO` folder of your boot disk.

## PC keyboard support

Since zeST uses a PC keyboard instead of the original ST keyboard, some keys do not exactly produce the expected characters because what zeST does is simply map each PC key to the most equivalent ST key.

To overcome this issue, [zkbd](https://codeberg.org/zerkman/zkbd) has been written, and provides drivers for several modern PC key maps.

Pre-built binaries for zkbd can be found in the downloadable zeST archive, under the `drivers/zkbd` directory. Just copy the one corresponding to your keyboard into the `AUTO` folder of your boot disk.
