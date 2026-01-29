/*
 * infomsg.c - Information message display on OSD
 *
 * Copyright (c) 2024-2026 Francois Galea <fgalea at free.fr>
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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <dirent.h>

#include "osd.h"
#include "setup.h"
#include "font.h"
#include "infomsg.h"
#include "misc.h"
#include "floppy.h"
#include "config.h"

/* from listview.c */
extern Font *lv_font;
extern int filter_flopimg(const struct dirent *e);
/* from infomsg.c */
extern uint64_t gettime(void);
/* from setup.c */
extern volatile int thr_end;


#define XPOS 40
#define YPOS 10
#define FLOPPY_STATUS_RASTER_COUNT 4

extern volatile int thr_end;

static int msg_on;
static int msg_pause;
static int floppy_status_on;
static uint64_t msg_timeout;

static void disable_floppy_status() {
  if (floppy_status_on) {
    infomsg_pause(1);
  }
  floppy_status_on = 0;
}

void switch_floppy_status() {
  floppy_status_on = !floppy_status_on;
  if (floppy_status_on) {
    msg_on = 0;
    static const uint32_t palette[] = { 0x000000,0xffffff,0x202020,0x80ff80 };
    osd_set_palette(palette);
    int height = font_get_height(lv_font);
    uint32_t changes[height];
    gradient(changes,height,0x09DE77,0x148C48);
    int i;
    for (i=0;i<height;++i) changes[i] = 1<<24 | changes[i];
    osd_set_palette_changes(changes,height);
    osd_set_size(FLOPPY_STATUS_RASTER_COUNT*16,height);
    osd_set_position(XPOS,YPOS);
    osd_show();
  } else {
    osd_hide();
  }
}

static void hide(void) {
  msg_on = 0;
  floppy_status_on = 0;
  osd_hide();
}

static void show(void) {
  osd_show();
  msg_on = 1;
  msg_timeout = 3+time(NULL);
}

void infomsg_pause(int pause) {
  msg_pause = pause;
  if (pause) {
    hide();
  }
}

void * thread_infomsg(void * arg) {
  int height = font_get_height(lv_font);
  while (thr_end==0) {
    usleep(50000);
    if (msg_on && time(NULL)>=msg_timeout) {
      hide();
    }
    if (floppy_status_on) {
      char msg[80];
      memset(osd_bitmap,0,FLOPPY_STATUS_RASTER_COUNT*height*sizeof(uint32_t));
      unsigned int r,w,track,side;
      get_floppy_status(&r,&w,&track,&side);
      sprintf(msg,"%c T:%u S:%u",w?'W':r?'R':'.',track,side);
      font_render_text(lv_font,osd_bitmap,FLOPPY_STATUS_RASTER_COUNT,2,height,FLOPPY_STATUS_RASTER_COUNT*16,0,msg);
    }
  }
  return NULL;
}

void infomsg_display(const char* msg) {
  static const uint32_t palette[] = { 0x000000,0xffffff,0x202020,0x80ff80 };
  osd_set_palette(palette);
  int width = font_text_width(lv_font,msg);
  int height = font_get_height(lv_font);
  uint32_t changes[height];
  gradient(changes,height,0xDE7709,0x8C4814);
  int i;
  for (i=0;i<height;++i) changes[i] = 1<<24 | changes[i];
  osd_set_palette_changes(changes,height);
  int raster_count = (width+15)/16;
  osd_set_size(raster_count*16,height);
  osd_set_position(XPOS,YPOS);
  memset(osd_bitmap,0,raster_count*height*sizeof(uint32_t));
  font_render_text(lv_font,osd_bitmap,raster_count,2,height,raster_count*16,0,msg);
  show();
}

static void show_volume(int vol) {
  char buf[40];
  int pc = vol*100/16;
  sprintf(buf,"Vol: %d%%",pc);
  infomsg_display(buf);
}

void vol_mute(void) {
  disable_floppy_status();
  int mute = !get_sound_mute();
  set_sound_mute(mute);
  if (mute) {
    infomsg_display("Sound off");
  } else {
    infomsg_display("Sound on");
  }
}

void vol_down(void) {
  disable_floppy_status();
  int vol = get_sound_vol()-1;
  if (vol>=0) {
    set_sound_vol(vol);
    show_volume(vol);
  }
}

void vol_up(void) {
  disable_floppy_status();
  int vol = get_sound_vol()+1;
  if (vol<32) {
    set_sound_vol(vol);
    show_volume(vol);
  }
}

// *Really* minimal PCG32 code / (c) 2014 M.E. O'Neill / pcg-random.org
// Licensed under Apache License 2.0 (NO WARRANTY, etc. see website)

typedef struct { uint64_t state;  uint64_t inc; } pcg32_random_t;

uint32_t pcg32_random_r(pcg32_random_t* rng)
{
  uint64_t oldstate = rng->state;
  // Advance internal state
  rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
  // Calculate output function (XSH RR), uses old state for max ILP
  uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
  uint32_t rot = oldstate >> 59u;
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

void pcg32_srandom_r(pcg32_random_t* rng, uint64_t initstate, uint64_t initseq)
{
  rng->state = 0U;
  rng->inc = (initseq << 1u) | 1u;
  pcg32_random_r(rng);
  rng->state += initstate;
  pcg32_random_r(rng);
}

pcg32_random_t random_for_disk_selection;

void init_pcg(void)
{
  pcg32_srandom_r(&random_for_disk_selection, time(NULL), (intptr_t)&random_for_disk_selection);
}

int jukebox_trigger_next_image=0;
int jukebox_current_alphabetical_image=0;

void * thread_jukebox(void * arg) {
  uint64_t jukebox_timeout = 0;
  while (thr_end == 0) {
    uint64_t current_time = time(NULL);
    usleep(1000);
    if (msg_pause==0 && config.jukebox_enabled && config.jukebox_path /*&& !file_selector_running*/) {
      if (current_time >= jukebox_timeout||jukebox_trigger_next_image)
      {
        jukebox_trigger_next_image=0;
        // Read directory
        struct dirent **namelist;
        int n = scandir(config.jukebox_path,&namelist,&filter_flopimg,alphasort);
        if (n<=0)
        {
          //infomsg_display("Error while reading jukebox directory. Jukebox off.");
          jukebox_timeout = current_time+1;
        } else {
          // Select image
          if (!random_for_disk_selection.state)
          {
            init_pcg();
          }
          int selected_image;
          do {
            if (config.jukebox_mode==0) {
              // Random
              selected_image = pcg32_random_r(&random_for_disk_selection) % n;
            } else {
              // Alphabetical, in order
              jukebox_current_alphabetical_image=(jukebox_current_alphabetical_image+1)%n;
              selected_image = jukebox_current_alphabetical_image;
            }
          } while (namelist[selected_image]->d_type==DT_DIR); // Avoid directories
          // Construct image filename
          char *selected_item = namelist[selected_image]->d_name;
          char new_disk_image_name[PATH_MAX];
          strcpy(new_disk_image_name, config.jukebox_path);
          strcat(new_disk_image_name, selected_item);
          printf("booting %s\n",new_disk_image_name);
          // Free item list
          int i;
          for (i=0; i<n; ++i) {
            free(namelist[i]);
          }
          free(namelist);
          // Boot the image and set timeout
          change_floppy(new_disk_image_name,0);
          //config.mem_size = selected_ram_size;
          cold_reset();
          jukebox_timeout = (uint64_t)config.jukebox_timeout_duration + time(NULL);
          infomsg_display(new_disk_image_name);
        }
      }
    }
  }
  return NULL;
}
