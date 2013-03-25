/* See LICENSE for details */
#define _XOPEN_SOURCE 500
#define _BSD_SOURCE /* initgroups */
#include <errno.h>
#include <ctype.h>
#include <setjmp.h>
#ifdef HAVE_SHADOW_H
#include <shadow.h>
#endif
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <grp.h>
#include <pwd.h>

/* code */
enum { BGColor, GreetColor, TextColor };
enum { AuthOK, AuthInvalidUser, AuthBadPass };

#define LENGTH(t) sizeof t / sizeof t[0]

struct DLG {
  unsigned int width;
  unsigned int height;
  char *greet;
  char *loginprompt;
  char *passprompt;
};

/* variables */
static char *progname;
static const char version[] = "0";

static struct {
  Display *xdsp;
  int screennum;
  struct {
    XFontStruct *xfont;
    int height;
  } font;
  int colors[3];
  GC gc;
  Pixmap drawable;
  Window root;
  Window win;
  int dsp_width;
  int dsp_height;
} ctx;

static struct {
  pid_t pid;
  int started;
  char *dsp;
} xserver;

static jmp_buf termdm;

/* configuration */
#include <config.h>

/* functions */
static int authenticate (const char *, const char *, struct passwd **);
static char *cat (char *, char *);
static void die (const char *, ...);
static void drawtext (const char *, int, int, int, int);
static int getcolor(const char *);
static void initfont (const char *);
static void initx (void);
static void makedaemon (void);
static void resetserver (void);
static struct passwd *rundialog (void);
static void runsession (struct passwd *);
static void serverhandler (int);
static void setsignal (int, void (*) (int));
static void spawnwm (struct passwd *);
static void startserver (void);
static void termhandler (int);
static void updatedialog (const char *);

int
authenticate (const char *username, const char *password, struct passwd **opwd) {
  char *pass;
  struct passwd *pwd;
#ifdef HAVE_SHADOW_H
  struct spwd *spwd;
#endif

  *opwd = NULL;
  pwd = getpwnam(username);
  if (pwd == NULL)
    return AuthInvalidUser;
  *opwd = pwd;
  pass = pwd->pw_passwd;

#ifdef HAVE_SHADOW_H
  spwd = getspnam(username);
  endspent();
  if (spwd == NULL)
    return AuthInvalidUser;
  pass = spwd->sp_pwdp;
#endif
  /* empty pass log on */
  if (!pass || pass[0] == '\0')
    return AuthOK;
  if (strcmp(crypt(password,pass),pass))
    return AuthBadPass;

  return AuthOK;
}

char *
cat (char *dst, char *src) {
  char buff[256];

  strncpy(buff,dst,LENGTH(buff));
  strncat(buff,src,LENGTH(buff));
  return strdup(buff);
}

/* void */
/* clean (void) { */
/*   XFreeGC(ctx.xdsp,ctx.gc); */
/*   XFreePixmap(ctx.xdsp,ctx.drawable); */
/*   XFreeFont(ctx.xdsp,ctx.font.xfont); */
/*   XCloseDisplay(ctx.xdsp); */
/* } */

void
die (const char *error, ...) {
  va_list alist;

  va_start(alist,error);
  vfprintf(stderr,error,alist);
  va_end(alist);

  exit(EXIT_FAILURE);
}

void
drawtext (const char *text, int col, int x, int y, int w) {
  int len;

  XSetForeground(ctx.xdsp,ctx.gc,col);
  len = strlen(text);
  for (; w < XTextWidth(ctx.font.xfont,text,len) && len > 0 ; len--);
  if (!len)
    return;
  XDrawString(ctx.xdsp,ctx.drawable,ctx.gc,x,y,text,len);
}

int
getcolor (const char *color) {
  XColor xcol;

  if (!XAllocNamedColor(ctx.xdsp,DefaultColormap(ctx.xdsp,ctx.screennum),
                       color, &xcol, &xcol))
    die("%s: unable to load color %s\n",progname,color);
  return xcol.pixel;
}

