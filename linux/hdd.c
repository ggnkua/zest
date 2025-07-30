/*
 * hdd.c - hard disk drive emulation (software part)
 *
 * Copyright (c) 2023-2025 Francois Galea <fgalea at free.fr>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "hdd.h"
#include "config.h"

/* ACSI status codes */
#define STATUS_OK    0
#define STATUS_ERROR 2

// format: 0xAAQQSS  AA:additional sense QQ: additional sense code qualifier SS:sense key
#define ERROR_OK       0x000000          /* OK return status */
#define ERROR_NOSECTOR 0x010004          /* No index or sector */
#define ERROR_WRITEERR 0x030002          /* Write fault */
#define ERROR_OPCODE   0x200005          /* Opcode not supported */
#define ERROR_INVADDR  0x21000d          /* Invalid block address */
#define ERROR_INVARG   0x240005          /* Invalid argument */
#define ERROR_INVLUN   0x250005          /* Invalid LUN */


static volatile uint32_t *acsireg;
static volatile uint32_t *iobuf;

static int hdd_dev_id = 0;

static struct {
  int fd;           // disk image file handle
  int sectors;      // number of sectors
  unsigned int lba; // current logical block address
  unsigned int sense;
  int report_lba;   // report LBA in sense data
} img;

static void clear_sense_data(void) {
  img.sense = 0;
  img.report_lba = 0;
}

static void set_error(unsigned int err, int report_lba) {
  img.sense = err;
  img.report_lba = report_lba;
  *acsireg = STATUS_ERROR;
}

static void openimg(const char *filename) {
  if (filename) {
    img.fd = open(filename,O_RDWR);
    if (img.fd==-1) {
      printf("could not open HDD image file `%s`\n",filename);
      return;
    }
    off_t size = lseek(img.fd,0,SEEK_END);
    img.sectors = size/512;
    lseek(img.fd,0,SEEK_SET);
  }
}

static void closeimg(void) {
  if (img.fd!=-1) {
    close(img.fd);
  }
  img.fd = -1;
}

void hdd_changeimg(const char *full_pathname) {
  closeimg();
  openimg(full_pathname);
}

void hdd_init(volatile uint32_t *parmreg) {
  acsireg = (void*)(((uint8_t*)parmreg)+0x4000);
  iobuf = acsireg + (0x800/4);
  img.fd = -1;
  openimg(config.hdd_image);
  clear_sense_data();
}

void hdd_exit(void) {
  closeimg();
}

static unsigned char command[10];
static int dev_id = 0;
static int cmd_ext = 0;
static int cmd_size = 0;
static int cmd_rd_idx = 0;
static int dma_mode = 0;    // 0:idle 1:read 2:write
static int dma_buf_id = 0;
static int dma_rem_sectors = 0;

static void read_next(int bsize) {
  if (dma_rem_sectors==0) {
    // finish command
    *acsireg = STATUS_OK;
    dma_mode = 0;
  } else {
    // initiate DMA read
    ++img.lba;
    int nbs = (bsize-1)/16;
    *acsireg = 0x100 | nbs<<3 | dma_buf_id;
    if (--dma_rem_sectors>0) {
      dma_buf_id ^= 1;
      int offset = dma_buf_id*512;
      read(img.fd,((char*)iobuf)+offset,512);
    }
  }
}

static void write_first(void) {
  // initiate initial DMA write
  int nbs = 31;
  *acsireg = 0x200 | nbs<<3 | dma_buf_id;
}

static void write_next(void) {
  // initiate next DMA write
  int nbs = 31;
  if (--dma_rem_sectors>0) {
    *acsireg = 0x200 | nbs<<3 | (1-dma_buf_id);
  }
  ++img.lba;
  write(img.fd,((char*)iobuf)+dma_buf_id*512,512);
  dma_buf_id ^= 1;
  if (dma_rem_sectors==0) {
    // finish command
    *acsireg = STATUS_OK;
    dma_mode = 0;
  }
}

