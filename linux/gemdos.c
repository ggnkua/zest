/*
 * gemdos.c - GEMDOS drive implementation
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

/*
 * General protocol:
 * on any GEMDOS call, the GEMDOS stub sends a special command (SC) with OP_GEMDOS and
 * the GEMDOS opcode and initiates a DMA write with a few dozen bytes of the stack.
 * - if the host decides to fall back to the original ROM code, it ends the command
 *   with STATUS_OK.
 * - if the host decides to handle the GEMDOS call:
 *   - it ends with STATUS_ERROR and waits for a new SC with OP_ACTION.
 *   - the stub switches to action mode and sends the SC/OP_ACTION in DMA read mode.
 *   - the host sends a data block with the action to perform.
 *   - The stub perform the action.
 *   - Depending on the action, resulting data may be returned by the stub to the host.
 *     In that case, the stub sends a SC with OP_RESULT, and the data is sent with a DMA write.
 *   - The stub issues a new OP_ACTION SC and the process is repeated until the host
 *     sends a ACTION_FALLBACK or ACTION_RETURN to terminate the GEMDOS call.
 *   - This whole SC/OP_ACTION process (action mode) is repeated as long as there are actions pending.
 */


#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>

#include "acsi.h"
#include "config.h"

/* DMA buffer size in sectors */
#define DMABUFSZ 5

/* ACSI status codes */
#define STATUS_OK    0
#define STATUS_ERROR 2

// format: 0xAAQQSS  AA:additional sense QQ: additional sense code qualifier SS:sense key
#define ERROR_OK       0x000000         /* OK return status */
#define ERROR_NOSECTOR 0x010004         /* No index or sector */
#define ERROR_WRITEERR 0x030002         /* Write fault */
#define ERROR_OPCODE   0x200005         /* Opcode not supported */
#define ERROR_INVADDR  0x21000d         /* Invalid block address */
#define ERROR_INVARG   0x240005         /* Invalid argument */
#define ERROR_INVLUN   0x250005         /* Invalid LUN */

/* operation codes in ACSI commands */
#define OP_GEMDOS 1                     /* new GEMDOS call */
#define OP_ACTION 2                     /* get next action to perform */
#define OP_RESULT 3                     /* send result */

/* action codes from Linux host to ST */
#define ACTION_FALLBACK 0               /* Fallback to TOS code */
#define ACTION_RETURN   1               /* Return from GEMDOS */
#define ACTION_RDMEM    2               /* Read from memory */
#define ACTION_WRMEM    3               /* Write to memory */
#define ACTION_WRMEM0   4               /* Write to memory then return 0 */
#define ACTION_GEMDOS   5               /* GEMDOS call */
#define ACTION_MODSTACK 6               /* modify calling stack and fallback */

/* GEMDOS file attribute flags */
#define FA_READONLY 0x01                /* Include files which are read-only */
#define FA_HIDDEN   0x02                /* Include hidden files */
#define FA_SYSTEM   0x04                /* Include system files */
#define FA_VOLUME   0x08                /* Include volume labels */
#define FA_DIR      0x10                /* Include subdirectories */
#define FA_ARCHIVE  0x20                /* Include files with archive bit set */

/* debugging messages */
//#define DEBUG
#ifdef DEBUG
# define DPRINTF(fmt,...) printf(fmt,##__VA_ARGS__)
#else
# define DPRINTF(fmt,...) // do nothing
#endif

static unsigned int presblk;            /* Address of resblk buffer in ST memory */
static uint8_t action[512*DMABUFSZ];    /* Action buffer */
static uint8_t *result;                 /* Pointer to returned result data */

struct _dta {
  int8_t    d_reserved[21];             /* Reserved for GEMDOS */
  uint8_t   d_attrib;                   /* File attributes */
  uint8_t   d_time[2];                  /* Time */
  uint8_t   d_date[2];                  /* Date */
  uint8_t   d_length[4];                /* File length */
  char      d_fname[14];                /* Filename */
} dta;
static unsigned int addr_dta;
static unsigned int gemdos_drv;         /* 0:A,1:B etc */
static unsigned int current_drv;        /* 0:A,1:B etc */
static char current_path[1024];

extern void acsi_wait_data(void *data, int n_bytes);
extern void acsi_send_reply(const void *data, int size);

extern const unsigned char gdboot_img[];

extern volatile uint32_t *acsireg;
extern volatile uint32_t *iobuf;

static struct __gemdos_disk {
  // int fd;           // disk image file handle
  // int sectors;      // number of sectors
  // unsigned int lba; // current logical block address
  unsigned int sense;
  // int report_lba;   // report LBA in sense data
} gemdos_disk;

static unsigned int read_u32(const unsigned char *p) {
  unsigned int a = *p++;
  unsigned int b = *p++;
  unsigned int c = *p++;
  unsigned int d = *p++;
  return a<<24 | b<<16 | c<<8 | d;
}

static int read_i32(const unsigned char *p) {
  int a = *(signed char*)p;
  unsigned int b = *++p;
  unsigned int c = *++p;
  unsigned int d = *++p;
  return a<<24 | b<<16 | c<<8 | d;
}

static unsigned int read_u16(const unsigned char *p) {
  unsigned int a = *p++;
  unsigned int b = *p++;
  return a<<8 | b;
}

static void write_u32(unsigned char *p, unsigned int x) {
  *p++ = x>>24;
  *p++ = x>>16;
  *p++ = x>>8;
  *p++ = x;
}

static void write_u16(unsigned char *p, unsigned int x) {
  *p++ = x>>8;
  *p++ = x;
}

static void clear_sense_data(void) {
  gemdos_disk.sense = 0;
}

static unsigned int gemdos_opcode;

extern volatile int thr_end;
static pthread_mutex_t mut = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_t thread;

