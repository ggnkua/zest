/*
 * config.c - zeST configuration
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

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include <ini.h>

#include "config.h"

static const char *config_file = NULL;
ZestConfig config;


// interpret string str as a boolean value
static int truefalse(const char *x) {
  if (strcasecmp(x,"true")==0) return 1;
  if (strcasecmp(x,"yes")==0) return 1;
  if (strcasecmp(x,"on")==0) return 1;
  if (strcmp(x,"1")==0) return 1;
  if (strcasecmp(x,"false")==0) return 0;
  if (strcasecmp(x,"no")==0) return 0;
  if (strcasecmp(x,"off")==0) return 0;
  if (strcmp(x,"0")==0) return 0;

  printf("could not interpret boolean value `%s`, returning false\n",x);
  return 0;
}

static const char *memsize_values[] = {"256K","512K","1M","2M","2.5M","4M","8M","14M"};
static const char *keymap_values[] = {"dk","nl","uk","us","fr","bepo","de","no","pl","es","se"};

// interpret string str as a memory size setting
static int list_id(const char *list[], int n, const char *x, int dflt) {
  int i;
  for (i=0;i<n;++i) {
    if (strcasecmp(x,list[i])==0)
      return i;
  }
  printf("invalid setting `%s`\n",x);
  return dflt;
}

// replace a string pointer with another, freeing the old one if necessary
static void set_str_var(const char **pv, const char *str) {
  if (*pv) {
    free((char*)*pv);
  }
  if (str) {
    *pv = strdup(str);
  } else {
    *pv = NULL;
  }
}

static int handler(void* user, const char* section, const char* name, const char* value) {
  ZestConfig* pconfig = user;

  if (strlen(value)==0) {
    // empty setting -> set NULL value
    value = NULL;
  }

  #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
  if (MATCH("main","mono")) {
    pconfig->mono = truefalse(value);
  } else if (MATCH("main","extended_video_modes")) {
    pconfig->extended_video_modes = truefalse(value);
  } else if (MATCH("main","turbo")) {
    pconfig->turbo = truefalse(value);
  } else if (MATCH("main","mem_size")) {
    pconfig->mem_size = list_id(memsize_values,sizeof(memsize_values)/sizeof(memsize_values[0]),value,CFG_1M);
  } else if (MATCH("main", "wakestate")) {
    int ws = atoi(value);
    if (ws<1 || ws>4) {
      printf("invalid wakestate value `%d`\n",ws);
    } else {
      pconfig->wakestate = ws-1;
    }
  } else if (MATCH("main", "shifter_wakestate")) {
    int ws = atoi(value);
    if (ws<0 || ws>1) {
      printf("invalid shifter wakestate value `%d`\n",ws);
    } else {
      pconfig->shifter_wakestate = ws;
    }
  } else if (MATCH("main", "scan_doubler_mode")) {
    int mode = atoi(value);
    if (mode<0 || mode>1) {
      printf("invalid scan doubler mode value `%d`\n",mode);
    } else {
      pconfig->scan_doubler_mode = mode;
    }
  } else if (MATCH("main","rom_file")) {
    set_str_var(&pconfig->rom_file,value);
  } else if (MATCH("main","timezone")) {
    int tz = atoi(value);
    if (tz<-12) tz = -12;
    if (tz>12) tz = 12;
    pconfig->timezone = tz+12;
  } else if (MATCH("main","keymap")) {
    pconfig->keymap_id = list_id(keymap_values,sizeof(keymap_values)/sizeof(keymap_values[0]),value,3);
  } else if (MATCH("floppy","floppy_a")) {
    set_str_var(&pconfig->floppy_a,value);
  } else if (MATCH("floppy","floppy_a_enable")) {
    if (value) pconfig->floppy_a_enable = truefalse(value);
  } else if (MATCH("floppy","floppy_a_write_protect")) {
    if (value) pconfig->floppy_a_write_protect = truefalse(value);
  } else if (MATCH("floppy","floppy_b")) {
    set_str_var(&pconfig->floppy_b,value);
  } else if (MATCH("floppy","floppy_b_enable")) {
    if (value) pconfig->floppy_b_enable = truefalse(value);
  } else if (MATCH("floppy","floppy_b_write_protect")) {
    if (value) pconfig->floppy_b_write_protect = truefalse(value);
  } else if (MATCH("hdd","image")) {
    set_str_var(&pconfig->acsi[0],value);
  } else if (!strcmp(section,"hdd") && !strncmp(name,"acsi",4)) {
    int id = atoi(name+4);
    if (id>=0 && id<=7) {
      set_str_var(&pconfig->acsi[id],value);
    }
  } else if (MATCH("hdd","gemdos")) {
    set_str_var(&pconfig->gemdos,value);
  } else if (MATCH("keyboard","right_alt_is_altgr")) {
    if (value) pconfig->right_alt_is_altgr = truefalse(value);
  } else if (MATCH("midi","in")) {
    set_str_var(&pconfig->midi_in,value);
  } else if (MATCH("midi","out")) {
    set_str_var(&pconfig->midi_out,value);
  } else if (MATCH("jukebox","enabled")) {
    if (value) pconfig->jukebox_enabled = truefalse(value);
  } else if (MATCH("jukebox","path")) {
    set_str_var(&pconfig->jukebox_path,value);
  } else if (MATCH("jukebox","timeout")) {
    int t = atoi(value);
    if (t < 1)
    {
      printf("Invalid jukebox timeout value '%d'\n", t);
    } else {
      pconfig->jukebox_timeout_duration = t;
    }
  }
  else {
    return 0;  /* unknown section/name, error */
  }
  return 1;
}

