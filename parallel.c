/* Atmel AT90S1200 programmer
 * Copyright (C)2004 Berke Durak
 * Released in the public domain.
 * $Id: parallel.c,v 1.9 2004/03/23 15:04:32 berke Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <sys/io.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#define LP 0x378

static int tau = 1;

static inline void microsleep(void)
{
  inb(LP);
}

static inline void udelay(int t_us)
{
  int i;
  for(i = 0; i<tau * t_us; i++) {
    microsleep();
  }
  /*usleep(t_us);*/
}

#define DATI 0x80
/* #define PWR 0x08 */
#define PWR 0x00
#define DATO 0x04
#define PUL 0x02
#define CTR 0x01

#define outb(x,y) outb(0xff^(x),(y))

#define AVR_MISO 0x01
#define AVR_MOSI 0x01
#define AVR_SCLK 0x02
#define AVR_RST  0x04
#define AVR_OUTBITS AVR_MOSI|AVR_SCLK|AVR_RST
#define AVR_INBITS AVR_MISO

void monitor(void)
{
  unsigned char c0,c1;
  outb(0x80,LP);
  c0 = 0;
  c1 = 0;
  for(;;) {
    c1 = inb(LP + 1);
    if(c0 != c1) {
      printf("0x%02x\n", c1);
      c0 = c1;
    }
  }
}

void capture(char *fn)
{
  unsigned char c;
  signed short buffer[4096];
  int i;
  FILE *f;

  f = fopen(fn, "wb");
  if(!f) {
    fprintf(stderr, "Can't open file.\n");
    return;
  }

  outb(0x80,LP);
  i = 0;
  for(;;){
    c = inb(LP + 1);
    buffer[i] = (c & 0x80) ? -32768 : 32767;
    i ++;
    if(i == sizeof(buffer)) {
      i = 0;
      fwrite(buffer, sizeof(signed short), sizeof(buffer), f);
      fflush(f);
    }
    (void) udelay(1);
  }
  fclose(f);
}

unsigned char avr_rxtx(unsigned char x)
{
  unsigned char c;
  outb(x & 7, LP);
  microsleep();
  c = !!(0x80 & inb(LP + 1));
  /* printf("T(0x%02x) R(%d)\n", x, c); */
  return c;
}

#define SAS_START 0xf
#define SAS_DATA_0 0x2
#define SAS_DATA_1 0x6
#define SAS_STOP 0xe

/* Send nibble LSB first. */
static inline void sas_send_nibble(int tau, unsigned char x)
{
  int i;
  unsigned char xor;

  xor = 0x00;
  for(i = 0; i < 4; i++) {
    outb(xor ^ (AVR_RST|((x & 1)?AVR_MOSI:0)), LP);
    udelay(tau);
    outb(xor ^ (AVR_RST|((x & 1)?AVR_MOSI|AVR_SCLK:AVR_SCLK)), LP);
    udelay(tau);
    /* printf("%c",x&1?'1':'0'); fflush(stdout); */
    x >>= 1;
  }
  /* printf(" "); fflush(stdout); */
}

void prototran(int tau, unsigned long x)
{
  unsigned long y;
  int i;
  unsigned char c; /* check byte */
  int ack1,ack2;
  int retries;

  c = - ((x & 0xff) + ((x >> 8) & 0xff) + ((x >> 16) & 0xff) + ((x >> 24) & 0xff));

  ack1 = !!(0x80 & inb(LP + 1));
  for(retries = 0; retries < 500; retries ++) {
    printf("Sending 0x%06lx...\n", x);

    sas_send_nibble(tau, SAS_START);
    sas_send_nibble(tau, SAS_START);
    y = x;
    for(i = 0; i < 32; i++) {
      sas_send_nibble(tau, (y & 1) ? SAS_DATA_1:SAS_DATA_0);
      y >>= 1;
    }
    y = c;
    for(i = 0; i < 8; i++) {
      sas_send_nibble(tau, (y & 1) ? SAS_DATA_1:SAS_DATA_0);
      y >>= 1;
    }
    sas_send_nibble(tau, SAS_STOP);
    ack2 = !!(0x80 & inb(LP + 1));
    if(ack1 != ack2) {
      printf("Command acknowledged.\n");
      break;
    }
  }
  if(retries == 500) {
    printf("ERROR: Command not acknowledged after %d attempts (%d,%d).\n", ack1, ack2, retries);
  }
  outb(AVR_RST, LP);
}
/* Power-up sequence */