// convert errno value to GEMDOS error code
static int gemdos_error_code(void) {
  switch (errno) {
    case ENOENT:
      return -33;   // EFILNF
    case ENOTDIR:
      return -34;   // EPTHNF
    case EBUSY:
    case EEXIST:
    case EIO:
    case EINVAL:
    case EISDIR:
    case EPERM:
    case EACCES:
    case EROFS:
      return -36;   // EACCDN
    case EBADF:
      return -37;   // EIHNDL
    default:
      return -65;   // EINTRN
  }
}

static int gemdos_cond_wait(int timeout_ms, const char *from) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME_COARSE,&ts);
  uint64_t tm = ts.tv_sec*1000ull + ts.tv_nsec/1000000 + timeout_ms;
  ts.tv_sec = tm/1000;
  ts.tv_nsec = (tm%1000)*1000000;
  int retval = pthread_cond_timedwait(&cond,&mut,&ts);
  if (retval!=0) {
    if (retval==ETIMEDOUT) {
      //printf("%s: gemdos_cond_wait timed out\n",from);
    } else {
      printf("%s: gemdos_cond_wait error\n",from);
    }
  }
  return retval;
}

// terminate ACSI command, signaling no action is required
static void no_action_required(void) {
  *acsireg = STATUS_OK;
}

// terminate ACSI command, signaling some action is required
static void action_required(void) {
  *acsireg = STATUS_ERROR;
}

// convert POSIX UTC time to DOS time/date
static void u2d_time(time_t time, unsigned int *ptime, unsigned int *pdate) {
  struct tm tm_info;
  time += 3600*(config.timezone-12);
  localtime_r(&time,&tm_info);
  *ptime = tm_info.tm_hour<<11 | tm_info.tm_min<<5 | tm_info.tm_sec>>1;
  *pdate = (tm_info.tm_year-80)<<9 | (tm_info.tm_mon+1)<<5 | tm_info.tm_mday;
}

// convert DOS time/date to POSIX UTC
static time_t d2u_time(unsigned int time, unsigned int date) {
  struct tm tm_info = {0};
  tm_info.tm_sec = (time&0x1f)<<1;
  tm_info.tm_min = (time&0x7e0)>>5;
  tm_info.tm_hour = (time&0xf800)>>11;
  tm_info.tm_mday = date&0x1f;
  tm_info.tm_mon = ((date&0x1e0)>>5)-1;
  tm_info.tm_year = ((date&0xfe00)>>9)+80;
  tm_info.tm_gmtoff = 3600*(config.timezone-12);
  return mktime(&tm_info);
}

// get bytes from address (stub must be in action mode)
// set nbytes to 0 if reading a null-terminated string
static void gemdos_read_memory(unsigned char *buf, unsigned int addr, unsigned int nbytes) {
  result = buf;
  // wait for stub to perform an OP_ACTION command
  if (gemdos_cond_wait(500,"gemdos_read_memory")!=0) return;
  write_u16(action,ACTION_RDMEM);
  write_u32(action+2,addr);
  write_u16(action+6,nbytes);
  acsi_send_reply(action,16);

  // wait for OP_RESULT command and sectors from DMA
  if (gemdos_cond_wait(500,"gemdos_read_memory 2")!=0) return;
  *acsireg = STATUS_OK;
  //return (const uint8_t*)iobuf;
}

// write bytes to address (stub must be in action mode) (generic function)
// ret0: if nonzero, terminate the action loop, having GEMDOS return 0
static void gemdos_write_memory_generic(const void *buf, unsigned int addr, unsigned int nbytes, int ret0) {
  // wait for stub to perform an OP_ACTION command
  if (gemdos_cond_wait(500,"gemdos_write_memory")!=0) return;
  write_u16(action,ret0?ACTION_WRMEM0:ACTION_WRMEM);
  write_u32(action+2,addr);
  write_u16(action+6,nbytes);
  memcpy(action+8,buf,nbytes);
  acsi_send_reply(action,(8+nbytes+15)&-16);
}

// write bytes to address (stub must be in action mode)
static void gemdos_write_memory(const void *buf, unsigned int addr, unsigned int nbytes) {
  gemdos_write_memory_generic(buf,addr,nbytes,0);
}

// write bytes to address (stub must be in action mode)
static void gemdos_write_memory0(const void *buf, unsigned int addr, unsigned int nbytes) {
  gemdos_write_memory_generic(buf,addr,nbytes,1);
}

// write a long word to address (stub must be in action mode)
static void gemdos_write_long(unsigned int addr, unsigned int val) {
  uint8_t buf[4];
  write_u32(buf,val);
  gemdos_write_memory(buf,addr,4);
}

static const char *gemdos_read_string(unsigned int addr) {
  gemdos_read_memory(NULL,addr,0);
  return (const char*)iobuf;
}

static unsigned int gemdos_read_long(unsigned int addr) {
  gemdos_read_memory(NULL,addr,4);
  return read_u32((uint8_t*)iobuf);
}

static unsigned int gemdos_printstr(const char *str) {
  // wait for stub to perform an OP_ACTION command
  if (gemdos_cond_wait(500,"gemdos_printstr")!=0) return -1;
  int len = strlen(str);
  write_u16(action,ACTION_GEMDOS);
  write_u16(action+2,6);
  write_u16(action+4,9);
  write_u32(action+6,presblk+10);
  memcpy(action+10,str,len);
  memcpy(action+10+len,"\r\n",3);
  acsi_send_reply(action,10+len+3);

  // wait for OP_RESULT command and sectors from DMA

  if (gemdos_cond_wait(500,"gemdos_printstr 2")!=0) return -1;
  *acsireg = STATUS_OK;
  return read_i32((uint8_t*)iobuf);
}

static void gemdos_printf(const char *fmt,...) {
  char buf[256];
  va_list ap;
  va_start(ap,fmt);
  vsnprintf(buf,sizeof(buf),fmt,ap);
  va_end(ap);
  gemdos_printstr(buf);
}

// finish action loop with fall back to GEMDOS
static void gemdos_fallback(void) {
  write_u16(action,ACTION_FALLBACK);
  // wait for stub to perform an OP_ACTION command
  if (gemdos_cond_wait(500,"gemdos_fallback")!=0) return;
  acsi_send_reply(action,16);
}

