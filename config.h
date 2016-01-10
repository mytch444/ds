/* config.h */

/* x server command */
char *const xcmd[]  = {"/usr/X11R6/bin/X", ":0", "vt5", NULL};

/* x server timeout (seconds) */
const int xtimeout = 15;

char *default_path = "/usr/local/bin:/usr/X11R6/bin:/usr/bin:/bin";

char *log_path = "/var/log/ds.log";
