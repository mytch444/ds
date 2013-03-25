# system dependant config
X11LIB = /usr/X11R6/lib
X11INC = /usr/X11R6/include

# unless all your passwords are in /etc/passwd,
# do not uncomment this
FEATURES = -DHAVE_SHADOW_H

# paths
prefix=/usr
bindir=${prefix}/bin
mandir=${prefix}/share/man/man1

CC = cc
INSTALL = install -c