// finish action loop terminating the GEMDOS call
static void gemdos_return(int val) {
  write_u16(action,ACTION_RETURN);
  write_u32(action+2,val);
  // wait for stub to perform an OP_ACTION command
  if (gemdos_cond_wait(500,"gemdos_return")!=0) return;
  acsi_send_reply(action,16);
}

// Compare a unix file name and a DOS file name
// returns 0 if names match
static int namecmp(const char *uname, const char *dname) {
  return strcasecmp(uname,dname);
}

// Look for file with name in directory
// returns 0 if file is found
static int filename_lookup(char *dest, const char *dir, const char *fname) {
  char search_path[1024];
  struct stat st;
  strcpy(search_path,dir);
  int len = strlen(search_path);
  search_path[len] = '/';
  strcpy(search_path+len+1,fname);
  if (!stat(search_path,&st)) {
    // found path
    strcpy(dest,fname);
    return 0;
  }
  // exact name does not exist; look for corresponding name
  DIR *d = opendir(dir);
  struct dirent *e;
  while ((e=readdir(d))!=NULL) {
    if (!namecmp(e->d_name,fname)) {
      strcpy(dest,e->d_name);
      break;
    }
  }
  closedir(d);
  return e==NULL;
}

// resolve DOS path to UNIX path, according to existing directories on the filesystem
// and current working drive and directory
// path is in form \SOME\DIR (absolute) or SOME\SUBDIR (relative)
// and may be preceded by a drive specification (A:,C:,etc)
// returns:
//  - -2: not on managed drive
//  - -1: path is invalid
//  - 0: path is a valid directory path
//  - 1: path is a valid file path
//  - 2: path contains a valid directory path then an unexisting file
static int path_lookup(char *search_path, char *src) {
  char realname[256];
  if (src[1]==':') {
    if (src[0]-'A'==gemdos_drv) {
      src += 2;
    } else {
      return -2;
    }
  }
  else if (current_drv!=gemdos_drv) {
    return -2;
  }
  if (src[0]=='\\') {
    // absolute path
    strcpy(search_path,config.gemdos);
    ++src;
  } else {
    // relative path
    strcpy(search_path,current_path);
  }
  int len = strlen(search_path);

  // for each directory in the source path, look for the corresponding one on the filesystem
  const char *dirname = strtok(src,"\\");
  struct stat st;
  while (dirname) {
    char *next = strtok(NULL,"\\");
    if (!filename_lookup(realname,search_path,dirname)) {
      search_path[len++] = '/';
      strcpy(search_path+len,realname);
      if (next && (stat(search_path,&st) || (st.st_mode&S_IFMT)!=S_IFDIR)) {
        return -1;
      }
      len += strlen(realname);
    } else if (next) {
      return -1;
    } else {
      // file not found: create a new file name
      search_path[len++] = '/';
      char *d = search_path+len;
      const char *s = dirname;
      while ((*d++=tolower(*s++)));
    }
    dirname = next;
  }
  // directory is valid. now, special treatment in last name of the file
  if (stat(search_path,&st)) {
    // file does not exist
    return 2;
  }
  if ((st.st_mode&S_IFMT)!=S_IFDIR) {
    // not a directory
    return 1;
  }
  // valid directory
  return 0;
}

// Dsetpath
static void Dsetpath(unsigned int ppath) {
  char path_host[1024];
  char path_gemdos[1024];
  action_required();
  strncpy(path_gemdos,gemdos_read_string(ppath),sizeof path_gemdos);
  DPRINTF("Dsetpath(\"%s\")\n",path_gemdos);
  if (current_drv!=gemdos_drv) {
    gemdos_fallback();
    return;
  }
  int retval = path_lookup(path_host,path_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  }
  if (retval==-1) {
    // invalid path
    gemdos_return(-34);   // EPTHNF
    return;
  }
  if (retval==1 || retval==2) {
    // directory found or missing file
    gemdos_return(-33);   // EFILNF
    return;
  }
  strncpy(current_path,path_host,sizeof current_path);
  gemdos_return(0);   // EFILNF
}

struct _file_search {
  char path[512];
  size_t path_len;
  char pattern[256];
  unsigned int attr;
  int first;
  DIR *d;
};

