/* config.h */

/* TTY to open */
char *tty = "/dev/ttyC4";

/* x server command */
#if __OpenBSD__
char *xcmd[]  = {"/usr/X11R6/bin/X", ":3", "vt5", NULL};
#elif __linux__
char *xcmd[]  = {"/usr/bin/Xorg", ":3", "vt5", NULL};
#else
#error "Unknown OS"
#endif

/* x server timeout (seconds) */
int xtimeout = 15;

char *default_path = "/usr/local/bin:/usr/X11R6/bin:/usr/bin:/bin";

/* Append to user $HOME after server start. */
char *user_script_suffix = "/.xinitrc";

char *log_path = "/var/log/ds.log";