void avr_powerup(void) /* with XTAL */
{
  /* Apply power while _RESET and SCK are set to 0. */
  (void) avr_rxtx(0);
  udelay(100);
  /* If the programmer cannot guarantee that SCK is held low during
   * power-up, _RESET must be given a positive pulse after SCK
   * has been set to 0. */
  (void) avr_rxtx(AVR_RST);
  udelay(1000);
  (void) avr_rxtx(0);
  /* Wait at least 20ms */
  udelay(20000);
}

unsigned short avr_talk(unsigned char u1, unsigned char u2, unsigned char u3, unsigned char u4)
{
  int i;
  unsigned char x,z;
  unsigned short res;

  /* printf("talk %02x %02x %02x %02x\n", u1, u2, u3, u4); */
  res = 0;
  x = u1;

  for(i = 0; i<8; i++) {
    if(x & 0x80) {
      (void) avr_rxtx(AVR_MOSI);
      udelay(3);
      (void) avr_rxtx(AVR_MOSI|AVR_SCLK);
      udelay(3);
    } else {
      (void) avr_rxtx(0);
      udelay(3);
      (void) avr_rxtx(AVR_SCLK);
      udelay(3);
    }
    x = (x << 1) & 0xff;
  }
  (void) avr_rxtx(0);

  x = u2;
  for(i = 0; i<8; i++) {
    if(x & 0x80) {
      (void) avr_rxtx(AVR_MOSI);
      udelay(3);
      z = avr_rxtx(AVR_MOSI|AVR_SCLK);
      udelay(3);
    } else {
      (void) avr_rxtx(0);
      udelay(3);
      z = avr_rxtx(AVR_SCLK);
      udelay(3);
    }
    res <<= 1;
    if(z) res|=1;
    x = (x << 1) & 0xff;
  }
  (void) avr_rxtx(0);

  x = u3;
  for(i = 0; i<8; i++) {
    if(x & 0x80) {
      (void) avr_rxtx(AVR_MOSI);
      udelay(3);
      (void) avr_rxtx(AVR_MOSI|AVR_SCLK);
      udelay(3);
    } else {
      (void) avr_rxtx(0);
      udelay(3);
      (void) avr_rxtx(AVR_SCLK);
      udelay(3);
    }
    x = (x << 1) & 0xff;
  }
  (void) avr_rxtx(0);

  x = u4;
  for(i = 0; i<8; i++) {
    if(x & 0x80) {
      (void) avr_rxtx(AVR_MOSI);
      udelay(3);
      z = avr_rxtx(AVR_MOSI|AVR_SCLK);
      udelay(3);
    } else {
      (void) avr_rxtx(0);
      udelay(3);
      z = avr_rxtx(AVR_SCLK);
      udelay(3);
    }
    res <<= 1;
    if (z) res |= 1;
    x = (x << 1) & 0xff;
  }
  (void) avr_rxtx(0);

  return res;
}

int avr_programming_enable()
{
  int i;
  unsigned short res;
  for(i = 0; i < 10; i++) {
    res = avr_talk(0xac,0x53,0x00,0x00);
    /* printf("0x%04x\n",res); */
    if(0xac00 == (res & 0xff00)) {
      return 1;
    }
  }
  printf("No AVR chip found.\n");
  return 0;
}

void avr_dump_program_memory(int low_addr, int m)
{
  unsigned int addr;
  unsigned short x,y;
  unsigned char ck;
  int j;

  for(addr = low_addr; addr < low_addr + m; ) {
    printf(":10%04X00", addr);
    ck = (addr >> 8) + (addr & 0xff) + 0x10;
    for(j = 0; j < 8; j++) {
      x = avr_talk(0x20, (addr >> 9),(addr >> 1) & 0xff,0x00) & 0xff;
      addr ++;
      y = avr_talk(0x28, (addr >> 9),(addr >> 1) & 0xff,0x00) & 0xff;
      addr ++;
      printf("%02X%02X", x, y);
      ck += x + y;
    }
    printf("%02X\n", ((0xff ^ ck) + 1) & 0xff);
  }
}