static void send_reply(const uint8_t *data, int size) {
  dma_mode = 1;
  dma_buf_id = 0;
  dma_rem_sectors = 1;
  memcpy((void*)iobuf,data,size);
  read_next(size);
}

// determine the command size depending on its header byte
static int command_size(unsigned int head) {
  if (head>=0xa0) return 12;
  if (head>=0x80) return 16;
  if (head>=0x20) return 10;
  return 6;
}

// Mode page 0 (disk and sector size)
void mode_sense_0(uint8_t *outBuf) {
  // Borrowed from acsi2stm who borrowed from Hatari

  uint32_t blocks = img.sectors;
  if(blocks > 0xffffff) {
    //dbg("(truncated) ");
    blocks = 0xffffff;
  }
  memset(outBuf,0,16);

  // Values got from the Hatari emulator
  outBuf[1] = 14;   // Remaining bytes
  outBuf[3] = 8;
  // Send the number of blocks of the HDD image
  outBuf[5] = (blocks >> 16) & 0xFF;
  outBuf[6] = (blocks >> 8) & 0xFF;
  outBuf[7] = (blocks) & 0xFF;
  // Sector size middle byte
  outBuf[10] = 2;
}

// Mode page 4 (disk geometry)
void mode_sense_4(uint8_t *outBuf) {
  // Borrowed from acsi2stm

  uint32_t blocks = img.sectors;
  int heads;
  int cylinders;
  for(heads = 255; heads >= 1; --heads) {
    cylinders = blocks / heads;
    if(cylinders > 0xffffff || (blocks % heads) == 0) {
      // if((blocks % heads) != 0)
      //   dbg("(truncated) ");
      break;
    }
  }

  memset(outBuf,0,24);

  // Rigid drive geometry
  outBuf[0] = 4; // Page code
  outBuf[1] = 22; // Page length

  // Send the number of blocks in CHS format
  outBuf[2] = (cylinders >> 16) & 0xFF;
  outBuf[3] = (cylinders >> 8) & 0xFF;
  outBuf[4] = (cylinders) & 0xFF;
  outBuf[5] = heads;
}