static void Pexec(int mode, unsigned int pname, unsigned int pcmdline, int penv) {
  char path_gemdos[1024],path_host[1024],cmdline[1024],env[1024];
  char printedenv[1026];
  action_required();
  if (penv==0)
    strcpy(printedenv,"0");
  else if (penv==-1)
    strcpy(printedenv,"-1");
  else {
    strncpy(env,gemdos_read_string(penv),1024);
    snprintf(printedenv,sizeof printedenv,"\"%s\"",env);
  }

  switch (mode) {
    case 0:
    case 3:
      strncpy(path_gemdos,gemdos_read_string(pname),1024);
      strncpy(cmdline,gemdos_read_string(pcmdline),1024);
      DPRINTF("Pexec(%d,\"%s\",\"%s\",%s)\n",mode,path_gemdos,cmdline,printedenv);
      int retval = path_lookup(path_host,path_gemdos);
      if (retval==-2) {
        // not on managed drive
        gemdos_fallback();
        return;
      }
      if (retval==-1) {
        // invalid path
        gemdos_return(-34);   // EPTHNF
        return;
      }
      if (retval==0 || retval==2) {
        // directory found or missing file
        gemdos_return(-33);   // EFILNF
        return;
      }
      write_u16(action,ACTION_GEMDOS);
      write_u16(action+2,16);
      write_u16(action+4,0x4b);     // Pexec
      write_u16(action+6,5);        // mode 5: create basepage
      write_u32(action+8,0);
      write_u32(action+12,pcmdline);
      write_u32(action+16,penv);
      // wait for stub to perform an OP_ACTION command
      if (gemdos_cond_wait(500,"load_prg")!=0) return;
      acsi_send_reply(action,20);

      // wait for OP_RESULT command and sectors from DMA
      if (gemdos_cond_wait(500,"load_prg 2")!=0) return;
      *acsireg = STATUS_OK;
      unsigned int pbasepage = read_u32((uint8_t*)iobuf);

      // create program environment
      int fd = open(path_host,O_RDONLY);
      int size = lseek(fd,0,SEEK_END);
      lseek(fd,0,SEEK_SET);
      uint8_t *progbuf = malloc(256-28+size);
      gemdos_read_memory(progbuf,pbasepage,256);
      unsigned char header[28];
      read(fd,header,28);
      read(fd,progbuf+256,size-28);
      close(fd);
      // update DTA
      int sz_text = read_u32(header+2);
      int sz_data = read_u32(header+6);
      int sz_bss = read_u32(header+10);
      int sz_sym = read_u32(header+14);
      unsigned int ptr = pbasepage + 256;
      write_u32(progbuf+8,ptr);         // program section address
      write_u32(progbuf+12,sz_text);    // program section size
      ptr += sz_text;
      write_u32(progbuf+16,ptr);        // data section address
      write_u32(progbuf+20,sz_data);    // data section size
      ptr += sz_data;
      write_u32(progbuf+24,ptr);        // BSS section address
      write_u32(progbuf+28,sz_bss);     // BSS section size
      ptr = pbasepage + 256;
      // relocate
      if (read_u16(header+26)==0) {
        uint8_t *rdat = progbuf+256+sz_text+sz_data+sz_sym;
        uint8_t *dest = progbuf+256;
        unsigned int offset = read_u32(rdat);
        dest += offset;
        rdat += 4;
        while (offset) {
          write_u32(dest,read_u32(dest)+ptr);
          do {
            offset = *rdat++;
            dest += (offset==1)?254:offset;
          } while (offset==1);
        }
      }
      // clear BSS
      unsigned int length = 256+sz_text+sz_data+sz_bss;
      progbuf = realloc(progbuf,length);
      memset(progbuf+256+sz_text+sz_data,0,sz_bss);
      // send the program block
      const uint8_t *src = progbuf;
      uint8_t buf[512*DMABUFSZ*2];
      int buf_id = 0;
      const int blksz = 512*DMABUFSZ-8;
      unsigned int addr = pbasepage;
      while (length>0) {
        uint8_t *pbuf = buf+512*DMABUFSZ*buf_id;
        unsigned int n = length<blksz?length:blksz;
        memcpy(pbuf+8,src,n);
        // wait for stub to perform an OP_ACTION command
        if (gemdos_cond_wait(500,"load_prg 3")!=0) return;
        write_u16(pbuf,ACTION_WRMEM);
        write_u32(pbuf+2,addr);
        write_u16(pbuf+6,n);
        acsi_send_reply(pbuf,8+n);
        src += n;
        addr += n;
        length -= n;
        buf_id = 1-buf_id;
      }
      free(progbuf);
      if (mode==3) {
        gemdos_return(pbasepage);
        break;
      }
      // default DTA address
      addr_dta = pbasepage+0x80;
      // patch stack
      write_u16(action,ACTION_MODSTACK);
      write_u16(action+2,16);
      write_u16(action+4,0x4b);   // Pexec
      write_u16(action+6,4);      // mode
      write_u32(action+8,0);
      write_u32(action+12,pbasepage);
      write_u32(action+16,0);
      // wait for stub to perform an OP_ACTION command
      if (gemdos_cond_wait(500,"load_prg 4")!=0) return;
      acsi_send_reply(action,20);
      break;
    case 5:
    case 7:
      strncpy(cmdline,gemdos_read_string(pcmdline),1024);
      DPRINTF("Pexec(%d,%#x,\"%s\",%s)\n",mode,pname,cmdline,printedenv);
      gemdos_fallback();
      break;
    default:
      DPRINTF("Pexec(%d,%#x,%#x,%s)\n",mode,pname,pcmdline,printedenv);
      if (mode==4 || mode==6) {
        // default DTA address
        addr_dta = pcmdline+0x80;
      }
      gemdos_fallback();
  }
}

// check whether a file name matches a search pattern
static int match_dos_pattern(const char *pattern, const char *string) {
  if (!pattern || !string)
    return 0;

  const char *p = pattern;
  const char *s = string;

  while (*p) {
    if (*p == '*') {
      while (*p == '*')
        p++;
      while (*s&&*s!='.') {
        if (match_dos_pattern(p, s))
          return 1;
        s++;
      }
      if (!s)
        return 1;
    } else if (*p == '?') {
      if (!*s)
        return 0;
      p++;
      s++;
    } else {
      if (*p=='.'&&!strcmp(p,".*"))
        return 1;
      if (tolower(*p) != tolower(*s))
        return 0;
      p++;
      s++;
    }
  }

  return !*s;
}


