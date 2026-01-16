# Communication with the Linux system

One part of zeST runs on your board's Arm CPU, running a Linux embedded environment.
zeST uses a specific piece of software running on Linux, providing simplified access to the USB peripherals and storage media for emulation
of Atari-compatible peripherals (keyboard, mouse, joystick, floppy drive, etc).

zeST's Linux operating system is an embedded system built using [Buildroot](https://buildroot.org/).
It is a minimal system that runs in a ramdisk.
An overlay file system image (named `overlay` in the root of the SD card) is created on first boot,
and is used to permanently store file modifications in specific locations such as `/etc` or `/var`.
This allows you to configure specific aspects that are not (yet) managed by the zeST configuration
interface, such as networking or wireless devices.

Since it is a fully functional Linux system, it comes with file management tools, a text editor (`vi`),
and other command line utilities.

Advanced or even intermediary users can interact with it, to perform different tasks:
managing files, transferring files from/to your personal computer, configuration, and more,
directly on the board's filesystem.

This document presents ways to interact with the zeST board using a computer (PC running Windows or Linux, Mac).

## Serial console

For serial access you will need a terminal emulator on the computer.

 - on Windows or Mac, you can use a tool like [Tabby](https://tabby.sh/).
 - on Linux: minicom

The board's serial port is configured at 921600 bps, 8 bit, 1 stop bit, so configure your terminal emulator accordingly.
This setting has been chosen to speed up file transfers through serial.
Typical serial port is `COM8` on Windows, or `/dev/ttyUSB0` on Linux.

Connect your board to the computer with a standard USB cable.
That cable must be connected to the UART port of the board.

Once connected to the board, press Enter.
This should display a `Login:` prompt.
Type `root`, press Enter, and if you see the `#` prompt, this means you have successfully connected to the root account on the board.

This is a standard [unix shell](https://en.wikipedia.org/wiki/Unix_shell) session, so you can issue the usual shell commands.
We will not describe here how to use a unix shell, there are plenty of useful webpages for that, such as [this one](https://www.stationx.net/unix-commands-cheat-sheet/) or [this one](https://www.geeksforgeeks.org/basic-shell-commands-in-linux/).


## Networking configuration

Networking is managed using ConnMan.
It is a daemon that automates most of the networking management.
It has integrated DHCP management and network time synchronisation.

### Ethernet

Ethernet should work out-of-the-box.; ConnMan is configured to detect the IP address with DHCP.

### Wi-Fi

There is experimental support for Wi-Fi in zeST.
Since none of the boards zeST supports has an integrated Wi-Fi device, Wi-Fi
only works through the use of a USB Wi-Fi dongle.

Only a few USB Wi-Fi drivers are integrated in zeST.
They are RTL87xx and RTL88xx drivers from Realtek.
If you would like support for another device, please [contact me](/support).

ConnMan automatically detects new USB Wi-Fi devices
and connects to its known configured network.

To set up a new Wi-Fi connection, run `connmanctl` in interactive mode:

    # connmanctl

On first use, enable Wi-Fi:

    connmanctl> enable wifi

Start a scan for available networks:

    connmanctl> scan wifi

After a few seconds you get the response: `Scan completed for wifi`.
Get the list of networks:

    connmanctl> services

You get a list of network names with associated IDs starting with `wifi_`.

Activate the agent that manages user requests:

    connmanctl> agent on

Connect to your network using its ID:

    connmanctl> connect wifi_001f1fa3d718_4b616d6f756c6f78_managed_psk

You will be prompted to input the Wi-Fi security passphrase.
Enter it, then type:

    connmanctl> quit

You should be connected to the network after a few seconds.


## SSH

SSH is a secure and convenient remote login and file copy service.

OpenSSH is configured, but you need some manual configuration to have it working.
Since there is no root password, ssh password login is disabled for root.

The recommended way is to use public key authentication.

From the serial console, when in the root user home directory, send your public
key file using [zmodem](#file-transfer-through-serial-zmodem).
Supposing the key file name is `id_rsa.pub`, use the command:

    # mkdir -p .ssh
    # chmod 700 .ssh
    # mv id_rsa.pub .ssh/authorized_keys
    # chmod 600 .ssh/authorized_keys

Now the board is accessible as root using that public key.


## Start and stop zeST

Before manipulating files that are being used in zeST (such as the hard disk image file), you need to stop zeST.
This is done with the command:

    zestboot stop

To start zeST again:

    zestboot start

To just restart zeST:

    zestboot restart

Note that the `zestboot` script runs zeST so that it uses the default `/sdcard/zest.cfg` configuration file.
You can have it use a different configuration file with the command:

    zeST /path/to/config.cfg

In that case, zeST runs as a foreground process.
You can stop it again by pressing Ctrl+C.


## SD card mount point

The contents of the SD card can be seen in the `/sdcard` folder.

## File transfer through serial (zmodem)

To send a file from the computer to the current directory of your shell session, just initiate a zmodem transfer on your terminal utility (minicom: press Ctrl+a, then s).
The terminal utility will make use of the pre-installed `rz` command on the zeST linux system so the files are written on the current directory.

To send a file from the zeST shell session to your computer, use the `sz filename` command.

## Mount a partition from an Atari-formatted hard disk image file

Mounting a partition gives you access to its contents in a system directory.
From there you can copy, rename, delete files, transfer them with zmodem or ssh, whatever you may do on a regular disk drive.

**Warning #1**: Make sure you have stopped zeST before doing the following operations.
There is a risk that your data gets corrupted if you access the contents of the image when zeST is already accessing it.
To stop zeST, follow the instructions in the [Start and stop zeST](#start-and-stop-zest) section.

**Warning #2**: Once you are done with file management in the partition, you must unmount it before starting zeST again.

To mount the partition, you need to know the offset for that partition in the image.

To list the partitions in a `hdd.img` file along with their offsets, use the command:

    parted hdd.img unit b p

You will get a result like this:

```raw
Model:  (file)
Disk /sdcard/hdd.img: 190840832B
Sector size (logical/physical): 512B/512B
Partition Table: atari
Disk Flags:

Number  Start      End         Size       Type     File system  Flags
 1      1024B      33554431B   33553408B  primary
 2      33554432B  96468991B   62914560B  primary  fat16
 3      96468992B  190840831B  94371840B  primary  fat16
```

In the above example, there are 3 partitions.
The first partition has offset 1024, the second partition has offset 33554432,
and the third partition has offset 96468992.

The command for mounting a partition with offset 1024 from an image file located in `/sdcard/hdd.img` is the following:

    mount -oloop,offset=1024 -tmsdos /sdcard/hdd.img /mnt

The partition contents should then be accessible in the `/mnt` folder.
You can perform any file copies, deletions, renames, as you want.

When you are done with your modifications, unmount the partition with the command:

    umount /mnt


# Bluetooth configuration

The Linux system has basic support for Bluetooth through the use of a USB Bluetooth dongle.
This allows you to pair wireless devices such as a keyboard, a mouse or a game controller.

For now, Bluetooth is configured from the command line, using the `bluetoothctl` utility.

The basic use is the following.
After starting `bluetoothctl`, power up the device with the command:

```
power on
```

To pair a new device, set it in pair mode, then type the command:

```
scan on
```

A list of devices should appear, with their respective MAC addresses.
When you see your device, supposing its MAC address is `11:22:33:44:55:66`,
connect to it with the command:

```
connect 11:22:33:44:55:66
```

**note:** you do not have to type the whole MAC address.
Just type the first digits/characters and use the **Tab** key to auto complete.

After a successful connection, you can pair it with the command:

```
pair 11:22:33:44:55:66
```

From there, your device should be usable in zeST.
To have the device automatically paired each time it is turned on, type the command:

```
trust 11:22:33:44:55:66
```


For more complete Bluetooth command line tutorials, you can check
[this link](https://www.baeldung.com/linux/bluetooth-via-terminal)
or [that one](https://www.makeuseof.com/manage-bluetooth-linux-with-bluetoothctl/).
