/*
 * midi.c - MIDI I/O management
 *
 * Copyright (c) 2025 Francois Galea <fgalea at free.fr>
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include "midi.h"
#include "config.h"

extern volatile uint32_t *parmreg;
extern volatile int thr_end;

static int midi_in_fd = -1;
static int midi_out_fd = -1;

// called from floppy interrupt manager if the midi flag is on
// If a character is available from the ACIA, send it to MIDI
void midi_interrupt(void) {
  unsigned int st = parmreg[12];
  int txd_full = st&0x200;
  if (txd_full) {
    unsigned char v = st&0xff;
    // printf("recv 0x%02x\n",(int)v);
    if (midi_out_fd!=-1)
      write(midi_out_fd,&v,1);
  }
}

// send a character to the MIDI ACIA
void midi_send(int c) {
  unsigned int st;
  int txd_full;
  int rxd_full;
  do {
    st = parmreg[12];
    txd_full = st&0x200;
    rxd_full = st&0x100;
    if (txd_full) {
      unsigned char v = st&0xff;
      // printf("recv 0x%02x\n",(int)v);
      if (midi_out_fd!=-1)
        write(midi_out_fd,&v,1);
    }
  } while (rxd_full);
  parmreg[12] = c;
}

static struct pollfd pfd = { .fd=-1, .events=POLLIN };

void midi_update_ports(void) {
  char buf[256];
  if (midi_in_fd!=-1 && midi_in_fd==midi_out_fd) {
    // both MIDI ports go to the same interface
    close(midi_in_fd);
    midi_in_fd = -1;
    midi_out_fd = -1;
  }
  if (midi_in_fd!=-1) {
    close(midi_in_fd);
    midi_in_fd = -1;
  }
  if (midi_out_fd!=-1) {
    close(midi_out_fd);
    midi_out_fd = -1;
  }
  if (config.midi_in&&config.midi_out&&!strcmp(config.midi_in,config.midi_out)) {
    // both MIDI ports go to the same interface
    snprintf(buf,sizeof(buf),"/dev/snd/%s",config.midi_in);
    midi_in_fd = open(buf,O_RDWR);
    midi_out_fd = midi_in_fd;
    pfd.fd = midi_in_fd;
  } else {
    if (config.midi_in) {
      snprintf(buf,sizeof(buf),"/dev/snd/%s",config.midi_in);
      midi_in_fd = open(buf,O_RDONLY);
      pfd.fd = midi_in_fd;
    }
    if (config.midi_out) {
      snprintf(buf,sizeof(buf),"/dev/snd/%s",config.midi_out);
      midi_out_fd = open(buf,O_WRONLY);
    }
  }
}

void * thread_midi(void * arg) {
  unsigned char buf[1024];

  midi_update_ports();

  for (;;) {
    int status = poll(&pfd,1,5);
    if (thr_end) break;
    if (status==-1) {
      perror("MIDI interface");
      break;
    } else if (status==0) {
      continue;
    }
    int n = read(midi_in_fd,buf,sizeof(buf));
    int i;
    for (i=0;i<n;++i) {
      // printf("send 0x%02x\n",(int)buf[i]);
      midi_send(buf[i]);
    }
  }

  if (midi_in_fd!=-1)
    close(midi_in_fd);
  if (midi_out_fd!=-1)
    close(midi_out_fd);
  return NULL;
}