void
initfont (const char *fontstr) {
  if ((ctx.font.xfont = XLoadQueryFont(ctx.xdsp,fontstr)) == NULL)
    die("%s: unable to load font \"%s\"\n",progname,fontstr);
  ctx.font.height = ctx.font.xfont->ascent + ctx.font.xfont->descent;
}

void
initx (void) {
  XGCValues values;

  if ((ctx.xdsp = XOpenDisplay(xserver.dsp)) == NULL)
    die("%s: unable to connect to the x server on display %s.\n",
        progname,XDisplayName(xserver.dsp));
  ctx.screennum = DefaultScreen(ctx.xdsp);
  ctx.root = RootWindow(ctx.xdsp,ctx.screennum);
  ctx.dsp_width = DisplayWidth(ctx.xdsp,ctx.screennum);
  ctx.dsp_height = DisplayHeight(ctx.xdsp,ctx.screennum);
  ctx.drawable = XCreatePixmap(ctx.xdsp,ctx.root,dlg.width,dlg.height,
                               DefaultDepth(ctx.xdsp,ctx.screennum));

  initfont(font);

  values.font = ctx.font.xfont->fid;
  ctx.gc = XCreateGC(ctx.xdsp,ctx.root,GCFont,&values);

  ctx.colors[TextColor] = getcolor(textcolor);
  ctx.colors[BGColor] = getcolor(bgcolor);
  ctx.colors[GreetColor] = getcolor(greetcolor);

  XDefineCursor(ctx.xdsp,ctx.root,
		XCreateFontCursor(ctx.xdsp,XC_left_ptr));
}

void
makedaemon (void) {
  pid_t pid;
  int fd;

  switch ((pid = fork())) {
  case -1:
    perror("fork");
    die("%s: cannot fork to go daemon.\n");
    break;
  case 0:
    setsid();
    chdir("/");
    fd = open("/dev/null",O_RDWR);
    dup2(fd,0);
    dup2(fd,1);
    dup2(fd,2);
    break;
  default:
    exit(EXIT_SUCCESS);
  }
}

void
resetserver (void) {
#if 0 /* x server is screwed, this will work only one time ! */
  int starttime = time(NULL);

  xserver.started = 0;
  setsignal(SIGUSR1,serverhandler);
  kill(xserver.pid,SIGHUP);
  while (time(NULL) - starttime < xtimeout) {
    if (xserver.started == 1)
      return;
    usleep(50000);
  }
  die("%s: x server seems to be dead.\n",progname);
#else /* do a soft reset instead */
  unsigned int nwins;
  Window *win;
  Window root;

  XQueryTree (ctx.xdsp,ctx.root,&root,&root,&win,&nwins);
  for (; nwins--; win++)
    XKillClient(ctx.xdsp,*win);

  XSync(ctx.xdsp,0);
#endif
}