void avr_dump_program_memory2(int low_addr, int m)
{
  unsigned int addr;
  unsigned short x,y;

  for(addr = low_addr; addr < low_addr + m; addr ++) {
    x = avr_talk(0x20, (addr >> 9),(addr >> 1) & 0xff,0x00);
    y = avr_talk(0x28, (addr >> 9),(addr >> 1) & 0xff,0x00);
    printf("0x%04x %02x%02x\n", addr, x & 0xff, y & 0xff);
  }
}

#define GIVE_UP 10
int avr_write_program_memory(int addr, unsigned short data)
{
  unsigned short res;
  int attempts;

  (void) avr_talk(0x40, 0xff & (addr >> 8), addr & 0xff, data & 0x00ff);
  for(attempts = 0; attempts < GIVE_UP; attempts ++) {
    udelay(5000);
    res = avr_talk(0x20, 0xff & (addr >> 8), addr & 0xff, 0x00);
    if((res & 0xff) == (data & 0xff)) break;
  }
  if(attempts == GIVE_UP) {
    printf("write error : low at 0x%04x wrote 0x%02x reads back as 0x%02x\n", addr, data & 0xff, res & 0xff);
    return 0;
  }

  (void) avr_talk(0x48, 0xff & (addr >> 8), addr & 0xff, (data >> 8) & 0x00ff);
  for(attempts = 0; attempts < GIVE_UP; attempts ++) {
    udelay(5000);
    res = avr_talk(0x28, 0xff & (addr >> 8), addr & 0xff, 0x00);
    if((res & 0xff) == ((data >> 8) & 0xff)) break;
  }
  if(attempts == GIVE_UP) {
    printf("write error : hi at 0x%04x wrote 0x%02x reads back as 0x%02x\n", addr, (data >> 8) & 0xff, res & 0xff);
    return 0;
  }
  return 1;
}

unsigned short avr_write_fuse_bits(unsigned char f_hi, unsigned char f_lo)
{
  printf("Writing fuse bytes: hi=0x%02x lo=0x%02x\n", f_hi, f_lo);
  (void) avr_talk(0xac, 0xa0, 0x00, f_lo);
  (void) avr_talk(0xac, 0xa8, 0x00, f_hi);
  return 1;
}

unsigned short avr_read_lock_bits(FILE *out)
{
  unsigned char l;

  l = avr_talk(0x98, 0x00, 0x00, 0x00) & 0x3f;
  fprintf(out, "Lock byte: 0x%02x\n", l);
  return 1;
}
unsigned short avr_read_fuse_bits(FILE *out)
{
  unsigned char f_lo,f_hi;

  f_lo = avr_talk(0x50, 0x00, 0x00, 0x00) & 0xff;
  f_hi = avr_talk(0x58, 0x08, 0x00, 0x00) & 0xff;
  fprintf(out, "Fuse bytes: hi=0x%02x lo=0x%02x\n", f_hi, f_lo);
  return 1;
}

void avr_chip_erase()
{
  (void) avr_talk(0xac,0x80,0x00,0x00);
  udelay(2000000); /* 20ms delay */
}

unsigned short test1[] = {
  0xc00c, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0xef0f, 0xbb01, 0xbb00, 0xbb02, 0x9503, 0xcffd
};

void avr_program1200(unsigned char *flash, int length, int verify) /* must have been powered-up */
{
  int i;
  printf("Code length is %d (0x%x) bytes.\n", length, length);
  for(i = 0; i<length / 2; i++) {
    printf("\rProgramming byte : 0x%04x", 2*i); fflush(stdout);
    if(!avr_write_program_memory(i,flash[2*i] | (flash[2*i + 1] << 8))) {
      printf("Error, aborting.\n");
      return;
    }
  }
  return;
}

