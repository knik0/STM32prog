/*************************************************************************
 STM32 programmer
 (C) 2012  Krzysztof Nikiel
 ************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <asm/ioctls.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <unistd.h>

#include <getopt.h>

#define TRUE 1
#define FALSE 0

#define ACK	0x79
#define NACK	0x1f

#define CMDGET	0x00
#define CMDGV	0x01
#define CMDGID	0x02
#define CMDRM	0x11
#define CMDGO	0x21
#define CMDWM	0x31
#define CMDER	0x43
#define CMDEER	0x44
#define CMDWP	0x63
#define CMDWUNP	0x73
#define CMDRDP	0x82
#define CMDRDUNP 0x92

static int g_fd = 0;
static uint8_t g_in;

static int g_speed = 17;
static char *g_dev = NULL;

#define MAXCMDS 20
static struct
{
  uint8_t ver;
  int num;
  uint8_t list[MAXCMDS];
} g_cmds;


static int devopen(void);

static inline int recv(void)
{
  int tmp;

  if (!g_fd)
  {
    fprintf(stderr, "recv: g_fd not open\n");
    exit(1);
  }

  if ((tmp = read(g_fd, &g_in, 1)) == 1)
    return 0;
  if (tmp < 0)
  {
    perror("read");
    return -1;
  }
  if (tmp < 1)
  {
    fprintf(stderr, "no data to read\n");
    return -1;
  }

  return 0;
}

static inline int send(uint8_t byte)
{
  if (!g_fd)
  {
    fprintf(stderr, "send: g_fd not open\n");
    exit(1);
  }

  if (write(g_fd, &byte, 1) < 0)
  {
    perror("write");
    return -1;
  }

  return 0;
}


static int g_ackwait = 100;
static int getack(void)
{
  int wait = 10;

wait:
  if (recv())
  {
    if (wait < g_ackwait)
    {
      fprintf(stderr, "waiting for ACK\n");
      usleep(1000*wait);
      wait *= 2;
      goto wait;
    }

    return FALSE;
  }

  if (g_in != ACK)
  {
    fprintf(stderr, "no ACK (0x%02x)\n", g_in);
    return FALSE;
  }

  return TRUE;
}


static int cmdget(void)
{
  int cnt;

  if (devopen())
    exit(1);

  fprintf(stderr, "sending GET\n");
  send(CMDGET);
  send(~CMDGET);

  if (!getack())
    return FALSE;

  if (recv())
  {
    fprintf(stderr, "no bytes\n");
    return FALSE;
  }
  g_cmds.num = g_in;

  if (recv())
  {
    fprintf(stderr, "can't read loader version\n");
    return FALSE;
  }
  g_cmds.ver = g_in;

  if (g_cmds.num > MAXCMDS)
  {
    fprintf(stderr, "commad list too big\n");
    return FALSE;
  }

  for (cnt = 0; cnt < g_cmds.num; cnt++)
  {
    if (recv())
    {
      fprintf(stderr, "can't read cmd byte %d\n", cnt);
      return FALSE;
    }
    g_cmds.list[cnt] = g_in;
  }

  fprintf(stderr, "bootloader version: %d.%d\n", g_cmds.ver >> 4,
	  g_cmds.ver & 15);
  fprintf(stderr, "commands:");
  for (cnt = 0; cnt < g_cmds.num; cnt++)
    fprintf(stderr, " 0x%02x", g_cmds.list[cnt]);
  fprintf(stderr, "\n");

  return getack();
}

static int checkcmd(uint8_t cmd)
{
  int cnt;

  if (g_cmds.num < 0)
    cmdget();

  for (cnt = 0; cnt < g_cmds.num; cnt++)
    if (g_cmds.list[cnt] == cmd)
      return FALSE;

  fprintf(stderr, "command 0x%02x not available\n", cmd);

  return TRUE;
}

static int cmdgv(void)
{
  if (checkcmd(CMDGV))
    return FALSE;
  fprintf(stderr, "sending GV\n");
  send(CMDGV);
  send(~CMDGV);

  if (!getack())
    return FALSE;

  if (recv())
  {
    fprintf(stderr, "no bytes\n");
    return FALSE;
  }
  fprintf(stderr, "GV version: 0x%02x\n", g_in);

  if (recv())
  {
    fprintf(stderr, "no bytes\n");
    return FALSE;
  }
  fprintf(stderr, "GV option1: 0x%02x\n", g_in);

  if (recv())
  {
    fprintf(stderr, "no bytes\n");
    return FALSE;
  }
  fprintf(stderr, "GV option2: 0x%02x\n", g_in);

  return getack();
}

static int cmdgid(void)
{
  int cnt;
  int num;

  if (checkcmd(CMDGID))
    return FALSE;
  fprintf(stderr, "sending GID\n");
  send(CMDGID);
  send(~CMDGID);

  if (!getack())
    return FALSE;

  if (recv())
  {
    fprintf(stderr, "no bytes\n");
    return FALSE;
  }
  num = g_in + 1;

  fprintf(stderr, "PID: 0x");
  for (cnt = 0; cnt < num; cnt++)
  {
    if (recv())
    {
      fprintf(stderr, "can't read PID byte %d\n", cnt);
      return FALSE;
    }
    fprintf(stderr, "%02x", g_in);
  }
  fprintf(stderr, "\n");

  return getack();
}

// read memory:
static int cmdrm(uint32_t addr32, uint8_t len)
{
  int cnt;
  uint8_t addr[4];
  uint8_t xsum;
  uint8_t buf[0x100];

  if (checkcmd(CMDRM))
    return FALSE;
  fprintf(stderr, "sending ReadMemory(%08x,%x)\n", addr32, len + 1);
  send(CMDRM);
  send(~CMDRM);

  if (!getack())
    return FALSE;

  for (cnt = 0; cnt < 4; cnt++)
    addr[cnt] = addr32 >> (cnt * 8);

  //fprintf(stderr, "sending address\n");
  xsum = 0;
  for (cnt = 3; cnt >= 0; cnt--)
  {
    send(addr[cnt]);
    xsum ^= addr[cnt];
  }
  send(xsum);
  if (!getack())
    return FALSE;

  send(len);
  send(~len);
  if (!getack())
    return FALSE;

  for (cnt = 0; cnt <= len; cnt++)
  {
    if (recv())
    {
      fprintf(stderr, "can't read byte %d\n", cnt);
      return FALSE;
    }
    buf[cnt] = g_in;
  }

  fwrite(buf, 1, cnt, stdout);

  return TRUE;
}

// write memory:
static int cmdwm(uint32_t addr32)
{
  int cnt;
  uint8_t addr[4];
  uint8_t xsum;
  uint8_t buf[0x100];
  int len;
  int tmp;

  if (checkcmd(CMDWM))
    return FALSE;

  len = fread(buf, 1, 0x100, stdin);
  while (1)
  {
    if (len < 1)
      break;

    fprintf(stderr, "sending WriteMemory(%08x,%x)\n", addr32, len);
    send(CMDWM);
    send(~CMDWM);

    if (!getack())
    {
      fprintf(stderr, "CMDWM cmd failed\n");
      return FALSE;
    };

    for (cnt = 0; cnt < 4; cnt++)
      addr[cnt] = addr32 >> (cnt * 8);

    //fprintf(stderr, "sending address\n");
    xsum = 0;
    for (cnt = 3; cnt >= 0; cnt--)
    {
      send(addr[cnt]);
      xsum ^= addr[cnt];
    }
    send(xsum);
    if (!getack())
    {
      fprintf(stderr, "CMDWM send(xsum) failed\n");
      return FALSE;
    };

    xsum = 0;
    len--;
    send(len);
    xsum ^= len;
    len++;

    for (cnt = 0; cnt < len; cnt++)
    {
      int c = buf[cnt];
      send(c);
      xsum ^= c;
    }
    send(xsum);

    // waitning for ack, good moment to read new data
    tmp = fread(buf, 1, 0x100, stdin);

    if (!getack())
    {
	fprintf(stderr, "CMDWM failed\n");
	return FALSE;
    };

    addr32 += len;
    len = tmp;
  }

  return TRUE;
}

// erase:
static int cmder(void)
{
  uint8_t len = 255;
  int cmd = CMDER;

  if (checkcmd(cmd))
  {
      cmd = CMDEER;
      fprintf(stderr, "Using Extended Erase\n");
  }
  if (checkcmd(cmd))
      return FALSE;
  fprintf(stderr, "sending Erase(%d)\n", len);
  send(cmd);
  send(~cmd);

  if (!getack())
    return FALSE;

  if (cmd == CMDER)
  {
      send(len);
      send(~len);
  }
  else if (cmd == CMDEER)
  {
      send(0xff);
      send(0xff);
      send(0);
  }

  g_ackwait = 2000;

  if (!getack())
    return FALSE;

  return TRUE;
}

// Go:
static int cmdgo(uint32_t addr32)
{
  int cnt;
  uint8_t addr[4];
  uint8_t xsum;

  if (checkcmd(CMDGO))
    return FALSE;
  fprintf(stderr, "sending Go(%08x)\n", addr32);
  send(CMDGO);
  send(~CMDGO);

  if (!getack())
    return FALSE;

  for (cnt = 0; cnt < 4; cnt++)
    addr[cnt] = addr32 >> (cnt * 8);

  xsum = 0;
  for (cnt = 3; cnt >= 0; cnt--)
  {
    send(addr[cnt]);
    xsum ^= addr[cnt];
  }
  send(xsum);
  if (!getack())
    return FALSE;
  fprintf(stderr, "ACK1 OK\n");

#if 0
  if (!getack())
    return FALSE;
  fprintf(stderr, "ACK2 OK\n");
#endif

  return TRUE;
}

static int cmdrdprot(int protect)
{
  if (protect)
  {
    if (checkcmd(CMDRDP))
      return FALSE;
    fprintf(stderr, "sending RDP\n");
    send(CMDRDP);
    send((uint8_t) ~ CMDRDP);
  }
  else
  {
    if (checkcmd(CMDRDUNP))
      return FALSE;
    fprintf(stderr, "sending RDUNP\n");
    send(CMDRDUNP);
    send((uint8_t) ~ CMDRDUNP);
  }

  if (!getack())
    return FALSE;

  usleep(100000);

  if (!getack())
    return FALSE;

  return TRUE;
}


static void devclose(void)
{
  int status;

  if (!g_fd)
    return;

  // clear RTS:
  status = TIOCM_RTS;
  ioctl(g_fd, TIOCMBIC, &status);

  close(g_fd);

  g_fd = 0;
}


static int init(void)
{
  fprintf(stderr, "sending initial 0x7f\n");
  send(0x7f);

  g_ackwait = 40;
#if 0
  // read pending input:
  if (recv())
    return FALSE;

  if (!getack())
    return FALSE;
#else
  if (!getack())
    return FALSE;
#endif

  fprintf(stderr, "init OK\n");
  usleep(40000);

  return TRUE;
}

static int devopen(void)
{
  int status;
  struct termios options;
  static int speeds[] = {
      0,
      50,
      75,
      110,
      134,
      150,
      200,
      300,
      600,
      1200,
      1800,
      2400,
      4800,
      9600,
      19200,
      38400,
      57600,
      115200,
      230400,
      460800,
      500000,
      576000,
      921600,
      1000000,
      1152000,
      1500000,
      2000000,
      2500000,
      3000000,
      3500000,
      4000000
  };

  void chkin(void)
  {
    fd_set rfds;
    struct timeval tv;
    int tmp;

    FD_ZERO(&rfds);
    FD_SET(g_fd, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 20000;

    tmp = select(1, &rfds, NULL, NULL, &tv);
    if (tmp < 0)
    {
      perror("select");
      return;
    }

    if (tmp)
    {
      fprintf(stderr, "input ready -> reading\n");
      recv();
    }
  }

  if (g_fd)
    return 0;

  if (!g_dev)
  {
    fprintf(stderr, "devopen: no device specified\n");
    return 1;
  }

  //fcntl(g_fd, F_SETFL, 0);

  if ((g_fd = open(g_dev, O_RDWR)) < 0)
  {
//      fprintf(stderr, "init: can't open %s\n", g_dev);
      perror(g_dev);
      exit(1);
      return 1;
  }
  // set RTS:
  status = TIOCM_RTS;
  ioctl(g_fd, TIOCMBIS, &status);
  usleep(40000);

  /* get the current options */
  tcgetattr(g_fd, &options);

  // raw I/O:
  options.c_cflag = (CLOCAL | CREAD | CS8);
  options.c_cflag |= PARENB;
  //options.c_cflag |= CRTSCTS;
  options.c_lflag = 0;
  options.c_oflag = 0;
  options.c_iflag = 0;
  options.c_iflag = INPCK;
  //options.c_iflag |= IGNPAR;
  // 0.1 second timeout:
  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 1;
  //options.c_cc[VTIME] = 10;

  if (g_speed < 16)
      cfsetspeed(&options, g_speed);
  else
      cfsetspeed(&options, 0x1000 + g_speed - 15);

  g_speed = cfgetospeed(&options);
  fprintf(stderr, "IO speed: ");
  if (g_speed < 16)
      fprintf(stderr, "%i\n", g_speed[speeds]);
  else
      fprintf(stderr, "%i\n", (g_speed - 0x1000 + 15)[speeds]);


  /* set the options */
  tcsetattr(g_fd, TCSANOW, &options);

  // check input:
  chkin();

  atexit(devclose);

  return 0;
}

