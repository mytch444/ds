/* config.h */

/* x server command */
static char *const xcmd[]  = {"/usr/X11R6/bin/X", ":1", "vt5", NULL};
/* x server timeout (seconds) */
static const int xtimeout = 15;

static char *default_path = "/usr/local/bin:/usr/X11R6/bin:/usr/bin:/bin";

/* User to start the server for */
static char *user = "ginko";

