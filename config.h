/* config.h */

/* font */
static const char font[] = "-*-*-medium-*-*-*-14-*-*-*-*-*-*-*";

/* colors */
static const char bgcolor[] = "white";
static const char greetcolor[] = "red";
static const char textcolor[] = "black";

/* dialog settings */
static const struct DLG dlg = {
  300, 150,			/* Dialog dimansions (width,height) */
  "welcome to dm 0 on cleon !",	/* Greeting message */
  "login : ",			/* Login prompt */
  "password : "			/* Password prompt */
};

/* external programs settings */
 /* x server command */
static char *const xcmd[]  = {"/usr/bin/Xorg", ":1", "vt8", NULL};
 /* window manager (session manager), don't forget -login if you invoke bash */
static char *const wmcmd[] = {"/usr/bin/ssh-agent", "/bin/bash", "-login", "-c", "/usr/bin/ratpoison", NULL};

/* x server timeout (seconds) */
static const int xtimeout = 15;
