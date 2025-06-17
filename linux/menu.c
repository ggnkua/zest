/*
 * menu.c - Setup menu
 *
 * Copyright (c) 2022-2025 Francois Galea <fgalea at free.fr>
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
#include "menu.h"
#include "listview.h"
#include "misc.h"
#include "config.h"
#include "setup.h"
#include "floppy.h"
#include "hdd.h"
#include "infomsg.h"
#include "midi.h"

#define WIDTH 192
#define HEIGHT 150
#define XPOS_RGB 240
#define YPOS_RGB 126
#define XPOS_MONO 128
#define YPOS_MONO 50
#define XPOS (config.mono?XPOS_MONO:XPOS_RGB)
#define YPOS (config.mono?YPOS_MONO:YPOS_RGB)

static const uint32_t menu_palette[] = {0x000040,0xc0c000,0xc0c000,0x000040};

void menu_init(const char *font_file_name) {
  lv_init(font_file_name);
}

int filter_flopimg(const struct dirent *e) {
  if (e->d_type==DT_DIR) {
    return strcmp(e->d_name,".")&&strcmp(e->d_name,"..");
  }
  char *ext = strrchr(e->d_name,'.');
  if (!ext) return 0;
  return !strcasecmp(ext,".msa") || !strcasecmp(ext,".st") || !strcasecmp(ext,".mfm");
}

static int filter_img(const struct dirent *e) {
  if (e->d_type==DT_DIR) {
    return strcmp(e->d_name,".")&&strcmp(e->d_name,"..");
  }
  char *ext = strrchr(e->d_name,'.');
  if (!ext) return 0;
  return !strcasecmp(ext,".img");
}

static int filter_directory(const struct dirent *e) {
  if (e->d_type==DT_DIR) {
    return strcmp(e->d_name,".")&&strcmp(e->d_name,"..");
  }
  return 0;
}

static int settings(void) {
  char tmp_rom[1024] = {0};
  if (config.rom_file) strcpy(tmp_rom,config.rom_file);
  char tmp_hdd[1024] = {0};
  if (config.hdd_image) strcpy(tmp_hdd,config.hdd_image);
  int mem_size = config.mem_size;
  int mono = config.mono;
  int hdd_set = config.hdd_image!=NULL;

  int quit = 0;
  int selected = 0;
  int ret = 0;
  const char *midi_in = config.midi_in;
  const char *midi_out = config.midi_out;
  while (!quit) {
    ListView *lv = lv_new(XPOS,YPOS,WIDTH,HEIGHT,"zeST settings",menu_palette);
    int entry_height = lv_entry_height();
    uint32_t gradient_header[entry_height];
    gradient(gradient_header,entry_height/2,0x00ff0000,0xffc000);
    gradient(gradient_header+entry_height/2,entry_height-entry_height/2,0xffc000,0xff0000);
    int i;
    for (i=0;i<entry_height;++i) {
      lv_set_colour_change(lv,i,1,gradient_header[i]);
    }
    lv_set_colour_change(lv,entry_height,1,menu_palette[1]);
    lv_add_choice(lv,"Monitor type",&config.mono,2,"PAL/NTSC","Monochrome");
    lv_add_choice(lv,"RAM size",&config.mem_size,8,"256K","512K","1M","2M","2.5M","4M","8M","14M");
    int e_turbo = lv_add_choice(lv,"Turbo mode",&config.turbo,2,"off","on");
    lv_add_choice(lv,"Enable floppy A",&config.floppy_a_enable,2,"no","yes");
    lv_add_choice(lv,"Write protect floppy A",&config.floppy_a_write_protect,2,"no","yes");
    lv_add_choice(lv,"Enable floppy B",&config.floppy_b_enable,2,"no","yes");
    lv_add_choice(lv,"Write protect floppy B",&config.floppy_b_write_protect,2,"no","yes");
    lv_add_file(lv,"Hard disk image",&config.hdd_image,LV_FILE_EJECTABLE,filter_img);
    lv_add_midi(lv,"MIDI in",&config.midi_in);
    lv_add_midi(lv,"MIDI out",&config.midi_out);
    lv_add_file(lv,"System ROM",&config.rom_file,0,filter_img);
    lv_add_choice(lv,"Extended video modes",&config.extended_video_modes,2,"no","yes");
    lv_add_choice(lv,"Scan doubler mode",&config.scan_doubler_mode,2,"VGA","CRT");
    lv_add_choice(lv,"Right Alt key",&config.right_alt_is_altgr,2,"Alternate","AltGr");
    int e_wakestate = lv_add_choice(lv,"Wakestate",&config.wakestate,4,"WS1","WS2","WS3","WS4");
    lv_add_choice(lv,"Shifter Wakestate",&config.shifter_wakestate,2,"SWS1","SWS2");
    lv_choice_set_dynamic(lv,e_turbo,1);
    lv_choice_set_dynamic(lv,e_wakestate,1);
    lv_select(lv,selected);

    for (;;) {
      selected = lv_run(lv);
      if (selected==e_turbo) {
        if (config.turbo && config.mem_size<5) {
          // in current version of zeST, only memory sizes of 2M, 4M and more are supported in turbo mode
          config.mem_size = config.mem_size<=3?3:5;
        }
        if (mem_size==config.mem_size && (mem_size==3||mem_size>=5)) {
          // turbo mode changed, and memory is linearly mapped (only 2M memory banks, or extended memory)
          // -> dynamically update the turbo mode
          setup_update();
        }
      }
      else if (selected==e_wakestate) {
        // dynamically update the wakestate
        setup_update();
      }
      else {
        quit = 1;
      }
      break;
    }
    lv_delete(lv);
  }

  if (midi_in!=config.midi_in||midi_out!=config.midi_out) {
    midi_update_ports();
  }

  if (strcmp(config.rom_file,tmp_rom)) {
    load_rom(config.rom_file);
    ret = 1;
  }
  hdd_changeimg(config.hdd_image);
  if (hdd_set!=(config.hdd_image!=NULL)) ret = 1;
  if (hdd_set&&strcmp(config.hdd_image,tmp_hdd)) ret = 1;

  if (config.turbo && config.mem_size<5 && config.mem_size!=3) {
    // if turbo mode was changed and memory size is not 2M or 4M+ (linear memory mapping)
    config.mem_size = config.mem_size<=3?3:5;
  }
  if (config.mem_size!=mem_size || config.mono!=mono) ret = 1;
  return ret;
}

static int tools(void) {
  int quit = 0;
  while (!quit) {
    ListView *lv = lv_new(XPOS,YPOS,WIDTH,HEIGHT,"zeST tools",menu_palette);
    int entry_height = lv_entry_height();
    uint32_t gradient_header[entry_height];
    gradient(gradient_header,entry_height/2,0x00ff0000,0xffc000);
    gradient(gradient_header+entry_height/2,entry_height-entry_height/2,0xffc000,0xff0000);
    int i;
    for (i=0;i<entry_height;++i) {
      lv_set_colour_change(lv,i,1,gradient_header[i]);
    }
    lv_set_colour_change(lv,entry_height,1,menu_palette[1]);

    int e_jbmode = lv_add_choice(lv,"Jukebox mode",&config.jukebox_enabled,2,"no","yes");
    if (config.jukebox_enabled) {
      lv_add_file(lv,"Jukebox directory",&config.jukebox_path,LV_FILE_DIRECTORY,filter_directory);
    }
    lv_choice_set_dynamic(lv,e_jbmode,1);
    int e = lv_run(lv);
    if (e==e_jbmode) {
      // do nothing, just have the menu refreshed
    } else {
      quit = 1;
    }
    lv_delete(lv);
  }

  return 0;
}

void menu(void) {
  int quit = 0;
  infomsg_pause(1);
  while (!quit) {
    ListView *lv = lv_new(XPOS,YPOS,WIDTH,HEIGHT,"zeST main menu",menu_palette);
    int entry_height = lv_entry_height();
    uint32_t gradient_header[entry_height];
    gradient(gradient_header,entry_height,0x10C0FF,0x14488C);
    int i;
    for (i=0;i<entry_height;++i) {
      lv_set_colour_change(lv,i,1,gradient_header[i]);
    }
    // restore initial palette colour after the header
    lv_set_colour_change(lv,entry_height,1,menu_palette[1]);

    int e_reset = lv_add_action(lv,"Reset (warm)");
    int e_coldreset = lv_add_action(lv,"Reset (cold)");
    if (config.floppy_a_enable) {
      lv_add_file(lv,"Floppy A",&config.floppy_a,LV_FILE_EJECTABLE,filter_flopimg);
    }
    if (config.floppy_b_enable) {
      lv_add_file(lv,"Floppy B",&config.floppy_b,LV_FILE_EJECTABLE,filter_flopimg);
    }
    int e_settings = lv_add_action(lv,"Settings");
    int e_tools = lv_add_action(lv,"Tools");
    int e_save_cfg = lv_add_action(lv,"Save config");
    //lv_add_action(lv,"Shutdown");

    int e = lv_run(lv);
    lv_delete(lv);
    if (e==-1) {
      quit = 1;
    }
    else if (e==e_reset) {
      warm_reset();
      quit = 1;
    }
    else if (e==e_coldreset) {
      cold_reset();
      quit = 1;
    }
    else if (e==e_settings) {
      if (settings()) {
        cold_reset();
        quit = 1;
      } else {
        setup_update();
      }
    }
    else if (e==e_tools) {
      tools();
    }
    else if (e==e_save_cfg) {
      config_save();
    }
  }
  if (config.floppy_a_enable) change_floppy(config.floppy_a,0);
  if (config.floppy_b_enable) change_floppy(config.floppy_b,1);
  infomsg_pause(0);
}