static void help(char *name)
{
    /* *INDENT-OFF* */
    fprintf(stderr, "usage: %s <options>; numbers in hex:\n"
	    " -h\t\thelp\n"
	    " -d <devname>\topen device\n"
	    " -i\t\tinit data connection\n"
	    " -v\t\tget version\n"
	    " -r <addr>\tRead Memory to stdout\n"
	    " -e\t\tGlobal Erase\n"
	    " -w <addr,len>\tWrite Memory from stdin\n"
	    " -g <addr>\tJump to address\n"
	    " -s<speed>\n"
	    " -p<set>\tRead protection\n"
	    , name);
    /* *INDENT-ON* */

  exit(1);
}



int main(int argc, char *argv[])
{
  //int cnt;
  int tmp1, tmp2;
  int phelp = TRUE;

  memset(&g_cmds, 0, sizeof(g_cmds));
  g_cmds.num = -1;

  while (1)
  {
    int opt;

    opt = getopt(argc, argv, "hd:ivr:ew:g:s:p:");

    if (opt < 0)
      break;

    phelp = FALSE;

    switch (opt)
    {
    case 'h':
      help(argv[0]);
      break;
    case 'd':
      g_dev = optarg;
      break;
    case 'i':
      devopen();
      if (!init())
	goto exit;
      cmdgid();
      break;
    case 'r':
      if (sscanf(optarg, "%x,%x", &tmp1, &tmp2) != 2)
	help(argv[0]);
      while (tmp2 > 256)
      {
	cmdrm(tmp1, 255);
	tmp1 += 256;
	tmp2 -= 256;
      }
      if (tmp2 > 0)
	cmdrm(tmp1, tmp2 - 1);
      break;
    case 'e':
      cmder();
      break;
    case 'w':
      if (sscanf(optarg, "%x", &tmp1) != 1)
	help(argv[0]);

      cmdwm(tmp1);
      break;
    case 'g':
      if (sscanf(optarg, "%x", &tmp1) != 1)
	help(argv[0]);
      cmdgo(tmp1);
      break;
    case 's':
      if (sscanf(optarg, "%d", &tmp1) != 1)
	help(argv[0]);
      g_speed = tmp1;
      if (g_speed > 30)
          g_speed = 30;
      break;
    case 'v':
      if (!cmdgid())
        return -1;
      cmdgv();
      break;
    case 'p':
      if (*optarg == '0')
	cmdrdprot(0);
      else
	cmdrdprot(1);
      break;
    }
  }
  if (phelp)
      help(argv[0]);

exit:
  devclose();

  return 0;
}