void config_init(void) {
  int i;
  config.mono = 0;
  config.extended_video_modes = 0;
  config.turbo = 0;
  config.mem_size = CFG_1M;
  config.wakestate = 2;
  config.shifter_wakestate = 0;
  config.scan_doubler_mode = 0;
  config.rom_file = NULL;
  config.timezone = 12;
  config.keymap_id = 3;
  config.floppy_a = NULL;
  config.floppy_a_enable = 1;
  config.floppy_a_write_protect = 0;
  config.floppy_b = NULL;
  config.floppy_b_enable = 0;
  config.floppy_b_write_protect = 0;
  for (i=0;i<8;++i) {
    config.acsi[i] = NULL;
  }
  config.gemdos = NULL;
  config.right_alt_is_altgr = 0;
  config.midi_in = NULL;
  config.midi_out = NULL;
  config.jukebox_enabled = 0;
  config.jukebox_timeout_duration = 90;
  config.jukebox_path = NULL;
}

void config_set_file(const char *filename) {
  if (config_file) {
    free((void*)config_file);
    config_file = NULL;
  }
  if (filename) {
    config_file = strdup(filename);
  }
}

const char *config_get_file(void) {
  return config_file;
}

void config_load(void) {
  if (!config_file)
    return;
  config_init();
  if (ini_parse(config_file,handler,&config) < 0) {
    printf("Can't load `%s`\n",config_file);
    return;
  }
}

void config_save(void) {
  int i;
  if (!config_file)
    return;
  FILE *fd = fopen(config_file,"w");
  if (!fd) {
    perror(config_file);
    return;
  }
  fprintf(fd,"[main]\n");
  fprintf(fd,"mono = %s\n",config.mono?"true":"false");
  fprintf(fd,"extended_video_modes = %s\n",config.extended_video_modes?"on":"off");
  fprintf(fd,"turbo = %s\n",config.turbo?"on":"off");
  fprintf(fd,"mem_size = %s\n",memsize_values[config.mem_size]);
  fprintf(fd,"wakestate = %d\n",config.wakestate+1);
  fprintf(fd,"shifter_wakestate = %d\n",config.shifter_wakestate);
  fprintf(fd,"scan_doubler_mode = %d\n",config.scan_doubler_mode);
  fprintf(fd,"rom_file = %s\n",config.rom_file?config.rom_file:"");
  fprintf(fd,"timezone = %d\n",config.timezone-12);
  fprintf(fd,"keymap = %s\n",keymap_values[config.keymap_id]);

  fprintf(fd,"\n[floppy]\n");
  fprintf(fd,"floppy_a_enable = %s\n",config.floppy_a_enable?"true":"false");
  fprintf(fd,"floppy_a = %s\n",config.floppy_a?config.floppy_a:"");
  fprintf(fd,"floppy_a_write_protect = %s\n",config.floppy_a_write_protect?"true":"false");
  if (config.floppy_b_enable) {
    fprintf(fd,"floppy_b_enable = %s\n",config.floppy_b_enable?"true":"false");
    fprintf(fd,"floppy_b = %s\n",config.floppy_b?config.floppy_b:"");
    fprintf(fd,"floppy_b_write_protect = %s\n",config.floppy_b_write_protect?"true":"false");
  }

  int hdd_section = config.gemdos!=NULL;
  for (i=0;i<8;++i) {
    if (config.acsi[i])
      hdd_section = 1;
  }
  if (hdd_section) {
    fprintf(fd,"\n[hdd]\n");
    for (i=0;i<8;++i) {
      if (config.acsi[i])
        fprintf(fd,"acsi%d = %s\n",i,config.acsi[i]?config.acsi[i]:"");
    }
    if (config.gemdos)
      fprintf(fd,"gemdos = %s\n",config.gemdos);
  }

  fprintf(fd,"\n[keyboard]\n");
  fprintf(fd,"right_alt_is_altgr = %s\n",config.right_alt_is_altgr?"true":"false");

  if (config.midi_in||config.midi_out) {
    fprintf(fd,"\n[midi]\n");
    fprintf(fd,"in = %s\n",config.midi_in?config.midi_in:"");
    fprintf(fd,"out = %s\n",config.midi_out?config.midi_out:"");
  }

  if (config.jukebox_enabled) {
    fprintf(fd,"\n[jukebox]\n");
    fprintf(fd,"enabled = %s\n",config.jukebox_enabled?"true":"false");
    fprintf(fd,"path = %s\n",config.jukebox_path?config.jukebox_path:"");
    fprintf(fd,"timeout = %d\n",(int)(config.jukebox_timeout_duration));
  }

  fclose(fd);
}
