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

#define len(X) (sizeof(X)/sizeof(X[0]))

/* variables */
static char *progname;

static struct {
	pid_t pid;
	int started;
	char *dsp;
} xserver;

static pid_t sessionpid = 0;

/* configuration */
#include <config.h>

/* functions */
static char *cat (char *, char *);
static void die (const char *, ...);
static void runsession (struct passwd *);
static void killsession (int sig);
static void serverhandler (int);
static void setsignal (int, void (*) (int));
static void spawnwm (struct passwd *);
static void startserver (void);

char *
cat (char *dst, char *src) {
	char buff[256];

	strncpy(buff, dst, len(buff));
	strncat(buff, src, len(buff));
	return strdup(buff);
}

void
die (const char *error, ...) {
	va_list alist;

	va_start(alist, error);
	vprintf(error, alist);
	va_end(alist);

	exit(EXIT_FAILURE);
}

void
runsession (struct passwd *pwd) {
	switch ((sessionpid = fork())) {
		case -1:
			perror("fork");
			die("%s: cannot fork to run client session.\n", progname);
			break;
		case 0:
			/* drop privileges & spawn wm */
			printf("dropping privileges and spawning wm\n");
			spawnwm(pwd);
			break;
		default:
			waitpid(sessionpid,NULL,0);
			printf("%s: session finished (pid %d).\n", progname, sessionpid);
			break;
	}
}

void
killsession (int sig) {
	kill(sessionpid, sig);
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
	char *xinit, *cmd[3], *env[7];
	int i;

	printf("setting user context: ");

	if (setusercontext(NULL, pwd, pwd->pw_uid, LOGIN_SETALL) != 0) {
		die("%s: (session process) cannot set user context\n", progname);
	} else {
		printf("success\n");
	}
	
	xinit = cat(pwd->pw_dir, "/.xinitrc"); 

	cmd[0] = pwd->pw_shell;
	cmd[1] = xinit;
	cmd[2] = NULL;

	printf("xinit command: %s %s\n", cmd[0], cmd[1]);

	env[0] = cat("HOME=", pwd->pw_dir);
	env[1] = cat("LOGNAME=", pwd->pw_name);
	env[2] = cat("USER=", pwd->pw_name);
	env[3] = cat("SHELL=", pwd->pw_shell);
	env[4] = cat("PATH=", default_path);
	env[5] = cat("DISPLAY=", xserver.dsp);
	env[6] = NULL;

	printf("setting env to:\n");
	for (i = 0; i < len(env) - 1; i++) {
		printf("\t%s\n", env[i]);
	}
	
	printf("chdir\n");
	chdir(pwd->pw_dir);
	printf("execve\n");
	execve(cmd[0], cmd, env);
	die("%s: (session process) cannot run session wm.\n",progname);
}

void
startserver (void) {
	pid_t pid;
	time_t starttime;
	char *display_name = ":0";
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

			for (i=0; i < len(xcmd); i++) {
				if (xcmd[i] && xcmd[i][0] == ':') {
					display_name = xcmd[i];
					break;
				}
			}

			xserver.dsp = display_name;
			printf("%s: x server started (dis: %s)"
			        "(pid: %d)\n", progname, display_name, pid);
			setsignal(SIGUSR1,SIG_IGN);
			break;
	}
}

int
main (int argc, char *argv[]) {
	struct passwd *pwd;
	int logfd;

	progname = argv[0];

	if (argc != 2) 
		die("usage: %s username\n", progname);
	
	if (geteuid() != 0)
		die("%s: only root can launch ds.\n", progname);

	if (!(pwd = getpwnam(argv[1]))) 
		die("Failed to get auth information for user: %s\n", argv[1]);

	logfd = open(log_path, O_WRONLY);
	if (logfd == -1)
		die("Error opening log : %s\n", log_path);

	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	dup2(logfd, STDOUT_FILENO);
	dup2(logfd, STDERR_FILENO);

	printf("%s (pid: %d)\n", progname, getpid());

	printf("start server\n");
	startserver();

	printf("runsession\n");
	
	setsignal(SIGTERM, killsession);
	setsignal(SIGINT, killsession);
	runsession(pwd);

	printf("killing x server (%d)\n", xserver.pid);
	kill(xserver.pid, SIGTERM);

	exit(EXIT_SUCCESS);
	return 0;
}