/* program length is in BYTES */
void avr_program_mega8(unsigned char *flash, int length, int page_size, int verify) /* must have been powered-up */
{
  int i, j;
  int pages;
  int this_length;
  unsigned char x;
  int byte_index;
  int tries;
  int okay;
  unsigned char x1, x2;
  unsigned char y1, y2;

  length = length / 2;
  pages = (length + page_size - 1) / page_size;
  printf("Code length is %d (0x%x) words(s), %d page(s).\n", length, length, pages);

  for(j = 0; j < pages; j ++) {
    if(j == pages - 1) {
      this_length = (length % page_size) + 1;
    } else {
      this_length = page_size;
    }
    printf("\nProgramming page %d : %d words (words 0x%04x to 0x%04x):\n",
        j, this_length, j * page_size, (j + 1) * page_size - 1);
    fflush(stdout);


    for(i = 0; i < this_length; i++) {
      byte_index = 2 * (page_size * j + i);
      printf("\rProgramming byte : 0x%04x (byte 0x%02x of page %d)",
                byte_index, 2 * i, j); fflush(stdout);

      /* low byte first */
      x = flash[byte_index];
      (void) avr_talk(0x40, 0x00, i, x);

      x = flash[byte_index + 1];
      (void) avr_talk(0x48, 0x00, i, x);
    }

    /* write page */
    printf("\nWriting page %d.\n", j);
    (void) avr_talk(0x4c, j >> 3, (j & 7) << 5, 0x00);

    /* poll/verify */
    for(tries = 0; tries < 1000; tries ++) {
      udelay(4500);
      okay = 1;
      for(i = 0; i < this_length; i ++) {
        byte_index = 2 * (page_size * j + i);
        x1 = flash[byte_index];
        x2 = avr_talk(0x20, 0xff & (byte_index >> 9), (byte_index >> 1) & 0xff, 0x00);

        if(x1 != x2) {
          printf("At index %d byte 0x%02x reads back as 0x%02x.\n", byte_index, x1, x2);
          okay = 0; break;
        }
        byte_index ++;
        y1 = flash[byte_index];
        y2 = avr_talk(0x28, 0xff & (byte_index >> 9), (byte_index >> 1) & 0xff, 0x00);
        if(y1 != y2) {
          printf("At index %d byte 0x%02x reads back as 0x%02x.\n", byte_index, y1, y2);
          okay = 0; break;
        }
      }
      if(okay) break;
    }
    if(tries == 1000) {
      printf("Error: can't write page %d.\n", j);
      break;
    } else {
      printf("Page %d okay.\n", j);
    }
  }
  return;
}

int intelhex_load(char *fn, unsigned char *flash, int m)
{
  unsigned int x;
  int n;
  int i;
  FILE *f;
  char line[512];
  unsigned char bytes[256];
  int n_bytes;
  int len;
  unsigned char sum;
  int addr;

  f = fopen(fn, "r");
  if(!f) {
    return -1;
  }

  memset(flash, 0, m);
  n = 0;
  for(;;) {
    if(fgets(line, sizeof(line), f)) {
      if(line[0] == ':') {
        for(n_bytes = 0, i = 1; isxdigit(line[i]) && isxdigit(line[i + 1]); i += 2) {
          if(1 == sscanf(line + i, "%02x", &x)) {
            bytes[n_bytes++] = x;
          } else {
            n = -1;
            fprintf(stderr,"intelhex_load: file %s: bad line\n", fn);
            goto bye;
          }
        }
        if(n_bytes >= 2) {
          len = bytes[0];
          if(len + 5 != n_bytes) {
            fprintf(stderr, "intelhex_load: bad record length\n");
            n = -1;
            goto bye;
          }
          sum = 0;
          for(i = 0; i < n_bytes; i++) {
            sum += bytes[i];
          }
          if(sum) {
            fprintf(stderr, "intelhex_load: checksum error, got 0x%02x\n", sum);
            n = -1;
            goto bye;
          }

          switch(bytes[3]) {
            case 0x00: /* data */
              if(len >= 0) {
                addr = (bytes[1] << 8) | bytes[2];
                if(0 <= addr && addr + len < m) {
                  for(i = 0; i < len; i++) {
                    flash[addr + i] = bytes[4+i];
                  }
                  if(addr + len > n) n = addr + len;
                } else {
                  fprintf(stderr, "intelhex_load: address 0x%02x out of range\n", addr);
                }
              } else fprintf(stderr, "intelhex_load: short data record\n");
              break;
            case 0x01: /* eof */
              goto bye;
            default:
              fprintf(stderr, "intelhex_load: unknown record type 0x%02x, ignoring\n", bytes[0]);
              break;
          }
        }
      } else {
        fprintf(stderr,"intelhex_load: ignoring line\n");
      }
    } else break;
  }

bye:
  fclose(f);
  return n;
}

