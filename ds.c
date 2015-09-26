/* See LICENSE for details */
#define _XOPEN_SOURCE 500
#define _BSD_SOURCE /* initgroups */
#include <errno.h>
#include <ctype.h>
#include <setjmp.h>
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
#include <login_cap.h>
#include <bsd_auth.h>

/* variables */
static char *progname;

static struct {
	pid_t pid;
	int started;
	char *dsp;
} xserver;

static jmp_buf termdm;

/* configuration */
#include <config.h>

/* functions */
static char *cat (char *, char *);
static void die (const char *, ...);
static void makedaemon (void);
static void resetserver (void);
static void runsession (void);
static void serverhandler (int);
static void setsignal (int, void (*) (int));
static void spawnwm (void);
static void startserver (void);
static void termhandler (int);

char *
cat (char *dst, char *src) {
	char buff[256];

	strncpy(buff,dst,sizeof(buff)/sizeof(buff[0]));
	strncat(buff,src,sizeof(buff)/sizeof(buff[0]));
	return strdup(buff);
}

void
die (const char *error, ...) {
	va_list alist;

	va_start(alist,error);
	vfprintf(stderr,error,alist);
	va_end(alist);

	exit(EXIT_FAILURE);
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
	unsigned int nwins;
	Display *dsp;
	Window *win;
	Window root;

	if ((dsp = XOpenDisplay(xserver.dsp)) == NULL)
		die("%s: unable to connect to the x server\n", progname);

	root = RootWindow(dsp, DefaultScreen(dsp));
	
	XQueryTree(dsp, root, &root, &root, &win, &nwins);
	for (; nwins--; win++)
		XKillClient(dsp, *win);

	XSync(dsp, 0);
}

void
runsession (void) {
	pid_t pid;

	switch ((pid = fork())) {
		case -1:
			perror("fork");
			die("%s: cannot fork to run client session.\n",progname);
			break;
		case 0:
			/* drop privileges & spawn wm */
			spawnwm();
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
spawnwm (void) {
	char *xinit, *cmd[3], *env[7];
	struct passwd *pwd;

	pwd = getpwnam(user);
	if (!pwd) {
		die("Failed to get auth information for user: %s\n", user);
	}

	if (setusercontext(NULL, pwd, pwd->pw_uid, LOGIN_SETALL) != 0) {
		die("%s: (session process) cannot set user context\n", progname);
	}

	chdir(pwd->pw_dir);

	xinit = cat(pwd->pw_dir, "/.xinitrc"); 

	cmd[0] = pwd->pw_shell;
	cmd[1] = xinit;
	cmd[2] = NULL;

	env[0] = cat("HOME=", pwd->pw_dir);
	env[1] = cat("LOGNAME=", pwd->pw_name);
	env[2] = cat("USER=", pwd->pw_name);
	env[3] = cat("SHELL=", pwd->pw_shell);
	env[4] = cat("PATH=", default_path);
	env[5] = cat("DISPLAY=", xserver.dsp);
	env[6] = NULL;

	execve(cmd[0], cmd, env);
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
			for (i=0; i<sizeof(xcmd)/sizeof(xcmd[0]); i++)
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

int
main (int argc, char *argv[]) {
	progname = argv[0];
	if (geteuid() != 0)
		die("%s: only root can launch ds.\n",progname);

	makedaemon();
	fprintf(stdout,"%s (pid: %d)\n",progname,getpid());
	startserver();

	if (!setjmp(termdm)) {
		setsignal(SIGTERM,termhandler);
		while (1) {
			runsession();
			resetserver();
			sleep(2);
		}
	}

	fprintf(stdout,"killing x server (%d)\n",xserver.pid);
	kill(xserver.pid,SIGTERM);
	exit(EXIT_SUCCESS);
	return 0;
}