struct passwd *
rundialog (void) {
  XSetWindowAttributes wa;
  int running = 2;
  XEvent e;
  char username[256];
  char password[256];
  char buff[16];
  char *storebuff = username;
  int len = 0, num = 0;
  KeySym ksym;
  struct passwd *pwd = NULL;

  if ((ctx.dsp_width < dlg.width) ||
      (ctx.dsp_height < dlg.height))
    die("%s: dialog dimensions are too big!\n",progname);
  wa.event_mask = KeyPressMask|ExposureMask;
  ctx.win = XCreateWindow(ctx.xdsp,ctx.root,(ctx.dsp_width-dlg.width)/2,
                          (ctx.dsp_height-dlg.height)/2,dlg.width,dlg.height,
                          0, DefaultDepth(ctx.xdsp,ctx.screennum),CopyFromParent,
                          DefaultVisual(ctx.xdsp,ctx.screennum),CWEventMask,&wa);
  if (!ctx.win)
    die("%s: cannot create dialog window",progname);
  XWarpPointer(ctx.xdsp,None,ctx.root,0,0,0,0,ctx.dsp_width,ctx.dsp_height);
  XMapWindow(ctx.xdsp,ctx.win);
  username[0] = '\0';
  while (running && !XNextEvent(ctx.xdsp,&e)) {
      switch (e.type) {
      case Expose:
        XSetInputFocus(ctx.xdsp,ctx.win,RevertToPointerRoot,CurrentTime);
        updatedialog(username);
        break;
      case KeyPress:
        buff[0] = 0;
        num = XLookupString(&e.xkey,buff,sizeof buff,&ksym,NULL);
        if (ksym == XK_KP_Enter)
          ksym = XK_Return;
        switch (ksym) {
        case XK_BackSpace:
          if (len)
            len--;
          storebuff[len] = '\0';
          break;
        case XK_Return:
          if (--running) {
            storebuff = password;
            len = 0;
          }
          if (!running) {
            if (authenticate(username,password,&pwd) != AuthOK) {
              XBell(ctx.xdsp,100);
              len = 0;
              storebuff = username;
              running = 2;
              username[0] = '\0';
              updatedialog("login failed"); sleep(1);
            }
          }
          break;
        default:
          if (num && !iscntrl((int) buff[0]) && (num + len + 1) < sizeof username) {
            memcpy(storebuff + len,buff,num);
            len += num;
            storebuff[len] = '\0';
          }
          break;
        }
        if (running == 2)
          updatedialog(username);
        break;
      }
  }
  XDestroyWindow(ctx.xdsp,ctx.win);
  XFlush(ctx.xdsp);
  memset(password,'\0',LENGTH(password));
  return pwd;
}

void
runsession (struct passwd *pwd) {
  pid_t pid;

  switch ((pid = fork())) {
  case -1:
    perror("fork");
    die("%s: cannot fork to run client session.\n",progname);
    break;
  case 0:
    /* drop privileges & spawn wm */
    spawnwm(pwd);
    break;
  default:
    waitpid(pid,NULL,0);
    fprintf(stdout,"%s: session finished (pid %d).\n",progname,pid);
    break;
  }
}

void
serverhandler (int sig) {
  if (sig == SIGUSR1)
    xserver.started = 1;
}

void
setsignal (int signum, void (*handler) (int)) {
  struct sigaction action;

  action.sa_handler = handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESETHAND;
  sigaction(signum, &action, NULL);
}

void
spawnwm (struct passwd *pwd) {
  char *env[6];

  if (setgid(pwd->pw_gid) == -1) {
    perror("setgid");
    die("%s: (session process) cannot drop privileges.\n",progname);
  }
  if (initgroups(pwd->pw_name,pwd->pw_gid) == -1) {
    perror("initgroups");
    die("%s: (session process) cannot drop privileges.\n",progname);
  }
  if (setuid(pwd->pw_uid) == -1) {
    perror("setuid");
    die("%s: (session process) cannot drop privileges.\n",progname);
  }
  chdir(pwd->pw_dir);

  env[0] = cat("HOME=",pwd->pw_dir);
  env[1] = cat("DISPLAY=",xserver.dsp);
  env[2] = cat("LOGNAME=",pwd->pw_name);
  env[3] = cat("USER=",pwd->pw_name);
  env[4] = cat("SHELL=",pwd->pw_shell);
  env[5] = NULL;

  execve(wmcmd[0],wmcmd,env);
  die("%s: (session process) cannot run session wm.\n",progname);
}