unsigned char avr_read_signature(int i)
{
  return avr_talk(0x03,0x00,i & 3,0x00);
}

void avr_dump_signature(FILE *f)
{
  int i;
  int sigaddrs[] = { 0x0,0x1,0x2 };

  for(i = 0; i<sizeof(sigaddrs)/sizeof(*sigaddrs); i++) {
    fprintf(f, "signature[0x%02x] = 0x%02x\n", sigaddrs[i], avr_read_signature(sigaddrs[i]));
  }
}

int main(int argc, char **argv)
{
  char *fn, *cmd;
  unsigned char flash[8192];
  int n;

  if(argc < 2) {
    fprintf(stderr, "usage: %s command...\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  if(ioperm(LP, 3, 1)) {
    perror("ioperm");
    exit(EXIT_FAILURE);
  }

  cmd = argv[1];

  if(!strcmp(cmd,"prototran")) {
    int tau;
    unsigned long x;

    tau = atoi(argv[2]);
    if (1 == sscanf(argv[3],"%li",&x)) {
      prototran(tau,x);
    } else {
      fprintf(stderr, "Bad integer.\n");
      exit(EXIT_FAILURE);
    }
  } else if(!strcmp(cmd,"powerup")) {
    outb(AVR_RST, LP);
  } else if(!strcmp(cmd,"set")) {
    outb(atoi(argv[2]), LP);
  } else if(!strcmp(cmd,"monitor")) {
    monitor();
  } else if(!strcmp(cmd,"capture")) {
    capture(argv[2]);
  } else if(!strcmp(cmd,"reset")) {
    outb(0, LP);
    udelay(1000000);
    outb(AVR_RST, LP);
  } else if(!strcmp(cmd,"ihexchk")) {
    fn = argv[2];
    n = intelhex_load(fn, flash, sizeof(flash));
    printf("Loaded %d bytes.\n", n);
  } else {
    avr_powerup();
    if(avr_programming_enable()) {
      if(!strcmp(cmd,"erase")) {
        avr_chip_erase();
      } else if(!strcmp(cmd,"unlock")) {
        avr_talk(0xac,0xff,0x00,0x00);
      } else if(!strcmp(cmd,"signature")) {
        avr_dump_signature(stdout);
      } else if(!strcmp(cmd,"readfuse")) {
        avr_read_fuse_bits(stdout);
      } else if(!strcmp(cmd,"readlock")) {
        avr_read_lock_bits(stdout);
      } else if(!strcmp(cmd,"writefuse")) {
        unsigned char f_hi;
        unsigned char f_lo;
        if (argc != 4) {
          fprintf(stderr,"usage: paravr writefuse <fuse_hi> <fuse_lo>\n");
          exit(1);
        }
        f_hi = strtol(argv[2], 0, 0);
        f_lo = strtol(argv[3], 0, 0);
        avr_write_fuse_bits(f_hi, f_lo);
      } else if(!strcmp(cmd,"dump")) {
        avr_dump_program_memory(0,8192);
      } else if(!strcmp(cmd,"1200program")) {
        fn = argv[2];
        n = intelhex_load(fn, flash, sizeof(flash));
        if(n < 0) {
          exit(EXIT_FAILURE);
        }
        printf("Loaded %d bytes.\n", n);
        avr_program1200(flash, n, 1);
      } else if(!strcmp(cmd,"megaprogram")) {
        fn = argv[2];
        n = intelhex_load(fn, flash, sizeof(flash));
        if(n < 0) {
          exit(EXIT_FAILURE);
        }
        printf("Loaded %d bytes.\n", n);
        avr_program_mega8(flash, n, 32, 1);
      } else {
        printf("unknown operation %s\n", argv[4]);
      }
    }
  }

  return 0;
}