// search for next file (Fsfirst/Fsnext)
static void next_file(void) {
  struct _file_search *fs;
  if (memcmp(dta.d_reserved,"zeST",4) || memcmp(dta.d_reserved+12,"zeST",4)) {
    gemdos_fallback();
    return;
  }
  memcpy(&fs,dta.d_reserved+4,sizeof(fs));
  if (!fs) {
    gemdos_fallback();
    return;
  }
  struct dirent *e;
  int match = 0;
  while (match==0) {
    e = readdir(fs->d);
    if (e==NULL) {
      closedir(fs->d);
      memset(dta.d_reserved,0,16);
      gemdos_write_memory(&dta,addr_dta,16);
      gemdos_return(fs->first?-33:-49);   // EFILNF/ENMFIL
      free(fs);
      return;
    }
    if (match_dos_pattern(fs->pattern,e->d_name)) {
      if (e->d_type==DT_REG)
        match = 1;
      else if (e->d_type==DT_DIR)
        match = fs->attr&FA_DIR;
      if (match) {
        // filename verification
        int len = strlen(e->d_name);
        const char *dot = strchr(e->d_name,'.');
        if (dot&&strcmp(e->d_name,"..")) {
          // check the filename has only one dot and respects 8.3 format
          int baselen = dot-e->d_name;
          if (strchr(dot+1,'.') || baselen>8 || len-baselen>4)
            match = 0;
        } else if (len>8) {
          match = 0;
        }
      }
    }
  }
  fs->first = 0;
  // copy and convert the name to upper case
  const char *s = e->d_name;
  char *d = dta.d_fname;
  while ((*d++=toupper(*s++))!=0);
  struct stat st;
  fs->path[fs->path_len] = '/';
  strcpy(fs->path+fs->path_len+1,e->d_name);
  stat(fs->path,&st);
  fs->path[fs->path_len] = '\0';
  write_u32(dta.d_length,st.st_size);

  unsigned int time,date;
  u2d_time(st.st_mtim.tv_sec,&time,&date);
  write_u16(dta.d_time,time);
  write_u16(dta.d_date,date);
  unsigned int attrib = 0;
  if (e->d_type==DT_DIR) attrib = 16;
  dta.d_attrib = attrib;

  gemdos_write_memory0(((void*)&dta)+20,addr_dta+20,sizeof(struct _dta)-20);
}

// Fsfirst call
static void Fsfirst(unsigned int pname, unsigned int attr) {
  char path_gemdos[1024];
  char path_host[1024];
  char pattern[256];
  action_required();
  strncpy(path_gemdos,gemdos_read_string(pname),sizeof path_gemdos);
  DPRINTF("Fsfirst(\"%s\",%d)\n",path_gemdos,attr);

  // separate pattern from path
  char *pos = strrchr(path_gemdos,'\\');
  char *path;
  if (pos==NULL) {
    strcpy(pattern,path_gemdos);
    path = "";
  } else {
    strcpy(pattern,pos+1);
    if (pos-path_gemdos==2) {
      // path+pattern of the form "C:\*.*" -> keep the "\"
      ++pos;
    }
    *pos++ = '\0';
    path = path_gemdos;
  }

  int retval = path_lookup(path_host,path);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  } else if (retval==-1 || retval>0) {
    // invalid path
    gemdos_return(-33);   // EFILNF
    return;
  }

  // fill DTA with the correct data
  struct _file_search *fs = malloc(sizeof(struct _file_search));
  if (!fs) {
    gemdos_fallback();
    return;
  }
  strcpy(fs->path,path_host);
  fs->path_len = strlen(path_host);
  strcpy(fs->pattern,pattern);
  fs->d = opendir(path_host);
  fs->attr = attr;
  fs->first = 1;
  memcpy(dta.d_reserved,"zeST",4);
  memcpy(dta.d_reserved+4,&fs,sizeof fs);
  memcpy(dta.d_reserved+12,"zeST",4);
  gemdos_write_memory(&dta,addr_dta,16);

  next_file();
}

static void Fsnext(void) {
  DPRINTF("Fsnext()\n");
  action_required();
  next_file();
}

static void Fsetdta(unsigned int addr) {
  if (addr_dta!=addr) {
    action_required();
    gemdos_read_memory(NULL,addr,sizeof(struct _dta));
    memcpy(&dta,(void*)iobuf,sizeof(struct _dta));
    addr_dta = addr;
    gemdos_fallback();
  } else {
    no_action_required();
  }
}


// on Fopen, open the corresponding file and return a custom handle
// that equals to 0x7a00 + host handle
static void Fopen(unsigned int pname, unsigned int mode) {
  char path_gemdos[1024];
  char path_host[1024];
  static const int opmode[] = { O_RDONLY, O_WRONLY, O_RDWR };
  action_required();
  strncpy(path_gemdos,gemdos_read_string(pname),sizeof path_gemdos);
  DPRINTF("Fopen(\"%s\",%d)\n",path_gemdos,mode);

  int retval = path_lookup(path_host,path_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  }
  if (retval==-1) {
    // invalid path
    gemdos_return(-34);   // EPTHNF
    return;
  }
  if (retval==0 || retval==2) {
    // directory found or missing file
    gemdos_return(-33);   // EFILNF
    return;
  }
  if ((mode&7)>2) {
    // invalid access mode
    gemdos_return(-36);   // EACCDN
    return;
  }
  int handle = open(path_host,opmode[mode&7]);
  if (handle==-1) {
    // file not found. should not happen
    gemdos_return(-33);   // EFILNF
    return;
  }
  // return our custom handle
  gemdos_return(0x7a00+handle);
}

static void Fcreate(unsigned int pname, unsigned int attr) {
  char path_gemdos[1024];
  char path_host[1024];
  action_required();
  strncpy(path_gemdos,gemdos_read_string(pname),sizeof path_gemdos);
  DPRINTF("Fcreate(\"%s\",%#x)\n",path_gemdos,attr);

  int retval = path_lookup(path_host,path_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  }
  if (retval==-1) {
    // invalid path
    gemdos_return(-34);   // EPTHNF
    return;
  }
  if (retval==0) {
    // directory found in place of the file
    gemdos_return(-36);   // EACCDN
    return;
  }
  int handle = open(path_host,O_CREAT|O_WRONLY|O_TRUNC);
  if (handle==-1) {
    // access denied
    gemdos_return(-36);   // EACCDN
    return;
  }
  // return our custom handle
  gemdos_return(0x7a00+handle);
}

static void Fclose(int handle) {
  DPRINTF("Fclose(%d)\n",handle);
  if (handle<0x7a00) {
    // not locally managed file
    no_action_required();
    return;
  }
  action_required();
  int retval = close(handle-0x7a00);
  if (retval==0) {
    gemdos_return(0);
    return;
  }
  gemdos_return(gemdos_error_code());
}