void
startserver (void) {
  pid_t pid;
  time_t starttime;
  char *display_name = ":0";
  int fd;
  int i;

  xserver.started = 0;
  setsignal(SIGUSR1,serverhandler);
  switch (pid = fork()) {
  case -1:
    perror("fork");
    die("%s: cannot fork to run x server.\n",progname);
    break;
  case 0:
    /* run x server, (TODO: add an auth feature) */
    setsignal(SIGUSR1,SIG_IGN);
    fd = open("/dev/null",O_RDWR);
    dup2(fd,0);
    dup2(fd,1);
    dup2(fd,2);
    execv(xcmd[0],xcmd);
    die("%s: (server process) server execution failed.\n",progname); /* not seen ... */
    break;
  default:
    xserver.pid = pid;
    starttime = time(NULL);
    while (xserver.started == 0) {
      if (time(NULL) - starttime > xtimeout)
        die("%s: server startup timed out.\n",progname);
      usleep(50000);
    }
    for (i=0; i<LENGTH(xcmd); i++)
      if (xcmd[i] && xcmd[i][0] == ':') {
        display_name = xcmd[i];
        break;
      }
    xserver.dsp = display_name;
    fprintf(stdout,"%s: x server started (pid: %d)\n",progname,pid);
    setsignal(SIGUSR1,SIG_IGN);
    break;
  }
}

void
termhandler (int sig) {
  if (sig == SIGTERM)
    longjmp(termdm,1);
}

void
updatedialog (const char *username) {
  int widthgreet, widthlogin, widthpass, widthuser;
  XRectangle r = {0, 0, 0, 0};

  widthgreet = XTextWidth(ctx.font.xfont,dlg.greet,strlen(dlg.greet));
  widthlogin = XTextWidth(ctx.font.xfont,dlg.loginprompt,strlen(dlg.loginprompt));
  widthpass = XTextWidth(ctx.font.xfont,dlg.passprompt,strlen(dlg.passprompt));
  if (username) {
    widthuser = XTextWidth(ctx.font.xfont,username,strlen(username));
  }

  if ((widthgreet > dlg.width) ||
      (10 + widthlogin > dlg.width) ||
      (10 + widthpass > dlg.width) ||
      (dlg.height < 7*ctx.font.height))
    die("%s: dialog dimensions are too small!\n",progname);

  r.width = dlg.width;
  r.height = dlg.height;
  XSetForeground(ctx.xdsp,ctx.gc,ctx.colors[BGColor]);
  XFillRectangles(ctx.xdsp,ctx.drawable,ctx.gc,&r,1);

  drawtext(dlg.greet,ctx.colors[GreetColor],(dlg.width-widthgreet)/2,
           ctx.font.height,widthgreet);
  drawtext(dlg.loginprompt,ctx.colors[TextColor],10,
           ctx.font.height*4,widthlogin);
  drawtext(dlg.passprompt,ctx.colors[TextColor],10,
           ctx.font.height*6,widthpass);
  if (username) {
    drawtext(username,ctx.colors[TextColor],10 + widthlogin,
             ctx.font.height*4,dlg.width-10-widthlogin);
  }

  XCopyArea(ctx.xdsp,ctx.drawable,ctx.win,ctx.gc,0,0,
            dlg.width,dlg.height,0,0);
  XFlush(ctx.xdsp);
}

int
main (int argc, char *argv[]) {
  struct passwd *userpwd;

  progname = argv[0];
  if (argc == 2 && !strcmp(argv[1],"-v"))
    die("dm version %s, © 2010 Quentin Carbonneaux.\n",version);
  if (geteuid() != 0)
    die("%s: only root can launch dm.\n",progname);
  makedaemon();
  fprintf(stdout,"%s version %s (pid: %d)\n",progname,version,getpid());
  startserver();
  initx();
  if (!setjmp(termdm)) {
    setsignal(SIGTERM,termhandler);
    while (1) {
      userpwd = rundialog();
      runsession(userpwd);
      resetserver();
      sleep(2);
    }
  }
  fprintf(stdout,"killing x server (%d)\n",xserver.pid);
  kill(xserver.pid,SIGTERM);
  exit(EXIT_SUCCESS);
  return 0;
}
