/*
 * acsi.c - ACSI interface and hard disk drive emulation (software part)
 *
 * Copyright (c) 2023-2026 Francois Galea <fgalea at free.fr>
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

#include "acsi.h"
#include "gemdos.h"
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

volatile uint32_t *acsireg;
volatile uint32_t *iobuf;

static struct __acsi_disk {
  int fd;           // disk image file handle
  int sectors;      // number of sectors
  unsigned int lba; // current logical block address
  unsigned int sense;
  int report_lba;   // report LBA in sense data
} acsi_disk[8];

static int gemdos_id;

static void clear_sense_data(int acsi_id) {
  acsi_disk[acsi_id].sense = 0;
  acsi_disk[acsi_id].report_lba = 0;
}

static void update_gemdos_id(void) {
  int i;
  gemdos_id = -1;
  for (i=0;i<8;++i) {
    if (acsi_disk[i].fd == -1) {
      gemdos_id = i;
      break;
    }
  }
}

static void openimg(int acsi_id, const char *filename) {
  if (filename) {
    acsi_disk[acsi_id].fd = open(filename,O_RDWR);
    if (acsi_disk[acsi_id].fd==-1) {
      printf("could not open HDD image file `%s`\n",filename);
      return;
    }
    off_t size = lseek(acsi_disk[acsi_id].fd,0,SEEK_END);
    acsi_disk[acsi_id].sectors = size/512;
    lseek(acsi_disk[acsi_id].fd,0,SEEK_SET);
    update_gemdos_id();
  }
}

static void closeimg(int acsi_id) {
  if (acsi_disk[acsi_id].fd!=-1) {
    close(acsi_disk[acsi_id].fd);
  }
  acsi_disk[acsi_id].fd = -1;
  update_gemdos_id();
}

void hdd_changeimg(int acsi_id, const char *full_pathname) {
  closeimg(acsi_id);
  openimg(acsi_id, full_pathname);
}

void acsi_init(volatile uint32_t *parmreg) {
  int i;
  acsireg = (void*)(((uint8_t*)parmreg)+0x4000);
  iobuf = acsireg + (0x800/4);
  for (i=0;i<8;++i) {
    acsi_disk[i].fd = -1;
    openimg(i,config.acsi[i]);
    clear_sense_data(i);
  }
  update_gemdos_id();
  gemdos_init();
}

void acsi_exit(void) {
  int i;
  gemdos_exit();
  for (i=0;i<8;++i) {
    closeimg(i);
  }
}

uint8_t acsi_command[10];
static int dev_id = 0;
static int cmd_ext = 0;
static int cmd_size = 0;
static int cmd_rd_idx = 0;
static int dma_mode = 0;    // 0:idle 1:read 2:write
static int dma_buf_id = 0;
static int dma_rem_bs = 0;  // remaining 16-byte blocks
static uint8_t *dma_gemdos_ptr = NULL;

static void set_error(unsigned int err, int report_lba) {
  acsi_disk[dev_id].sense = err;
  acsi_disk[dev_id].report_lba = report_lba;
  *acsireg = STATUS_ERROR;
}

static void read_next(void) {
  if (dma_rem_bs==0) {
    // finish command
    *acsireg = STATUS_OK;
    dma_mode = 0;
  } else {
    // initiate DMA read
    ++acsi_disk[dev_id].lba;
    int nbs = dma_rem_bs<32?dma_rem_bs:32;
    *acsireg = 0x100 | (nbs-1)<<3 | dma_buf_id;
    dma_rem_bs -= nbs;
    if (dma_rem_bs>0) {
      dma_buf_id ^= 1;
      int offset = dma_buf_id*512;
      nbs = dma_rem_bs<32?dma_rem_bs:32;
      if (dev_id==gemdos_id) {
        memcpy(((char*)iobuf)+offset,dma_gemdos_ptr,nbs*16);
        dma_gemdos_ptr += 512;
      } else {
        read(acsi_disk[dev_id].fd,((char*)iobuf)+offset,512);
      }
    }
  }
}

static void write_next(void) {
  // initiate next DMA write
  int nbs = dma_rem_bs<32?dma_rem_bs:32;
  dma_rem_bs -= nbs;
  if (dma_rem_bs>0) {
    int nbs = dma_rem_bs<32?dma_rem_bs:32;
    *acsireg = 0x200 | (nbs-1)<<3 | (1-dma_buf_id);
  }
  if (dev_id==gemdos_id) {
    if (acsi_command[0]==0x11) {
      if (dma_gemdos_ptr) {
        memcpy(dma_gemdos_ptr,((char*)iobuf)+dma_buf_id*512,nbs*16);
        dma_gemdos_ptr += nbs*16;
      }
      if (dma_rem_bs==0) {
        // finish command
        dma_mode = 0;
        gemdos_stub_call();
      }
    }
  } else {
    ++acsi_disk[dev_id].lba;
    write(acsi_disk[dev_id].fd,((char*)iobuf)+dma_buf_id*512,512);
    if (dma_rem_bs==0) {
      // finish command
      *acsireg = STATUS_OK;
      dma_mode = 0;
    }
  }
  dma_buf_id ^= 1;
}

// initiate an ACSI DMA transfer from ST to host (DMA write)
void acsi_wait_data(void *data, int n_bytes) {
  dma_mode = 2;
  dma_buf_id = 0;
  dma_rem_bs = (n_bytes+15)/16;
  dma_gemdos_ptr = (uint8_t*)data;
  int nbs = dma_rem_bs<32?dma_rem_bs:32;
  *acsireg = 0x200 | (nbs-1)<<3 | dma_buf_id;
}

// initiate an ACSI DMA transfer from host to ST (DMA read)
void acsi_send_reply(const void *data, int size) {
  dma_mode = 1;
  dma_buf_id = 0;
  dma_rem_bs = (size+15)/16;
  dma_gemdos_ptr = ((uint8_t*)data)+512;
  memcpy((void*)iobuf,data,size<512?size:512);
  read_next();
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

  uint32_t blocks = acsi_disk[dev_id].sectors;
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

  uint32_t blocks = acsi_disk[dev_id].sectors;
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

void acsi_interrupt(void) {
  if (dma_mode==1) {
    // a DMA read command is running
    read_next();
    return;
  }
  if (dma_mode==2) {
    // a DMA write command is running
    write_next();
    return;
  }

  // Not a DMA interrupt: command byte reception
  unsigned int reg = *acsireg;

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
      // ignore command if no image is set up for the device ID
      if (acsi_disk[dev_id].fd==-1 && (dev_id!=gemdos_id || config.gemdos==NULL)) return;
      cmd = d&0x1f;
      if (cmd==0x1f) {
        // ICD command extension
        cmd_ext = 1;
        *acsireg = STATUS_OK;
        return;
      }
    }
    if (dev_id==gemdos_id) {
      if (cmd!=0 && cmd!=3 && cmd!=8 && cmd!=0x11 && cmd!=0x12) {
        set_error(ERROR_OPCODE,0);
        return;
      }
    } else {
      if (cmd!=0 && cmd!=3 && cmd!=8 && cmd!=0x0a && cmd!=0x12 && cmd!=0x1a && cmd!=0x25) {
        set_error(ERROR_OPCODE,0);
        return;
      }
    }
    cmd_size = command_size(cmd);
    acsi_command[cmd_rd_idx++] = cmd;
  } else {
    acsi_command[cmd_rd_idx++] = d;
  }

  struct __acsi_disk *img = &acsi_disk[dev_id];
  if (cmd_rd_idx==cmd_size) {
    cmd_rd_idx = 0;
    cmd_ext = 0;
    int cmd = acsi_command[0];
    if (dev_id==gemdos_id) {
      gemdos_acsi_cmd();
      return;
    }
    else if (cmd==0) {
      // send response, no error
      *acsireg = STATUS_OK;
      return;
    }
    else if (cmd==3) {
      // request sense
      uint8_t data[256];
      unsigned int length = acsi_command[4];
      if (length<4) {
        length = 4;
      }
      memset(data,0,length);
      if (length<=4) {
        data[0] = (img->sense>>16)&0xff;   // additional sense code
        if (img->report_lba) {
          data[0] |= 0x80;
          data[1] = (img->lba>>16)&0xff;
          data[2] = (img->lba>>8)&0xff;
          data[3] = img->lba&0xff;
        }
      } else {
        data[0] = 0x70;
        if (img->report_lba) {
          data[0] |= 0x80;
          data[3] = (img->lba>>24)&0xff;
          data[4] = (img->lba>>16)&0xff;
          data[5] = (img->lba>>8)&0xff;
          data[6] = img->lba&0xff;
        }
        data[2] = img->sense&0x0f;         // sense key
        data[7] = 10;   // additional sense length
        data[12] = (img->sense>>16)&0xff;  // additional sense code
        data[13] = (img->sense>>8)&0xff;   // additional sense code qualifier
      }
      acsi_send_reply(data,length);
      clear_sense_data(dev_id);
      return;
    }
    else if (cmd==8) {
      // read
      img->lba = (acsi_command[1]<<8|acsi_command[2])<<8|acsi_command[3];
      dma_rem_bs = acsi_command[4]*32;
      if (img->lba >= img->sectors) {
        set_error(ERROR_INVADDR,1);
        return;
      }
      if (img->lba + acsi_command[4] > img->sectors) {
        img->lba = img->sectors;
        set_error(ERROR_INVADDR,1);
        return;
      }
      dma_mode = 1;
      dma_buf_id = 0;
      lseek(img->fd,img->lba*512,SEEK_SET);
      read(img->fd,(void*)iobuf,512);
      read_next();
      return;
    }
    else if (cmd==0x0a) {
      // write
      int sector = (acsi_command[1]<<8|acsi_command[2])<<8|acsi_command[3];
      if (img->lba >= img->sectors) {
        img->lba = sector;
        set_error(ERROR_INVADDR,1);
        return;
      }
      if (sector + acsi_command[4] > img->sectors) {
        img->lba = img->sectors;
        set_error(ERROR_INVADDR,1);
        return;
      }
      lseek(img->fd,sector*512,SEEK_SET);
      acsi_wait_data(NULL,acsi_command[4]*512);
      return;
    }
    else if (cmd==0x12) {
      // inquiry
      static const uint8_t data[48] =
        "\x00\x00\x01\x00\x1f\x00\x00\x00"
        "zeST    "
        "EmulatedHarddisk"
        "0100" "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
      int alloc = acsi_command[3]<<8 | acsi_command[4];
      if (alloc>48) alloc = 48;
      acsi_send_reply(data,alloc);
      return;
    }
    else if (cmd==0x1a) {
      // mode sense
      // Borrowed from acsi2stm who borrowed from Hatari
      uint8_t data[48];
      switch (acsi_command[2]) {
      case 0:
        mode_sense_0(data);
        acsi_send_reply(data,16);
        break;
      case 4:
        mode_sense_4(data);
        acsi_send_reply(data,24);
        break;
      case 0x3f:
        data[0] = 43;
        data[1] = 0;
        data[2] = 0;  // blockDev->isWritable() ? 0x00 : 0x80;
        data[3] = 0;
        mode_sense_4(data+4);
        mode_sense_0(data+28);
        acsi_send_reply(data,44);
        break;
      default:
        set_error(ERROR_INVARG,0);
      }
      return;
    }
    else if (cmd==0x25) {
      // read capacity
      unsigned int lba = img->sectors-1;
      uint8_t data[8] = {
        lba>>24, lba>>16, lba>>8, lba,  // logical block address
        0, 0, 2, 0,                     // block size = 512 bytes
      };
      acsi_send_reply(data,8);
      return;
    }

  }
  // send immediate response for non-DMA commands
  *acsireg = STATUS_OK;
}