static void Fread(int handle, unsigned int length, unsigned int addr) {
  DPRINTF("Fread(%d,%d,%#x)\n",handle,length,addr);
  if (handle<0x7a00) {
    // not locally managed file
    no_action_required();
    return;
  }
  action_required();
  uint8_t buf[512*DMABUFSZ*2];
  int buf_id = 0;
  const int blksz = 512*DMABUFSZ-8;
  int nread = 0;

  while (length>0) {
    uint8_t *pbuf = buf+512*DMABUFSZ*buf_id;
    unsigned int n = length<blksz?length:blksz;
    int rdb = read(handle-0x7a00,pbuf+8,n);
    if (rdb==0) {
      // end of file
      break;
    }
    if (rdb==-1) {
      gemdos_return(gemdos_error_code());
      return;
    }
    // wait for stub to perform an OP_ACTION command
    if (gemdos_cond_wait(500,"Fread")!=0) return;
    write_u16(pbuf,ACTION_WRMEM);
    write_u32(pbuf+2,addr);
    write_u16(pbuf+6,rdb);
    acsi_send_reply(pbuf,(8+rdb+15)&-16);
    nread += rdb;
    addr += rdb;
    length -= rdb;
    buf_id = 1-buf_id;
  }
  gemdos_return(nread);
}

static void Fwrite(int handle, unsigned int length, unsigned int addr) {
  DPRINTF("Fwrite(%d,%d,%#x)\n",handle,length,addr);
  if (handle<0x7a00) {
    // not locally managed file
    no_action_required();
    return;
  }
  action_required();

  unsigned char buf[512*DMABUFSZ];
  int nwritten = 0;

  while (length>0) {
    unsigned int n = length<sizeof buf?length:sizeof buf;
    gemdos_read_memory(buf,addr,n);
    int wrb = write(handle-0x7a00,buf,n);
    if (wrb==-1) {
      gemdos_return(gemdos_error_code());
      return;
    }
    nwritten += wrb;
    addr += wrb;
    length -= wrb;
  }
  gemdos_return(nwritten);
}

static void Fdelete(unsigned int pname) {
  char path_gemdos[1024];
  char path_host[1024];
  action_required();
  strncpy(path_gemdos,gemdos_read_string(pname),sizeof path_gemdos);
  DPRINTF("Fdelete(\"%s\")\n",path_gemdos);

  int retval = path_lookup(path_host,path_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  }
  if (retval==-1||retval==2) {
    // invalid path or file not existing
    gemdos_return(-34);   // EPTHNF
    return;
  }
  if (retval==0) {
    // directory found in place of the file
    gemdos_return(-36);   // EACCDN
    return;
  }
  retval = unlink(path_host);
  if (retval==-1) {
    gemdos_return(gemdos_error_code());
    return;
  }
  gemdos_return(0);
}

static void Fseek(int offset, int handle, int mode) {
  DPRINTF("Fseek(%d,%d,%d)\n",offset,handle,mode);
  if (handle<0x7a00) {
    // not locally managed file
    no_action_required();
    return;
  }
  action_required();
  int whence = -1;
  if (mode==0) whence = SEEK_SET;
  else if (mode==1) whence = SEEK_CUR;
  else if (mode==2) whence = SEEK_END;
  else {
    gemdos_return(-36);   // EACCDN
    return;
  }
  off_t off = lseek(handle-0x7a00,offset,whence);
  if (off==-1) {
    gemdos_return(gemdos_error_code());
    return;
  }
  gemdos_return(off);
}

static void Dgetpath(unsigned int ppath, unsigned int drive) {
  char path_gemdos[1024];
  DPRINTF("Dgetpath(%#x,%d)\n",ppath,drive);
  if ((drive==0&&current_drv!=gemdos_drv) || (drive>0&&drive-1!=gemdos_drv)) {
    no_action_required();
    return;
  }
  action_required();
  const char *src = current_path+strlen(config.gemdos);
  while (*src=='/') ++src;
  char *dest = path_gemdos;
  *dest++ = '\\';
  char c;
  while ((c=*src++)) {
    if (c=='/') c = '\\';
    else c = toupper(c);
    *dest++ = c;
  }
  *dest++ = 0;
  gemdos_write_memory(path_gemdos,ppath,strlen(path_gemdos)+1);
  gemdos_return(0);
}

static void Dfree(unsigned int diskinfo_addr, unsigned int drive) {
  unsigned char diskinfo[16];
  DPRINTF("Dfree(%#x,%d)\n",diskinfo_addr,drive);
  if ((drive==0&&current_drv!=gemdos_drv) || (drive>0&&drive-1!=gemdos_drv)) {
    no_action_required();
    return;
  }
  action_required();
  struct statvfs buf;
  if (statvfs(config.gemdos,&buf)) {
    gemdos_return(-65); return; // EINTRN
  }
  // limit free size to a positive, signed 32 bit number
  unsigned int max = 0x7fffffff/buf.f_bsize;
  write_u32(diskinfo,buf.f_bfree<=max?buf.f_bfree:max);
  write_u32(diskinfo+4,buf.f_blocks);
  write_u32(diskinfo+8,512);
  write_u32(diskinfo+12,buf.f_bsize/512);

  gemdos_write_memory(diskinfo,diskinfo_addr,sizeof diskinfo);
  gemdos_return(0);
}

static void Dcreate(unsigned int pname) {
  char path_gemdos[1024];
  char path_host[1024];
  action_required();
  strncpy(path_gemdos,gemdos_read_string(pname),sizeof path_gemdos);
  DPRINTF("Dcreate(\"%s\")\n",path_gemdos);
  int retval = path_lookup(path_host,path_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  }
  if (retval==-1) {
    // invalid path
    gemdos_return(-34);   // EPTHNF
    return;
  }
  if (retval==0||retval==1) {
    // directory or file found in place of the file
    gemdos_return(-36);   // EACCDN
    return;
  }
  retval = mkdir(path_host,0777);
  if (retval==-1) {
    gemdos_return(gemdos_error_code());
    return;
  }
  gemdos_return(0);
}

