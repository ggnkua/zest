/*
 * floppy_img.c - floppy disk image file management
 *
 * Copyright (c) 2022,2023 Francois Galea <fgalea at free.fr>
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

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "floppy_img.h"

static const uint8_t *findam(const uint8_t *p, const uint8_t *buf_end) {
  static const uint8_t head[] = {0,0,0,0xa1,0xa1,0xa1};
  buf_end -= sizeof(head);
  while (p<buf_end) {
    if (memcmp(p,head,sizeof(head))==0) {
      return p;
    }
    p++;
  }
  return NULL;
}

static const uint8_t *find_sector(const uint8_t *p, int track, int side, int sector) {
  const uint8_t *p_end = p + 6250;
  int ok = 0;
  while (!ok) {
    p = findam(p,p_end);
    if (p==NULL || p[6]!=0xfe || p[7]!=track || p[8]!=side) {
      printf("wrong ID address mark\n");
      ok = 2;
      break;
    }
    else {
      ok = p[9]==sector?1:0;
    }

    p += 11;
    p = findam(p,p_end);
    if (p==NULL || p[6]!=0xfb) {
      printf("wrong data address mark\n");
      ok = 2;
      break;
    }
    if (!ok) p += 521;
  }
  if (ok==1) {
    p += 7;
    return p;
  }
  return NULL;
}

static uint16_t crc16_table[256];

static void crc16_init(void) {
  unsigned int i,j;
  for (i=0; i<256; ++i) {
     unsigned int w = i<<8;
     for (j=0; j<8; ++j)
       w = (w<<1) ^ (0x1021&-((w>>15)&1));
     crc16_table[i] = w;
  }
}

static unsigned int crc16(const void *buf,size_t size) {
  const uint8_t *p = buf;
  unsigned int crc = 0xcdb4;
  while (size-->0)
    crc = crc16_table[crc>>8^*p++]^(crc&0xff)<<8;
  return crc;
}

static unsigned int readw(const unsigned char *ptr) {
  unsigned int v;
  v = ((unsigned int)ptr[1])<<8;
  v |= ((unsigned int)ptr[0]);
  return v;
}

static unsigned int readwb(const unsigned char *ptr) {
  unsigned int v;
  v = ((unsigned int)ptr[0])<<8;
  v |= ((unsigned int)ptr[1]);
  return v;
}

static void writewb(unsigned char *ptr, uint16_t v) {
  ptr[0] = v>>8;
  ptr[1] = v;
}

static void load_mfm(Flopimg *img) {
  int size = read(img->fd,img->buf,6250*2*MAXTRACK);
  int sectors = 0;

  if (size > 0) {
    const uint8_t *p = img->buf;
    p = find_sector(p,0,0,1);
    if (p) {
      sectors = readw(p+0x18);
      img->nsides = readw(p+0x1a);
      if (sectors<9 || sectors>11 || img->nsides<1 || img->nsides>2) {
        p = NULL;
      } else {
        img->ntracks = readw(p+0x13)/(sectors*img->nsides);
      }
    }
    if (p==NULL) {
      if (size>6250*100) {
        img->nsides = 2;
        img->ntracks = size/(6250*2);
      } else {
        img->nsides = 1;
        img->ntracks = size/6250;
      }
    }
  }
}

static void save_mfm(Flopimg *img) {
  lseek(img->fd,0,SEEK_SET);
  write(img->fd,img->buf,6250*img->nsides*img->ntracks);
}

static int guess_size(Flopimg *img) {
  if (img->image_size % 512) {
    return 0;
  }
  int tracks, sectors;
  for (tracks = MAXTRACK;tracks>0;tracks--) {
    for (sectors = 11; sectors >= 9; sectors--) {
      if (!(img->image_size % tracks)) {
        if ((img->image_size % (tracks*sectors*2*512))==0) {
          img->ntracks = tracks;
          img->nsides = 2;
          img->nsectors = sectors;
          printf("Geometry guessed: %d tracks, %d sides, %d sectors\n", tracks, 2, sectors);
          return 1;
        }
        else if ((img->image_size % (tracks*sectors*1*512))==0) {
          img->ntracks = tracks;
          img->nsides = 1;
          img->nsectors = sectors;
          printf("Geometry guessed: %d tracks, %d sides, %d sectors\n", tracks, 2, sectors);
          return 1;
        }
      }
    }
  }
  printf("Failed to guess disk geometry\n");
  return 0;
}

static void load_st_msa(Flopimg *img, int skew, int interleave) {
  unsigned int bps;
  int track,side;
  uint8_t buf[512*11];

  crc16_init();

  if (img->format==1)
  {
    // ST file format
    img->image_size = lseek(img->fd, 0, SEEK_END);
    lseek(img->fd, 0, SEEK_SET);
    read(img->fd,buf,32);
    lseek(img->fd, 0, SEEK_SET);

    img->nsectors = readw(buf+0x18);
    img->nsides = readw(buf+0x1a);
    img->ntracks = readw(buf+0x13)/(img->nsectors*img->nsides);
    printf("tracks:%u sides:%u sectors:%u\n",img->ntracks,img->nsides,img->nsectors);

    bps = readw(buf+0x0b);
    if (bps!=512) {
      printf("invalid sector size:%u\n",bps);
      if (!guess_size(img)) {
        return;
      }
    }

    if (img->nsectors<9 || img->nsectors>11) {
      printf("unsupported number of sectors per track:%u\n",img->nsectors);
      if (!guess_size(img)) {
        return;
      }
    }

    if (img->ntracks > MAXTRACK || img->ntracks < 0) {
      printf("unsupported number of tracks:%u\n", img->ntracks);
      if (!guess_size(img)) {
        return;
      }
    }
  } else {
    // MSA image file format
    img->image_size = lseek(img->fd, 0, SEEK_END);
    lseek(img->fd, 0, SEEK_SET);
    read(img->fd,buf,10);
    if (readwb(buf)!=0x0e0f) {
      printf("Error: not a valid .MSA file\n");
      return;
    }
    img->nsectors = readwb(buf+2);
    img->nsides = readwb(buf+4)+1;
    unsigned short start_track = readwb(buf+6);
    if (start_track != 0) {
      printf("Partial .msa file supplied. It starts at track %d. This is currently not supported\n", start_track + 1);
      return;
    }
    img->ntracks = readwb(buf+8)+1;
    printf("tracks:%u sides:%u sectors:%u\n",img->ntracks,img->nsides,img->nsectors);
  }

  int gap1,gap2,gap4,gap5;
  if (img->nsectors==11) {
    gap1 = 10;
    gap2 = 3;
    gap4 = 1;
    gap5 = 14;
  } else {
    gap1 = 60;
    gap2 = 12;
    gap4 = 40;
    if (img->nsectors==10) {
      gap5 = 50;
    } else {
      gap5 = 664;
    }
  }

  crc16_init();

  int sec_shift = 1;
  if (interleave==0) interleave = 1;
  if (interleave==1 && img->nsectors==11) interleave = 2;
  for (track=0; track<img->ntracks; ++track) {
    // compute order of sectors depending on skew and interleave
    int i;
    unsigned int written = 0;
    int sec_no = sec_shift;
    unsigned char order[img->nsectors];
    memset(order,0,img->nsectors);
    for (i=0; i<img->nsectors; ++i) {
      order[sec_no] = i;
      written |= 1<<sec_no;
      sec_no += interleave;
      if (sec_no>=img->nsectors) sec_no -= img->nsectors;
      if (i+1<img->nsectors) {
        while ((written&1<<sec_no) != 0) {
          sec_no = (sec_no+1<img->nsectors) ? sec_no+1 : 0;
        }
      }
    }
    sec_shift -= img->nsectors-skew;
    if (sec_shift<0) sec_shift += img->nsectors;

    for (side=0; side<img->nsides; ++side) {
      int sector;
      uint8_t *p0 = flopimg_trackpos(img,track,side);
      uint8_t *p = p0;
      unsigned int crc;
      if (img->format==1) {
        read(img->fd,buf,512*img->nsectors);
      } else {
        // decode next MSA track
        unsigned int tracksize = 512*img->nsectors;
        read(img->fd,buf,2);
        unsigned int datalen = readwb(buf);
        if (datalen==tracksize) {
          // uncompressed track
          read(img->fd,buf,tracksize);
        } else {
          // compressed track
          uint8_t msa_buf[512*11];
          uint8_t *src = msa_buf;
          uint8_t *dest = buf;
          read(img->fd,msa_buf,datalen);
          while ((dest-buf)<tracksize) {
            uint8_t b = *src++;
            if (b==0xe5) {
              b = *src++;
              unsigned int length = readwb(src);
              src += 2;
              unsigned int i;
              for (i=0; i<length; ++i) {
                *dest++ = b;
              }
            } else {
              *dest++ = b;
            }
          }
        }
      }
      for (i=0; i<gap1; ++i) *p++ = 0x4E;

      for (sector=0; sector<img->nsectors; ++sector) {
        sec_no = order[sector];
        for (i=0; i<gap2; ++i) *p++ = 0x00;
        for (i=0; i<3; ++i) *p++ = 0xA1;
        *p++ = 0xFE;
        *p++ = track;
        *p++ = side;
        *p++ = sec_no+1;
        *p++ = 2;
        crc = crc16(p-5,5);
        *p++ = crc>>8;
        *p++ = crc;
        for (i=0; i<22; ++i) *p++ = 0x4E;
        for (i=0; i<12; ++i) *p++ = 0x00;
        for (i=0; i<3; ++i) *p++ = 0xA1;
        *p++ = 0xFB;
        memcpy(p,buf+512*sec_no,512);
        p += 512;
        crc = crc16(p-513,513);
        *p++ = crc>>8;
        *p++ = crc;
        for (i=0; i<gap4; ++i) *p++ = 0x4E;
      }
      for (i=0; i<gap5; ++i) *p++ = 0x4E;
      if (p-p0 != 6250) {
        printf("format error\n");
      }
    }
  }

}

static void save_st(Flopimg *img) {
  int track;
  lseek(img->fd,0,SEEK_SET);

  // Read sectors, tracks, sides from the buffer, in case the disk has been reformatted
  const uint8_t *p = find_sector(img->buf,0,0,1);
  int sectors = readw(p+0x18);
  int nsides = readw(p+0x1a);
  int ntracks = readw(p+0x13)/(sectors*nsides);

  for (track=0; track<ntracks; ++track) {
    int side;
    for (side=0; side<nsides; ++side) {
      int sector;
      p = flopimg_trackpos(img,track,side);
      for (sector=0; sector<sectors; ++sector) {
        const uint8_t *sp = find_sector(p,track,side,sector+1);
        if (sp==NULL) {
          printf("sector not found\n");
          return;
        }
        write(img->fd,sp,512);
      }
    }
  }
}

// try to pack a chunk of data in MSA RLE format
// returns packed size or -1 if packing was unsuccessful
static int msa_pack(uint8_t *dest, const uint8_t *src, int len) {
  int pklen = 0;
  const uint8_t *p = src, *src_end = src+len;

  while (p<src_end) {
    const uint8_t *prev = p;
    unsigned int pkv = *p++;
    while (p<src_end && *p==pkv) p++;
    int n = p-prev;
    if ((n>4||pkv==0xE5) && pklen+4<len) {
      *dest++ = 0xE5;
      *dest++ = pkv;
      *dest++ = n>>8;
      *dest++ = n;
      pklen += 4;
    } else if (pklen+n<len) {
      int i;
      for (i=0;i<n;++i) *dest++ = pkv;
      pklen += n;
    } else {
      return -1;
    }
  }
  return pklen;
}

static void save_msa(Flopimg *img) {
  lseek(img->fd,0,SEEK_SET);

  // Read sectors, tracks, sides from the buffer, in case the disk has been reformatted
  const uint8_t *p = find_sector(img->buf,0,0,1);
  int sectors = readw(p+0x18);
  int nsides = readw(p+0x1a);
  int ntracks = readw(p+0x13)/(sectors*nsides);

  // write MSA header
  uint8_t header[10];
  memcpy(header+0,"\x0e\x0f",2);
  writewb(header+2,sectors);
  writewb(header+4,nsides-1);
  writewb(header+6,0);
  writewb(header+8,ntracks-1);
  write(img->fd,header,10);
  off_t length = 10;

  int track;
  for (track=0; track<ntracks; ++track) {
    int side;
    for (side=0; side<nsides; ++side) {
      int sector;
      uint8_t trbuf[11*512];
      p = flopimg_trackpos(img,track,side);
      // copy the sectors in order
      for (sector=0; sector<sectors; ++sector) {
        const uint8_t *sp = find_sector(p,track,side,sector+1);
        if (sp==NULL) {
          printf("sector not found\n");
          return;
        }
        memcpy(trbuf+sector*512,sp,512);
      }
      // try to compress the track
      uint8_t pkbuf[2+11*512];
      int pklen = msa_pack(pkbuf+2,trbuf,sectors*512);
      if (pklen<0) {
        // compression failed, writing uncompressed
        writewb(pkbuf,sectors*512);
        write(img->fd,pkbuf,2);
        write(img->fd,trbuf,sectors*512);
        length += 2+sectors*512;
      } else {
        // write the compressed data
        writewb(pkbuf,pklen);
        write(img->fd,pkbuf,2+pklen);
        length += 2+pklen;
      }
    }
  }

  ftruncate(img->fd,length);
}

Flopimg * flopimg_open(const char *filename, int rdonly, int skew, int interleave) {
  int format = -1;
  char *rpp = strrchr(filename,'.');
  if (rpp && strcasecmp(rpp,".mfm")==0) {
    format = 0;
  } else if (rpp && strcasecmp(rpp,".st")==0) {
    format = 1;
  } else if (rpp && strcasecmp(rpp,".msa")==0) {
    format = 2;
  } else {
    printf("Could not determine the floppy image file format\n");
    return NULL;
  }
  Flopimg *img = malloc(sizeof(Flopimg));
  if (img==NULL) return NULL;
  memset(img,0,sizeof(Flopimg));

  img->fd = open(filename,rdonly?O_RDONLY:O_RDWR);
  if (img->fd == -1) {
    printf("Could not open floppy image file `%s`\n",filename);
    return NULL;
  }

  img->format = format;
  img->rdonly = rdonly;

  if (format==0) {
    load_mfm(img);
  } else if (format==1 || format==2) {
    load_st_msa(img,skew,interleave);
  }

  return img;
}

void flopimg_writeback(Flopimg *img) {
  img->wrb = 1;
}

uint8_t * flopimg_trackpos(Flopimg *img, int track, int side) {
  if (track>=img->ntracks) {
    img->ntracks = track+1;
  }
  if (side>=img->nsides) {
    img->nsides = side+1;
  }
  return img->buf+(track*img->nsides+side)*6250;
}

void flopimg_sync(Flopimg *img) {
  if (img->wrb) {
    if (img->format==0) {
      save_mfm(img);
    } else if (img->format==1) {
      save_st(img);
    } else if (img->format==2) {
      save_msa(img);
    }
    img->wrb = 0;
  }
}

void flopimg_close(Flopimg *img) {
  flopimg_sync(img);
  close(img->fd);
  free(img);
}