void hdd_interrupt(void) {
  unsigned int reg = *acsireg;

  // if no hard drive image is set, don't respond to commands
  if (img.fd==-1) return;

  if (dma_mode==1) {
    // a DMA read command is running
    read_next(512);
    return;
  }
  if (dma_mode==2) {
    // a DMA write command is running
    write_next();
    return;
  }

  int d = reg&0xff;
  int a1 = (reg>>8)&1;
  //printf("received: d=%d, a1=%d\n",d,a1);

  if (cmd_rd_idx==0&&cmd_ext==0&&a1==1) {
    // we can safely ignore bytes as long as they do not start a new command
    return;
  } else if ((cmd_rd_idx>0||cmd_ext>0)&&a1==0) {
    // initial byte in the middle of a command
    printf("ACSI error: cmd byte #%d, A1=0\n",cmd_rd_idx);
    cmd_rd_idx = 0;
    *acsireg = STATUS_ERROR;
    return;
  }

  if (cmd_rd_idx==0) {
    int cmd = d;
    if (cmd_ext==0) {
      // command byte
      dev_id = d>>5;
      // ignore command if wrong device ID
      if (dev_id!=hdd_dev_id) return;
      cmd = d&0x1f;
      if (cmd==0x1f) {
        // ICD command extension
        cmd_ext = 1;
        *acsireg = STATUS_OK;
        return;
      }
    }
    if (cmd!=0 && cmd!=3 && cmd!=8 && cmd!=0x0a && cmd!=0x12 && cmd!=0x1a && cmd!=0x25) {
      set_error(ERROR_OPCODE,0);
      return;
    }
    cmd_size = command_size(cmd);
    command[cmd_rd_idx++] = cmd;
  } else if (dev_id==hdd_dev_id) {
    command[cmd_rd_idx++] = d;
  }
  if (cmd_rd_idx==cmd_size) {
    cmd_rd_idx = 0;
    cmd_ext = 0;
    int cmd = command[0];
    if (cmd==0) {
      // send response, no error
      *acsireg = STATUS_OK;
      return;
    }
    else if (cmd==3) {
      // request sense
      uint8_t data[256];
      unsigned int length = command[4];
      if (length<4) {
        length = 4;
      }
      memset(data,0,length);
      if (length<=4) {
        data[0] = (img.sense>>16)&0xff;   // additional sense code
        if (img.report_lba) {
          data[0] |= 0x80;
          data[1] = (img.lba>>16)&0xff;
          data[2] = (img.lba>>8)&0xff;
          data[3] = img.lba&0xff;
        }
      } else {
        data[0] = 0x70;
        if (img.report_lba) {
          data[0] |= 0x80;
          data[3] = (img.lba>>24)&0xff;
          data[4] = (img.lba>>16)&0xff;
          data[5] = (img.lba>>8)&0xff;
          data[6] = img.lba&0xff;
        }
        data[2] = img.sense&0x0f;         // sense key
        data[7] = 10;   // additional sense length
        data[12] = (img.sense>>16)&0xff;  // additional sense code
        data[13] = (img.sense>>8)&0xff;   // additional sense code qualifier
      }
      send_reply(data,length);
      clear_sense_data();
      return;
    }
    else if (cmd==8) {
      // read
      img.lba = (command[1]<<8|command[2])<<8|command[3];
      dma_rem_sectors = command[4];
      if (img.lba >= img.sectors) {
        set_error(ERROR_INVADDR,1);
        return;
      }
      if (img.lba + dma_rem_sectors > img.sectors) {
        img.lba = img.sectors;
        set_error(ERROR_INVADDR,1);
        return;
      }
      dma_mode = 1;
      dma_buf_id = 0;
      lseek(img.fd,img.lba*512,SEEK_SET);
      read(img.fd,(void*)iobuf,512);
      read_next(512);
      return;
    }
    else if (cmd==0x0a) {
      // write
      int sector = (command[1]<<8|command[2])<<8|command[3];
      dma_rem_sectors = command[4];
      if (img.lba >= img.sectors) {
        img.lba = sector;
        set_error(ERROR_INVADDR,1);
        return;
      }
      if (sector + dma_rem_sectors > img.sectors) {
        img.lba = img.sectors;
        set_error(ERROR_INVADDR,1);
        return;
      }
      dma_mode = 2;
      dma_buf_id = 0;
      lseek(img.fd,sector*512,SEEK_SET);
      write_first();
      return;
    }
    else if (cmd==0x12) {
      // inquiry
      static const uint8_t data[48] =
        "\x00\x00\x01\x00\x1f\x00\x00\x00"
        "zeST    "
        "EmulatedHarddisk"
        "0100" "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
      int alloc = command[3]<<8 | command[4];
      if (alloc>48) alloc = 48;
      send_reply(data,alloc);
      return;
    }
    else if (cmd==0x1a) {
      // mode sense
      // Borrowed from acsi2stm who borrowed from Hatari
      uint8_t data[48];
      switch (command[2]) {
      case 0:
        mode_sense_0(data);
        send_reply(data,16);
        break;
      case 4:
        mode_sense_4(data);
        send_reply(data,24);
        break;
      case 0x3f:
        data[0] = 43;
        data[1] = 0;
        data[2] = 0;  // blockDev->isWritable() ? 0x00 : 0x80;
        data[3] = 0;
        mode_sense_4(data+4);
        mode_sense_0(data+28);
        send_reply(data,44);
        break;
      default:
        set_error(ERROR_INVARG,0);
      }
      return;
    }
    else if (cmd==0x25) {
      // read capacity
      unsigned int lba = img.sectors-1;
      uint8_t data[8] = {
        lba>>24, lba>>16, lba>>8, lba,  // logical block address
        0, 0, 2, 0,                     // block size = 512 bytes
      };
      send_reply(data,8);
      return;
    }

  }
  // send immediate response for non-DMA commands
  *acsireg = STATUS_OK;
}