static void Ddelete(unsigned int pname) {
  char path_gemdos[1024];
  char path_host[1024];
  action_required();
  strncpy(path_gemdos,gemdos_read_string(pname),sizeof path_gemdos);
  DPRINTF("Ddelete(\"%s\")\n",path_gemdos);
  int retval = path_lookup(path_host,path_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  }
  if (retval==-1) {
    // invalid path
    gemdos_return(-34);   // EPTHNF
    return;
  }
  if (retval==1) {
    // regular file found
    gemdos_return(-36);   // EACCDN
    return;
  }
  if (retval==2) {
    // file not existing
    gemdos_return(-34);   // EPTHNF
    return;
  }
  retval = rmdir(path_host);
  if (retval==-1) {
    gemdos_return(gemdos_error_code());
    return;
  }
  gemdos_return(0);
}

static void Frename(unsigned int poldname,unsigned int pnewname) {
  char oldname_gemdos[1024];
  char newname_gemdos[1024];
  char oldname_host[1024];
  char newname_host[1024];
  action_required();
  strncpy(oldname_gemdos,gemdos_read_string(poldname),512);
  strncpy(newname_gemdos,gemdos_read_string(pnewname),512);
  DPRINTF("Frename(\"%s\",\"%s\")\n",oldname_gemdos,newname_gemdos);
  int retval = path_lookup(oldname_host,oldname_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_fallback();
    return;
  }
  if (retval==-1 || retval==2) {
    // invalid path or file not existing
    gemdos_return(-34);   // EPTHNF
    return;
  }
  retval = path_lookup(newname_host,newname_gemdos);
  if (retval==-2) {
    // not on managed drive
    gemdos_return(-48);   // ENSAME
    return;
  }
  if (retval==-1) {
    // invalid path
    gemdos_return(-34);   // EPTHNF
    return;
  }
  if (retval==0 || retval==1) {
    // directory or file already existing
    gemdos_return(-36);   // EACCDN
    return;
  }
  retval = rename(oldname_host,newname_host);
  if (retval==-1) {
    gemdos_return(gemdos_error_code());
    return;
  }
  gemdos_return(0);
}

static void Fdatime(unsigned int timeptr, int handle, int wflag) {
  DPRINTF("Fdatime(%#x,%d,%d)\n",timeptr,handle,wflag);
  if (handle<0x7a00) {
    // not locally managed file
    no_action_required();
    return;
  }
  action_required();
  uint8_t buf[4];
  if (wflag==0) {
    struct stat st;
    unsigned int time,date;
    fstat(handle-0x7a00,&st);
    u2d_time(st.st_mtim.tv_sec,&time,&date);
    write_u16(buf,time);
    write_u16(buf+2,date);
    gemdos_write_memory(buf,timeptr,4);
    gemdos_return(0);
  } else if (wflag==1) {
    struct timespec ts[2];
    gemdos_read_memory(buf,timeptr,4);
    ts[0].tv_sec = d2u_time(read_u16(buf),read_u16(buf+2));
    ts[0].tv_nsec = 0;
    ts[1] = ts[0];
    futimens(handle-0x7a00,ts);
  }
  gemdos_return(0);
}

// Called by stub at initialisation
static void drive_init(unsigned int begin_adr, unsigned int resblk_adr) {
  presblk = resblk_adr;
  action_required();
  unsigned int drvbits = gemdos_read_long(0x4c2);
  DPRINTF("Driver init, begin:%#x, resblk:%#x, size:%u, drvbits:%u\n",begin_adr,resblk_adr,resblk_adr-begin_adr+28+512*DMABUFSZ,drvbits);
  gemdos_drv = 2;
  while (drvbits&(1<<gemdos_drv)) ++gemdos_drv;
  gemdos_write_long(0x4c2,drvbits|(1<<gemdos_drv));
  gemdos_printf("GEMDOS drive installed as drive %c:",'A'+gemdos_drv);
  if (gemdos_drv==2) {
    // if drive is C:, set it as the current drive
    current_drv = 2;
  }
  gemdos_fallback();
}

static void *gemdos_thread(void *ptr) {
  unsigned char *buf = (unsigned char*)iobuf;
  int i;
  pthread_mutex_lock(&mut);
  while (!thr_end) {
    if (gemdos_cond_wait(200,"gemdos_thread")==0) {
      switch (gemdos_opcode) {
      case 0x0e:  // Dsetdrv
        current_drv = read_u16(buf+2);
        DPRINTF("Dsetdrv(%d)\n",current_drv);
        no_action_required();
        break;
      case 0x19:  // Dgetdrv
        DPRINTF("Dgetdrv()\n");
        no_action_required();
        break;
      case 0x1a:  // Fsetdta
        Fsetdta(read_u32(buf+2));
        break;
      case 0x36:  // Dfree
        Dfree(read_u32(buf+2),read_u16(buf+6));
        break;
      case 0x39:  // Dcreate
        Dcreate(read_u32(buf+2));
        break;
      case 0x3a:  // Ddelete
        Ddelete(read_u32(buf+2));
        break;
      case 0x3b:  // Dsetpath
        Dsetpath(read_u32(buf+2));
        break;
      case 0x3c:  // Fcreate
        Fcreate(read_u32(buf+2),read_u16(buf+6));
        break;
      case 0x3d:  // Fopen
        Fopen(read_u32(buf+2),read_u16(buf+6));
        break;
      case 0x3e:  // Fclose
        Fclose(read_u16(buf+2));
        break;
      case 0x3f:  // Fread
        Fread(read_u16(buf+2),read_u32(buf+4),read_u32(buf+8));
        break;
      case 0x40:  // Fwrite
        Fwrite(read_u16(buf+2),read_u32(buf+4),read_u32(buf+8));
        break;
      case 0x41:  // Fdelete
        Fdelete(read_u32(buf+2));
        break;
      case 0x42:  // Fseek
        Fseek(read_i32(buf+2),read_u16(buf+6),read_u16(buf+8));
        break;
      case 0x43:  // Fattrib
        int wflag = read_u16(buf+6);
        int attrib = read_u16(buf+8);
        action_required();
        DPRINTF("Fattrib(\"%s\".%d,%d)\n",gemdos_read_string(read_u32(buf+2)),wflag,attrib);
        gemdos_fallback();
        break;
      case 0x47:  // Dgetpath
        Dgetpath(read_u32(buf+2),read_u16(buf+6));
        break;
      case 0x4b:  // Pexec
        Pexec(read_u16(buf+2),read_u32(buf+4),read_u32(buf+8),read_u32(buf+12));
        break;
      case 0x4e:  // Fsfirst
        Fsfirst(read_u32(buf+2),read_u16(buf+6));
        break;
      case 0x4f:  // Fsnext
        Fsnext();
        break;
      case 0x56:  // Frename
        Frename(read_u32(buf+4),read_u32(buf+8));
        break;
      case 0x57:  // Fdatime
        Fdatime(read_u32(buf+2),read_u16(buf+6),read_u16(buf+8));
        break;
      case 0xffff:  // driver initialisation
        drive_init(read_u32(buf),read_u32(buf+4));
        break;
      default:
        DPRINTF("gemdos_opcode=%d\n",gemdos_opcode);
        for (i=0;i<16;++i) {
          DPRINTF("%02x ",buf[i]);
        }
        DPRINTF("\n");
      }
    }
  }
  pthread_mutex_unlock(&mut);
  return NULL;
}

