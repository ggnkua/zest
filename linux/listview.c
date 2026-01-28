/*
 * listview.c - List view system in the zeST OSD menu
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
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

#include "listview.h"
#include "osd.h"
#include "font.h"
#include "input.h"
#include "config.h"
#include "misc.h"

#include "dead_combos.c"
#include "keymap_bepo.c"
#include "keymap_de.c"
#include "keymap_dk.c"
#include "keymap_es.c"
#include "keymap_fr.c"
#include "keymap_gb.c"
#include "keymap_nl.c"
#include "keymap_no.c"
#include "keymap_pl.c"
#include "keymap_se.c"
#include "keymap_us.c"

// entry types
#define LV_ENTRY_ACTION 1
#define LV_ENTRY_CHOICE 2
#define LV_ENTRY_FILE 3
#define LV_ENTRY_MIDI 4
#define LV_ENTRY_EDITABLE 5

// width of choice field in 16-pixel rasters
#define N_RASTER_CHOICE 4
#define N_RASTER_FILE 6
#define N_RASTER_MIDI 8

#define BUF_SIZE 256

struct {
  const char *title;
  const int16_t (*map)[128];
} keymaps[] = {
  {"Danish",keymap_dk},
  {"Dutch",keymap_nl},
  {"English (UK)",keymap_gb},
  {"English (US)",keymap_us},
  {"French",keymap_fr},
  {"Fr. (bépo)",keymap_bepo},
  {"German",keymap_de},
  {"Norwegian",keymap_no},
  {"Polish",keymap_pl},
  {"Spanish",keymap_es},
  {"Swedish",keymap_se},
};

Font *lv_font;

const uint32_t lv_palette[4] = {0x000040,0xc0c000,0x2020c0,0xf0f060};

extern volatile int thr_end;

// make a screenshot of the OSD as a TGA image in the current directory
//void osd_screenshot(void);

struct lv_entry {
  int type;
  int dynamic;
  const char *title;
};

// basic object model in which all lv_* structs inherit from lv_entry
struct lv_action {
  struct lv_entry e;
};

struct lv_choice {
  struct lv_entry e;
  int n_choices;
  int *selected;
  const char **entries;
};

struct lv_file {
  struct lv_entry e;
  const char **filename;
  int flags;
  int (*filter)(const struct dirent *);
};

struct lv_midi {
  struct lv_entry e;
  const char **portname;
  const char *devname;
};

struct lv_editable {
  struct lv_entry e;
  int capacity;
  int nc;
  int cur_pos;              // cursor position in string
  int cur_x;                // cursor position in pixels
  int shift;                // number of pixels of textfield shifting
  int n_raster;
  char *text;
};

struct listview {
  int xpos;
  int ypos;
  int width;
  int height;
  int selected;             // selected entry
  int offset;               // first entry to display
  int align_left;
  const char *header;
  uint32_t *colour_change;
  int n_entries;
  int capacity;
  struct lv_entry **entries;
};

// get character from pressed keys
static int read_character(int keycode, int pressed) {
  static int shiftstate = 0;
  static int dead = 0;
  if (keycode==KEY_LEFTSHIFT||keycode==KEY_RIGHTSHIFT) {
    shiftstate = pressed?shiftstate|1:shiftstate&~1;
  }
  else if (keycode==KEY_CAPSLOCK && pressed) {
    shiftstate ^= 2;
  }
  else if (keycode==KEY_RIGHTALT) {
    shiftstate = pressed?shiftstate|4:shiftstate&~4;
  }
  else if (pressed) {
    int val = keymaps[config.keymap_id].map[shiftstate][keycode];
    if (dead) {
      int16_t (*p)[2] = dead_combos;
      while ((*p)[0]!=dead) p=p+(*p)[1]+1;
      int n = (*p)[1];
      int i;
      for (i=0;i<n;++i) {
        if ((*p)[0]==val) break;
        ++p;
      }
      val = i<n?(*p)[1]:0;
      dead = 0;
    }
    if (val<0) {
      dead = -val;
      return 0;
    }
    return val;
  }
  return 0;
}

// read the next multi-byte character in an UTF-8-formatted string
static int decode_utf8(const char **text) {
  int c = 0;
	const uint8_t *p = (const uint8_t *)*text;
  for (;;) {
    c = *p++;
    if (c>=0x80) {
      int nbytes = 0;
      if (c<0xc0) continue;
      if (c<0xe0) {
        c = c&0x1f;
        nbytes = 1;
      } else if (c<0xf0) {
        c = c&0xf;
        nbytes = 2;
      } else if (c<0xf8) {
        c = c&0x7;
        nbytes = 3;
      } else continue;
      int i;
      for (i=0;i<nbytes;++i) {
        int xc = *p++;
        if (xc<0x80||xc>=0xC0) break;
        c = c<<6 | (xc&0x3f);
      }
      if (i<nbytes) continue;
    }
    break;
  }
  *text = (const char*)p;
  return c;
}

static int encode_utf8(char *buf, unsigned int c) {
  char *p = buf;
  if (c<0x80) {
    *p++ = c;
  } else if (c<0x800) {
    *p++ = 0xc0 | c>>6;
    *p++ = 0x80 | (c&0x3f);
  } else if (c<0x10000) {
    *p++ = 0xe0 | c>>12;
    *p++ = 0x80 | (c>>6&0x3f);
    *p++ = 0x80 | (c&0x3f);
  } else if (c<0x200000) {
    *p++ = 0xf0 | c>>18;
    *p++ = 0x80 | (c>>12&0x3f);
    *p++ = 0x80 | (c>>6&0x3f);
    *p++ = 0x80 | (c&0x3f);
  }
  *p = 0;
  return p-buf;
}

// Read a single line from a file into buf; returns 1 if success, 0 if fail
static int read_sysfs_str(const char *path, char *buf) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;

  if (!fgets(buf,BUF_SIZE,f)) {
    fclose(f);
    return 0;
  }
  fclose(f);
  // Strip newline
  buf[strcspn(buf,"\n")] = '\0';
  return 1;
}

// Given the sysfs path of midi device (e.g. "/sys/class/sound/midiC1D0"),
// try to get some strings such as USB product and manufacturer.
// Extra items after n_str are a sequence of string identifier (key),
// and a character pointer to write the value to. There must be n_str pairs.
// Returns 1 on success (at least the first item is found), 0 on failure.
static int midi_get_strings(const char *midi_sysfs_path, int n_str, ...) {
  va_list ap;
  va_start(ap,n_str);
  char real_path[PATH_MAX];
  ssize_t len = readlink(midi_sysfs_path,real_path,sizeof(real_path)-1);
  if (len < 0) return 0;
  real_path[len] = '\0';

  // Resolve to absolute path
  char abs_path[PATH_MAX];
  if (!realpath(midi_sysfs_path, abs_path)) {
    // fallback to readlink result
    strncpy(abs_path, real_path, sizeof(abs_path));
    abs_path[sizeof(abs_path)-1] = '\0';
  }

  // climb up the hierarchy until we find the first key
  const char *key = va_arg(ap,const char*);
  char *value = va_arg(ap,char *);
  value[0] = '\0';
  char pathbuf[PATH_MAX*2];
  for (;;) {
    char *slash = strrchr(abs_path, '/');
    if (!slash) { va_end(ap); return 0; }
    *slash = '\0';
    snprintf(pathbuf,sizeof(pathbuf),"%s/%s",abs_path,key);
    if (read_sysfs_str(pathbuf,value))
      break;
  }

  // Read other keys (optional)
  for (int i=1;i<n_str;++i) {
    key = va_arg(ap,const char*);
    value = va_arg(ap,char*);
    value[0] = '\0';
    snprintf(pathbuf,sizeof(pathbuf),"%s/%s",abs_path,key);
    read_sysfs_str(pathbuf,value);
  }
  va_end(ap);
  return 1;
}

// Produce device name from either USB product info or driver id
static char *midi_device_name(const char *dev) {
  char name[BUF_SIZE];
  char sysfs_path[BUF_SIZE];
  char product[BUF_SIZE];
  char manufacturer[BUF_SIZE];

  snprintf(sysfs_path,sizeof(sysfs_path),"/sys/class/sound/%s",dev);
  if (midi_get_strings(sysfs_path,2,"product",product,"manufacturer",manufacturer)) {
    if (manufacturer[0]) {
      snprintf(name,BUF_SIZE*2,"%s %s",manufacturer,product);
    } else {
      strncpy(name,product,BUF_SIZE);
    }
  } else if (midi_get_strings(sysfs_path,1,"id",product)) {
    strncpy(name,product,BUF_SIZE);
  } else {
    name[0] = '\0';
  }
  return name[0]?strdup(name):NULL;
}

static int add_entry(ListView *lv,int type,const char *title,struct lv_entry *e) {
  if (lv->n_entries==lv->capacity) {
    lv->capacity = lv->capacity?lv->capacity*2:2;
    lv->entries = realloc(lv->entries,lv->capacity*sizeof(struct lv_entry*));
  }
  e->type = type;
  e->dynamic = 0;
  e->title = strdup(title);
  lv->entries[lv->n_entries] = e;
  return lv->n_entries++;
}

void lv_init(const char *font_file_name) {
  osd_init();
  lv_font = font_new_from_file(font_file_name);
}

int lv_entry_height(void) {
  return font_get_height(lv_font);
}

ListView *lv_new(int xpos, int ypos, int width, int height, const char *header) {
  ListView *lv = malloc(sizeof(ListView));
  memset(lv,0,sizeof(ListView));
  lv->xpos = xpos;
  lv->ypos = ypos;
  lv->width = width&-16;
  lv->height = height;
  lv->header = header;
  lv->colour_change = malloc(height*sizeof(uint32_t));
  memset(lv->colour_change,-1,height*sizeof(uint32_t));
  return lv;
}

void lv_set_colour_change(ListView *lv, int line_no, int col_no, uint32_t rgb) {
  lv->colour_change[line_no] = col_no<<24 | rgb;
}

void lv_delete(ListView *lv) {
  if (lv) {
    int i;
    for (i=0;i<lv->n_entries;++i) {
      struct lv_entry *e = lv->entries[i];
      switch (e->type) {
      case LV_ENTRY_ACTION:
        //struct lv_action *a = (struct lv_action*)e;
        // nothing to free
        break;
      case LV_ENTRY_CHOICE: {
        struct lv_choice *ch = (struct lv_choice*)e;
        free(ch->entries);
        break;
      }
      case LV_ENTRY_FILE:
        //struct lv_file *fl = (struct lv_file*)e;
        break;
      case LV_ENTRY_MIDI:
        struct lv_midi *md = (struct lv_midi*)e;
        free((char*)md->devname);
        break;
      }
      free((char*)e->title);
      free(e);
    }
    free(lv->entries);
    free(lv);
  }
}

// add entry with action function
int lv_add_action(ListView *lv, const char *title) {
  struct lv_action *a = malloc(sizeof(struct lv_action));
  return add_entry(lv,LV_ENTRY_ACTION,title,(struct lv_entry*)a);
}

// add entry with a list of choices (vararg mode)
int lv_add_choice(ListView *lv, const char *title, int *pselect, int count, ...) {
  struct lv_choice *ch = malloc(sizeof(struct lv_choice));
  ch->n_choices = count;
  ch->selected = pselect;
  ch->entries = malloc(count*sizeof(const char*));

  int i;
  va_list ap;
  va_start(ap,count);
  for (i=0;i<count;++i) {
    ch->entries[i] = va_arg(ap,char*);
  }
  va_end(ap);
  return add_entry(lv,LV_ENTRY_CHOICE,title,(struct lv_entry*)ch);
}

// add entry with a list of choices (array mode)
int lv_add_choice_array(ListView *lv, const char *title, int *pselect, int count, const char **entries) {
  struct lv_choice *ch = malloc(sizeof(struct lv_choice));
  ch->n_choices = count;
  ch->selected = pselect;
  ch->entries = malloc(count*sizeof(const char*));

  int i;
  for (i=0;i<count;++i) {
    ch->entries[i] = entries[i];
  }
  return add_entry(lv,LV_ENTRY_CHOICE,title,(struct lv_entry*)ch);
}

// add entry with a list of choices
int lv_add_keymap_choice(ListView *lv) {
  struct lv_choice *ch = malloc(sizeof(struct lv_choice));
  ch->n_choices = sizeof(keymaps)/sizeof(keymaps[0]);
  ch->selected = &config.keymap_id;
  ch->entries = malloc(ch->n_choices*sizeof(const char*));

  int i;
  for (i=0;i<ch->n_choices;++i) {
    ch->entries[i] = keymaps[i].title;
  }
  return add_entry(lv,LV_ENTRY_CHOICE,"Keymap",(struct lv_entry*)ch);
}

// sets if entry is dynamic (menu exits on every change)
void lv_entry_set_dynamic(ListView *lv, int entry, int dynamic) {
  struct lv_entry *e = lv->entries[entry];
  e->dynamic = dynamic;
}

// add entry with a file to select
// possible flags:
// - LV_FILE_EJECTABLE: the user can "eject" the file using the Delete/Backspace keys, or appropriate controller button
// - LV_FILE_DIRECTORY: select a directory instead of a file
int lv_add_file(ListView *lv, const char *title, const char **pfilename, int flags, int (*filter)(const struct dirent *)) {
  struct lv_file *fi = malloc(sizeof(struct lv_file));
  fi->filename = pfilename;
  fi->flags = flags;
  fi->filter = filter;
  return add_entry(lv,LV_ENTRY_FILE,title,(struct lv_entry*)fi);
}

// add entry with a midi port to select
int lv_add_midi(ListView *lv, const char *title, const char **pportname) {
  struct lv_midi *e = malloc(sizeof(struct lv_midi));
  e->portname = pportname;
  if (*pportname) {
    e->devname = midi_device_name(*pportname);
  } else {
    e->devname = NULL;
  }
  return add_entry(lv,LV_ENTRY_MIDI,title,(struct lv_entry*)e);
}

// add entry with an editable text field
int lv_add_editable(ListView *lv, const char *title, int capacity, char *text, int width) {
  struct lv_editable *e = malloc(sizeof(struct lv_editable));
  e->capacity = capacity;
  e->nc = strlen(text);
  e->text = text;
  e->cur_pos = e->nc;
  e->cur_x = font_text_width(lv_font,text);
  e->shift = 0;
  e->n_raster = width;
  return add_entry(lv,LV_ENTRY_EDITABLE,title,(struct lv_entry*)e);
}

static int editable_update_shift(struct lv_editable *ed) {
  if (ed->cur_x < ed->shift) {
    ed->shift = ed->cur_x;
    return 1;
  }
  if (ed->cur_x-ed->shift >= ed->n_raster*16) {
    ed->shift = ed->cur_x-ed->n_raster*16+1;
    return 1;
  }
  return 0;
}

static void display_entry(ListView *lv, int line_no) {
  int raster_count = lv->width/16;
  int font_height = font_get_height(lv_font);
  uint32_t *bitmap = osd_bitmap+(line_no+1)*raster_count*font_height;
  struct lv_entry *e = lv->entries[line_no+lv->offset];
  switch (e->type) {
  case LV_ENTRY_ACTION:
    font_render_text(lv_font,bitmap,raster_count,2,font_height,lv->width,0,e->title);
    break;
  case LV_ENTRY_CHOICE: {
    const struct lv_choice *ch = (struct lv_choice*)e;
    font_render_text(lv_font,bitmap,raster_count,2,font_height,(raster_count-N_RASTER_CHOICE)*16,0,e->title);
    font_render_text_centered(lv_font,bitmap+raster_count-N_RASTER_CHOICE,raster_count,2,font_height,N_RASTER_CHOICE*16,ch->entries[*ch->selected]);
    break;
  }
  case LV_ENTRY_FILE: {
    const struct lv_file *fl = (struct lv_file*)e;
    const char *filename = *fl->filename;
    if (filename&&strcmp(filename,"/")) {
      const char *sep = strrchr(filename,'/');
      if (sep && fl->flags & LV_FILE_DIRECTORY) {
        if (sep) {
          do { --sep; } while (sep>filename && *sep!='/');
        }
      } else {
        if (!filename[0]) filename = NULL;
      }
      if (sep) filename = sep+1;
    }
    font_render_text(lv_font,bitmap,raster_count,2,font_height,(raster_count-N_RASTER_FILE)*16,0,e->title);
    font_render_text_centered(lv_font,bitmap+raster_count-N_RASTER_FILE,raster_count,2,font_height,N_RASTER_FILE*16,filename?filename:"<empty>");
    break;
  }
  case LV_ENTRY_MIDI: {
    const struct lv_midi *md = (struct lv_midi*)e;
    const char *portname = *md->portname;
    char buf[BUF_SIZE];
    if (md->devname) {
      snprintf(buf,BUF_SIZE,"%s: %s",portname+4,md->devname);
    }
    font_render_text(lv_font,bitmap,raster_count,2,font_height,(raster_count-N_RASTER_MIDI)*16,0,e->title);
    font_render_text_centered(lv_font,bitmap+raster_count-N_RASTER_MIDI,raster_count,2,font_height,N_RASTER_MIDI*16,md->devname?buf:portname?portname+4:"<disconnected>");
    break;
  }
  case LV_ENTRY_EDITABLE: {
    const struct lv_editable *ed = (struct lv_editable*)e;
    font_render_text(lv_font,bitmap,raster_count,2,font_height,(raster_count-ed->n_raster)*16,0,e->title);
    font_render_text(lv_font,bitmap+raster_count-ed->n_raster,raster_count,2,font_height,ed->n_raster*16,-ed->shift,ed->text);
    break;
  }
  }
}

static void highlight(ListView *lv, int line_no, int highlight) {
  int font_height = font_get_height(lv_font);
  unsigned int mask = highlight ? 0xffff : 0;
  int raster_count = lv->width/16;
  const struct lv_entry *e = lv->entries[line_no+lv->offset];
  if (e->type == LV_ENTRY_ACTION) {
    int beg_offset = raster_count*font_height*(line_no+1);
    int end_offset = raster_count*font_height*(line_no+2);
    int i;
    for (i=beg_offset;i<end_offset;++i) {
      *((uint16_t*)(osd_bitmap+i)+1) = mask;
    }
  } else {
    int i,y;
    int n_raster = e->type==LV_ENTRY_CHOICE?N_RASTER_CHOICE:e->type==LV_ENTRY_FILE?N_RASTER_FILE:e->type==LV_ENTRY_MIDI?N_RASTER_MIDI:((struct lv_editable*)e)->n_raster;
    int beg = raster_count-n_raster;
    uint32_t *bitmap = osd_bitmap+raster_count*font_height*(line_no+1);
    for (y=0;y<font_height;++y) {
      for (i=beg;i<raster_count;++i) {
        *((uint16_t*)(bitmap+i)+1) = mask;
      }
      bitmap += raster_count;
    }
    if (e->type==LV_ENTRY_EDITABLE) {
      const struct lv_editable *ed = (struct lv_editable*)e;
      int cur_x = ed->cur_x-ed->shift;
      bitmap = osd_bitmap+raster_count*font_height*(line_no+1)+beg+cur_x/16;
      unsigned int mask = 0x8000>>(cur_x%16);
      for (y=0;y<font_height;++y) {
        *((uint16_t*)bitmap) ^= mask;
        bitmap += raster_count;
      }
    }
  }
}

// clear the text zone of current choice before displaying a new value
static void clear_choice(ListView *lv) {
  const struct lv_entry *e = lv->entries[lv->selected];
  if (e->type == LV_ENTRY_CHOICE) {
    int font_height = font_get_height(lv_font);
    int raster_count = lv->width/16;
    int i,y;
    int beg = raster_count-N_RASTER_CHOICE;
    uint32_t *bitmap = osd_bitmap+raster_count*font_height*(lv->selected-lv->offset+1);
    for (y=0;y<font_height;++y) {
      for (i=beg;i<raster_count;++i) {
        *(uint16_t*)(bitmap+i) = 0;
      }
      bitmap += raster_count;
    }
  }
}

static void update_pos(ListView *lv, int new_pos) {
  if (new_pos==lv->selected)
    return;
  int i;
  int raster_count = lv->width/16;
  int font_height = font_get_height(lv_font);
  int old_offset = lv->offset;
  int max_display = lv->height/font_get_height(lv_font)-1;
  highlight(lv,lv->selected-lv->offset,0);
  int line_size = raster_count*font_height;
  uint32_t *bitmap = osd_bitmap+line_size;

  // scroll down
  if (new_pos-lv->offset>=max_display) {
    lv->offset = new_pos-max_display+1;
    int n_redisplay = lv->offset - old_offset;
    if (n_redisplay>max_display) n_redisplay = max_display;
    int n_scroll = max_display - n_redisplay;
    memmove(bitmap,bitmap+n_redisplay*line_size,n_scroll*line_size*sizeof(uint32_t));
    memset(bitmap+n_scroll*line_size,0,n_redisplay*line_size*sizeof(uint32_t));
    for (i=n_scroll;i<max_display;++i) {
      display_entry(lv,i);
    }
  }

  // scroll up
  if (new_pos<lv->offset) {
    lv->offset = new_pos;
    int n_redisplay = old_offset - lv->offset;
    if (n_redisplay>max_display) n_redisplay = max_display;
    int n_scroll = max_display - n_redisplay;
    memmove(bitmap+n_redisplay*line_size,bitmap,n_scroll*line_size*sizeof(uint32_t));
    memset(bitmap,0,n_redisplay*line_size*sizeof(uint32_t));
    for (i=0;i<n_redisplay;++i) {
      display_entry(lv,i);
    }
  }

  struct lv_entry *e = lv->entries[new_pos];
  if (e->type==LV_ENTRY_EDITABLE) {
    struct lv_editable *ed = (struct lv_editable*)e;
    if (editable_update_shift(ed)) {
      memset(osd_bitmap+(new_pos-lv->offset+1)*line_size,0,line_size*sizeof(uint32_t));
      display_entry(lv,new_pos-lv->offset);
    }
  }
  highlight(lv,new_pos-lv->offset,1);
  lv->selected = new_pos;
}

static void update_choice(ListView *lv, int new_ch) {
  const struct lv_entry *e = lv->entries[lv->selected];
  if (e->type == LV_ENTRY_CHOICE) {
    struct lv_choice *ch = (struct lv_choice*)e;
    *ch->selected = new_ch;
    clear_choice(lv);
    display_entry(lv,lv->selected-lv->offset);
  }
}

int lv_select(ListView *lv, int selected) {
  if (selected>=lv->n_entries) {
    lv->selected = lv->n_entries-1;
  } else if (selected<0) {
    lv->selected = 0;
  } else {
    lv->selected = selected;
  }
  return lv->selected;
}

static int file_select_compar(const struct dirent **a, const struct dirent **b) {
  // directories should come before files
  if ((*a)->d_type==DT_DIR && (*b)->d_type!=DT_DIR) {
    return -1;
  }
  if ((*a)->d_type!=DT_DIR && (*b)->d_type==DT_DIR) {
    return 1;
  }
  return strcasecmp((*a)->d_name,(*b)->d_name);
}

static const char *file_select(int xpos, int ypos, int width, int height, const char *init_file, int (*filter)(const struct dirent *), int flags) {
  char directory[1024];
  char init_file_name[256];
  init_file_name[0] = 0;

  // establish absolute directory location
  if (init_file==NULL) {
    getcwd(directory,sizeof(directory));
  } else if (init_file[0]=='/') {
    char *p = strrchr(init_file,'/');
    strcpy(init_file_name,p+1);
    int len = p-init_file;
    strncpy(directory,init_file,len);
    directory[len] = '\0';
  } else {
    getcwd(directory,sizeof(directory));
    char *p = strrchr(init_file,'/');
    if (p) {
      strcpy(init_file_name,p+1);
      int len = p-init_file;
      strcat(directory,"/");
      strncat(directory,init_file,len);
    } else {
      strcpy(init_file_name,init_file);
    }
  }

  int ret = 0;
  while (ret==0) {
    struct dirent **namelist;
    int n = scandir(directory,&namelist,filter,file_select_compar);

    ListView *fslv = lv_new(xpos, ypos, width, height, directory);
    fslv->align_left = 1;
    int entry_height = lv_entry_height();
    uint32_t gradient_header[entry_height];
    gradient(gradient_header,entry_height,0x79de07,0x488c14);
    int i;
    for (i=0;i<entry_height;++i) {
      lv_set_colour_change(fslv,i,1,gradient_header[i]);
    }
    lv_set_colour_change(fslv,entry_height,1,lv_palette[1]);
    lv_add_action(fslv,"<parent dir>");
    for (i=0;i<n;++i) {
      struct dirent *e = namelist[i];
      char buf[400];
      const char *name = e->d_name;
      if (e->d_type==DT_DIR) {
        sprintf(buf,"[%s]",name);
        name = buf;
      }
      int id = lv_add_action(fslv,name);
      if (!strcmp(e->d_name,init_file_name)) {
        lv_select(fslv,id);
      }
    }
    if (flags & LV_FILE_DIRECTORY) {
      lv_add_action(fslv,"<choose this directory>");
    }
    ret = lv_run(fslv);
    lv_delete(fslv);

    if (ret==0) {
      // parent dir
      char *p = strrchr(directory,'/');
      strcpy(init_file_name,p+1);
      if (p!=directory) {
        *p = '\0';
      } else {
        *++p = '\0';
      }
    } else if ((flags&LV_FILE_DIRECTORY) && ret==n+1) {
      if (strcmp(directory,"/")) strcat(directory,"/");
    } else if (ret>0) {
      if (strcmp(directory,"/")) strcat(directory,"/");
      strcat(directory,namelist[ret-1]->d_name);
      if (namelist[ret-1]->d_type==DT_DIR) ret = 0;
    }

    for (i=0;i<n;++i) {
      free(namelist[i]);
    }
    free(namelist);
  }

  if (ret==-1) return NULL;
  return strdup(directory);
}

static int midi_filter(const struct dirent *e) {
  if (e->d_type==DT_DIR)
    return 0;
  if (strncmp(e->d_name,"midiC",5))
    return 0;
  return 1;
}

const char *midi_select(int xpos, int ypos, int width, int height, const char *title, const char *init_val) {
  char port[256];
  struct dirent **namelist;
  int n = scandir("/dev/snd",&namelist,midi_filter,alphasort);
  if (n==0) return NULL;
  int i;
  ListView *lv = lv_new(xpos,ypos,width,height,title);
  int entry_height = lv_entry_height();
  uint32_t gradient_header[entry_height];
  gradient(gradient_header,entry_height,0x79de07,0x488c14);
  for (i=0;i<entry_height;++i) {
    lv_set_colour_change(lv,i,1,gradient_header[i]);
  }
  lv_set_colour_change(lv,entry_height,1,lv_palette[1]);
  for (i=0;i<n;++i) {
    const char *name = namelist[i]->d_name;
    char *devname = midi_device_name(name);
    if (devname) {
      char buf[BUF_SIZE];
      snprintf(buf,BUF_SIZE,"%s: %s",name+4,devname);
      lv_add_action(lv,buf);
    } else {
      lv_add_action(lv,name+4);
    }
    if (init_val&&!strcmp(init_val,namelist[i]->d_name)) {
      lv_select(lv,i);
    }
  }
  int ret = lv_run(lv);
  if (ret>=0) {
    strncpy(port,namelist[ret]->d_name,sizeof(port));
  }

  for (i=0;i<n;++i) {
    free(namelist[i]);
  }
  free(namelist);
  if (ret==-1) return NULL;
  return strdup(port);
}

static void lv_draw(ListView *lv) {
  osd_set_palette(lv_palette);
  osd_set_palette_changes(lv->colour_change,lv->height);
  osd_set_position(lv->xpos,lv->ypos);
  osd_set_size(lv->width,lv->height);

  int font_height = font_get_height(lv_font);
  int raster_count = lv->width/16;
  memset(osd_bitmap,0,raster_count*lv->height*sizeof(uint32_t));
  int offset = raster_count*font_height;
  int max_display = lv->height/font_height-1;
  int i;
  if (lv->align_left) {
    font_render_text(lv_font,osd_bitmap,raster_count,2,font_height,lv->width,0,lv->header);
  } else {
    font_render_text_centered(lv_font,osd_bitmap,raster_count,2,font_height,lv->width,lv->header);
  }
  int n_display = lv->n_entries<max_display ? lv->n_entries : max_display;
  for (i=0;i<n_display;++i) {
    display_entry(lv,i);
    offset += raster_count*font_height;
  }
  highlight(lv,lv->selected-lv->offset,1);
}

int lv_run(ListView *lv) {
  int font_height = font_get_height(lv_font);
  int max_display = lv->height/font_height-1;

  lv->offset = lv->selected-max_display/2;
  if (lv->offset>lv->n_entries-max_display) {
    lv->offset = lv->n_entries-max_display;
  }
  if (lv->offset<0) {
    lv->offset = 0;
  }
  lv_draw(lv);

  osd_show();

  int quit = 0;
  int funcret = -1;
  while (quit == 0 && thr_end == 0) {
    int evtype, evcode, evvalue, joyid;
    int retval = input_event(100,&evtype,&evcode,&evvalue,&joyid);
    int new_pos;
    if (retval < 0) {
      // an error occurred
      break;
    } else if (retval == 0) {
      osd_refresh();
      continue;
    }

    if (evtype == EV_ABS && joyid != -1 && evvalue != 0) {
      // convert joystick direction to arrow key press
      if (evcode==0) {
        evcode = evvalue>0?KEY_RIGHT:KEY_LEFT;
      } else if (evcode==1) {
        evcode = evvalue>0?KEY_DOWN:KEY_UP;
      }
      evtype = EV_KEY;
      evvalue = 1;
    }
    if (evtype == EV_KEY) {
      //printf("evtype=EV_KEY evcode=%d evvalue=%d\n",evcode,evvalue);
      struct lv_entry *e = lv->entries[lv->selected];
      // keyboard event, key is pressed
      if (evvalue >= 1) {
        // key is pressed
        switch (evcode) {
        case KEY_ESC:
        case BTN_START:
        case BTN_EAST:
          quit = 1;
          break;
        case KEY_DOWN:
          if (lv->selected<lv->n_entries-1) {
            update_pos(lv,lv->selected+1);
          }
          break;
        case KEY_UP:
          if (lv->selected>0) {
            update_pos(lv,lv->selected-1);
          }
          break;
        case KEY_PAGEDOWN:
          new_pos = lv->selected+max_display;
          if (new_pos>lv->n_entries-1) {
            new_pos = lv->n_entries-1;
          }
          update_pos(lv,new_pos);
          break;
        case KEY_PAGEUP:
          new_pos = lv->selected-max_display;
          if (new_pos<0) {
            new_pos = 0;
          }
          update_pos(lv,new_pos);
          break;
        case KEY_HOME:
        case KEY_END:
          if (e->type==LV_ENTRY_EDITABLE) {
            struct lv_editable *ed = (struct lv_editable*)e;
            highlight(lv,lv->selected-lv->offset,0);
            if (evcode==KEY_HOME) {
              ed->cur_pos = 0;
              ed->cur_x = 0;
            } else {
              ed->cur_pos = ed->nc;
              ed->cur_x = font_text_width(lv_font,ed->text);
            }
            if (editable_update_shift(ed)) {
              int line_size = lv->width/16*font_get_height(lv_font);
              memset(osd_bitmap+(lv->selected-lv->offset+1)*line_size,0,line_size*sizeof(uint32_t));
              display_entry(lv,lv->selected-lv->offset);
            }
            highlight(lv,lv->selected-lv->offset,1);
          } else {
            update_pos(lv,evcode==KEY_END?lv->n_entries-1:0);
          }
          break;
        case KEY_LEFT:
        case KEY_RIGHT:
          if (e->type==LV_ENTRY_CHOICE) {
            struct lv_choice *ch = (struct lv_choice*)e;
            if (evcode==KEY_LEFT) {
              update_choice(lv,(*ch->selected+ch->n_choices-1)%ch->n_choices);
            } else {
              update_choice(lv,(*ch->selected+1)%ch->n_choices);
            }
            if (e->dynamic) {
              funcret = lv->selected;
              quit = 1;
            }
          } else if (e->type==LV_ENTRY_EDITABLE) {
            struct lv_editable *ed = (struct lv_editable*)e;
            highlight(lv,lv->selected-lv->offset,0);
            if (evcode==KEY_LEFT) {
              if (ed->cur_pos>0) {
                int pos = ed->cur_pos;
                do --pos; while ((ed->text[pos]&0x80) && (ed->text[pos]&0xc0)==0x80);
                const char *p = ed->text+pos;
                ed->cur_x -= font_char_width(lv_font,decode_utf8(&p));
                ed->cur_pos = pos;
              }
            } else {
              if (ed->cur_pos<ed->nc) {
                const char *p = ed->text+ed->cur_pos;
                ed->cur_x += font_char_width(lv_font,decode_utf8(&p));
                ed->cur_pos = p-ed->text;
              }
            }
            if (editable_update_shift(ed)) {
              int line_size = lv->width/16*font_get_height(lv_font);
              memset(osd_bitmap+(lv->selected-lv->offset+1)*line_size,0,line_size*sizeof(uint32_t));
              display_entry(lv,lv->selected-lv->offset);
            }
            highlight(lv,lv->selected-lv->offset,1);
          }
          break;
        //case KEY_F2:
        //  osd_screenshot();
        //  break;
        case KEY_DELETE:
        case KEY_BACKSPACE:
        case BTN_WEST:
          if (e->type==LV_ENTRY_FILE) {
            const struct lv_file *lf = (struct lv_file*)e;
            if ((lf->flags&LV_FILE_EJECTABLE) && *lf->filename) {
              if (lf->flags&LV_FILE_EJECTABLE) {
                free((void*)*lf->filename);
                *lf->filename = NULL;
                osd_hide();
                lv_draw(lv);
                osd_show();
              } else {
                char *p = (char*)strrchr(*lf->filename,'/');
                if (p) {
                  osd_hide();
                  p[1] = '\0';
                  lv_draw(lv);
                  osd_show();
                }
              }
            }
          }
          else if (e->type==LV_ENTRY_MIDI) {
            struct lv_midi *md = (struct lv_midi*)e;
            if (md->portname) {
              char *p = (char*)(*md->portname);
              if (p) {
                osd_hide();
                free(p);
                *md->portname = NULL;
                if (md->devname) {
                  free((char*)md->devname);
                  md->devname = NULL;
                }
                lv_draw(lv);
                osd_show();
              }
            }
          }
          else if (e->type==LV_ENTRY_EDITABLE) {
            struct lv_editable *ed = (struct lv_editable*)e;
            if (evcode==KEY_BACKSPACE) {
              if (ed->cur_pos>0) {
                int pos = ed->cur_pos;
                do --pos; while ((ed->text[pos]&0x80) && (ed->text[pos]&0xc0)==0x80);
                const char *p = ed->text+pos;
                int c = decode_utf8(&p);
                int len = ed->cur_pos-pos;
                memmove(ed->text+pos,ed->text+ed->cur_pos,ed->nc-ed->cur_pos+1);
                highlight(lv,lv->selected-lv->offset,0);
                ed->cur_x -= font_char_width(lv_font,c);
                ed->cur_pos = pos;
                ed->nc -= len;
                int cxd = ed->cur_x-20;
                if (ed->shift>cxd) ed->shift = cxd>0?cxd:0;
                int line_size = lv->width/16*font_get_height(lv_font);
                memset(osd_bitmap+(lv->selected-lv->offset+1)*line_size,0,line_size*sizeof(uint32_t));
                display_entry(lv,lv->selected-lv->offset);
                highlight(lv,lv->selected-lv->offset,1);
              }
            }
            else if (evcode==KEY_DELETE) {
              if (ed->cur_pos<ed->nc) {
                char *p0 = ed->text+ed->cur_pos;
                const char *p = p0;
                decode_utf8(&p);
                int len = p-p0;
                memmove(p0,p,ed->nc-ed->cur_pos-len+1);
                highlight(lv,lv->selected-lv->offset,0);
                ed->nc -= len;
                int line_size = lv->width/16*font_get_height(lv_font);
                memset(osd_bitmap+(lv->selected-lv->offset+1)*line_size,0,line_size*sizeof(uint32_t));
                display_entry(lv,lv->selected-lv->offset);
                highlight(lv,lv->selected-lv->offset,1);
              }
            }
          }
          break;
        case KEY_ENTER:
        case BTN_SOUTH:
          if (e->type==LV_ENTRY_ACTION) {
            // struct lv_action *a = (struct lv_action*)e;
            funcret = lv->selected;
            quit = 1;
          }
          else if (e->type==LV_ENTRY_FILE) {
            const struct lv_file *lf = (struct lv_file*)e;
            osd_hide();
            const char *name = file_select(lv->xpos,lv->ypos,lv->width,lv->height,*lf->filename,lf->filter,lf->flags);
            if (name) {
              free((void*)*lf->filename);
              *lf->filename = name;
            }
            lv_draw(lv);
            osd_show();
            if (e->dynamic) {
              funcret = lv->selected;
              quit = 1;
            }
          }
          else if (e->type==LV_ENTRY_MIDI) {
            struct lv_midi *md = (struct lv_midi*)e;
            osd_hide();
            const char *name = midi_select(lv->xpos,lv->ypos,lv->width,lv->height,e->title,*md->portname);
            if (name) {
              free((void*)*md->portname);
              *md->portname = name;
              free((char*)md->devname);
              md->devname = midi_device_name(name);
            }
            lv_draw(lv);
            osd_show();
          }
          else if (e->type==LV_ENTRY_EDITABLE) {
            if (e->dynamic) {
              funcret = lv->selected;
              quit = 1;
            }
          }
        }
      }

      int c = read_character(evcode,evvalue);
      if (c>=32&&e->type==LV_ENTRY_EDITABLE) {
        char buf[16];
        int len = encode_utf8(buf,c);
        struct lv_editable *ed = (struct lv_editable*)e;
        if (ed->capacity-ed->nc>=len) {
          memmove(ed->text+ed->cur_pos+len,ed->text+ed->cur_pos,ed->nc-ed->cur_pos+1);
          memcpy(ed->text+ed->cur_pos,buf,len);
          highlight(lv,lv->selected-lv->offset,0);
          ed->cur_x += font_char_width(lv_font,c);
          ed->cur_pos += len;
          ed->nc += len;
          editable_update_shift(ed);
          int line_size = lv->width/16*font_get_height(lv_font);
          memset(osd_bitmap+(lv->selected-lv->offset+1)*line_size,0,line_size*sizeof(uint32_t));
          display_entry(lv,lv->selected-lv->offset);
          highlight(lv,lv->selected-lv->offset,1);
        }
      }
    }
  }

  osd_hide();

  return funcret;
}
