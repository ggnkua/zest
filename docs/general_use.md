---
title: "General use"
date: 2025-06-08
---

## Boot

To power on the board, you can either connect it to one of your PC's USB ports using a USB cable, or directly use a USB power supply.

The ST should boot after less than 5 seconds.

**Important:** On the Z7-Lite board, you can use any of the two USB slave ports (UART or JTAG) for power supply.
However, this board is known to provide reliable power to its USB slave port only when both UART and JTAG are connected to a power source such as a PC's USB port.
You may not get enough power for your USB devices (or even, the board may not work at all) if you use only one of UART or JTAG ports to power the board.


## Setup menu

By default, the ST starts without any floppy inserted.

You can get access to the setup menu by pressing either of the **Scroll Lock** or **Page Up** keys, or using the **Meta+Enter** key combo.
From there, you can access different functions including rebooting the ST, selecting a floppy disk image, choosing the RAM size.

All dialogs use a system of lists: lists of settings, files, or MIDI devices.

You can navigate between the different items in a list using the **Up** and **Down** arrow keys.
The **Page Up** and **Page Down** keys jumps up and down by several items, allowing to navigate faster in long lists.
The **Home** and **End** keys allow to jump to the first and last item in a list, respectively.
To activate the selected list entry, press **Enter**.
A multi-value setting can be changed by highlighting it, then using the **Left** and **Right** arrow keys to navigate the different values.

File selection entries will show a file selector for you to select another floppy, hard disk or ROM image.

Floppy and hard disk image file selection entries are “removable”: this means you can “eject” a floppy disk image, or disable hard disk emulation.
This can be done by highlighting the corresponding menu entry, then pressing **Del** or **Backspace** instead of **Enter**.
The behaviour is similar with MIDI ports, on which you may decide to disconnect the currently used device on any of the *in* or *out* ports.


To exit the setup menu, press **Esc**.

### File selection

Navigating the files in the file selector is similar to the navigation in the menu entries. Press **Enter** to choose a file or directory, or **Esc** to cancel the file selection.

You can also choose a directory to browse its files and subdirectories.

The SD card root directory can be accessed from the `/sdcard` directory.

### MIDI device selection

zeST supports any MIDI device that can be connected to the FPGA board's USB port.
To be able to use a MIDI device with zeST, you need to associate or *connect* it to
at least one of the Atari's virtual MIDI *in* and *out* ports.
This can be done using the *MIDI In* and *MIDI Out* entries in the *Settings* submenu.

The selection process for MIDI devices consists in associating one of the connected USB MIDI
devices to either the MIDI *in*, or *out* ports of the Atari machine.
The same device can be connected to both ports.
You can virtually disconnect a MIDI device from any of the Atari's MIDI ports by pressing **Del**
or **Backspace** when the device selection menu item for the chosen port is highlighted.

### Save settings

The *Save settings* menu entry saves all the chosen settings to the `zest.cfg` file on the SD card.

## Joystick emulation

If you press **Num Lock** or use the **Meta+J** key combo, this enables or disables joystick emulation.
A LED on the board should light up when joystick emulation is on.
An alert message showing the current status of joystick emulation is briefly shown on screen.

When joystick emulation is on, the arrow keys will act as the joystick directions, and the **Left shift** key becomes the *fire* button.

## Volume control

The multimedia keys **Volume Up**, **Volume Down** and **Mute** allow you to control the sound level.

