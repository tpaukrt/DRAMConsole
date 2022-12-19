# Dynamic RAM Console

## Introduction

  The Dynamic RAM Console is an alternative to Android ram-console and pstore
  console that does not require a dedicated memory region for its operation.
  It continually captures all console messages and stores them in a small
  dynamically allocated circular buffer in RAM. The content of this buffer
  persists across reboots and is available as /proc/kmsg.last.

## Prerequisites

  * Linux kernel 2.6.27+

## Build instructions

  1. Copy the file `kmsg_last.c` to the directory `linux-X.Y.Z/fs/proc`

  2. Add the following line to the end of file `linux-X.Y.Z/fs/proc/Makefile`

     ```
     proc-$(CONFIG_PRINTK) += kmsg_last.o
     ```

  3. Rebuild the Linux kernel

## Directory structure

  ```
  DRAMConsole
   |
   |--COPYING                   GNU General Public License version 2
   |--NEWS.md                   Version history
   |--README.md                 This file
   +--kmsg_last.c               The Dynamic RAM Console kernel module
  ```

## License

  The code is available under the GNU General Public License version 2 or later.
  See the `COPYING` file for the full license text.