// function called by the ACSI interrupt handler when a DMA write is finished
// or when a GEMDOS command is received.
// This allows the GEMDOS thread to wait for write completion or new commands
// using gemdos_cond_wait()
void gemdos_stub_call(void) {
  pthread_mutex_lock(&mut);
  pthread_cond_signal(&cond);
  pthread_mutex_unlock(&mut);
}

void gemdos_acsi_cmd(void) {
  int cmd = acsi_command[0];

  if (cmd==0) {
    // send response, no error
    *acsireg = STATUS_OK;
  }
  else if (cmd==3) {
    // request sense
    uint8_t data[256];
    unsigned int length = acsi_command[4];
    struct __gemdos_disk *img = &gemdos_disk;
    data[0] = 0x70;
    data[2] = img->sense&0x0f;         // sense key
    data[7] = 10;   // additional sense length
    data[12] = (img->sense>>16)&0xff;  // additional sense code
    data[13] = (img->sense>>8)&0xff;   // additional sense code qualifier
    acsi_send_reply(data,length);
    clear_sense_data();
  }
  else if (cmd==8) {
    // read
    unsigned int lba = (acsi_command[1]<<8|acsi_command[2])<<8|acsi_command[3];
    unsigned int n_sectors = acsi_command[4];
    if (lba+n_sectors>4) {
      gemdos_disk.sense = ERROR_INVADDR;
      *acsireg = STATUS_ERROR;
    } else {
      acsi_send_reply(gdboot_img+lba*512,n_sectors*512);
    }
  }
  else if (cmd==0x11) {
    unsigned int op = acsi_command[1];
    if (op==OP_GEMDOS) {
      gemdos_opcode = read_u16(acsi_command+2);
      if (gemdos_opcode==0x19   // Dgetdrv
        || gemdos_opcode==0x4f  // Fsnext
      ) {
        // commands without a data block
        gemdos_stub_call();
      } else
      if (gemdos_opcode==0x0e   // Dsetdrv
        || gemdos_opcode==0x1a  // Fsetdta
        || gemdos_opcode==0x36  // Dfree
        || gemdos_opcode==0x39  // Dcreate
        || gemdos_opcode==0x3a  // Ddelete
        || gemdos_opcode==0x3b  // Dsetpath
        || gemdos_opcode==0x3c  // Fcreate
        || gemdos_opcode==0x3d  // Fopen
        || gemdos_opcode==0x3e  // Fclose
        || gemdos_opcode==0x3f  // Fread
        || gemdos_opcode==0x40  // Fwrite
        || gemdos_opcode==0x41  // Fdelete
        || gemdos_opcode==0x42  // Fseek
        || gemdos_opcode==0x43  // Fattrib
        || gemdos_opcode==0x47  // Dgetpath
        || gemdos_opcode==0x4b  // Pexec
        || gemdos_opcode==0x4e  // Fsfirst
        || gemdos_opcode==0x56  // Frename
        || gemdos_opcode==0x57  // Fdatime
        || gemdos_opcode==0xffff// driver initialisation
      ) {
        // commands with a data block
        acsi_wait_data(NULL,16);
      } else {
        // Ignore commands
        DPRINTF("Ignored: %#x ",gemdos_opcode);
        switch (gemdos_opcode) {
        case 0x20: DPRINTF("Super"); break;
        case 0x31: DPRINTF("Ptermres"); break;
        case 0x48: DPRINTF("Malloc"); break;
        case 0x49: DPRINTF("Mfree"); break;
        case 0x4a: DPRINTF("Mshrink"); break;
        }
        DPRINTF("\n");
        *acsireg = STATUS_OK;
      }
    } else if (op==OP_ACTION) {
      // Request for command
      gemdos_stub_call();
    } else if (op==OP_RESULT) {
      // Action result
      acsi_wait_data(result,read_u16(acsi_command+2));
    } else {
      gemdos_disk.sense = ERROR_INVARG;
      *acsireg = STATUS_ERROR;
    }
  }
  else if (cmd==0x12) {
    // inquiry
    static const uint8_t data[48] =
      "\x0a\x00\x01\x00\x1f\x00\x00\x00"
      "zeST    "
      "GEMDOS_Drive    "
      "0100" "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";
    int alloc = acsi_command[3]<<8 | acsi_command[4];
    if (alloc>48) alloc = 48;
    acsi_send_reply(data,alloc);
  }
}


void gemdos_init(void) {
  pthread_create(&thread,NULL,gemdos_thread,NULL);
}

void gemdos_exit(void) {
  pthread_join(thread,NULL);
}
