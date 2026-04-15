CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread $(shell pkg-config --cflags x11 xfixes xext xrandr cairo libevdev gtk+-3.0 appindicator3-0.1)
LIBS    = $(shell pkg-config --libs x11 xfixes xext xrandr cairo libevdev gtk+-3.0 appindicator3-0.1) -lm -pthread

wacom-cursor: cursor.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f wacom-cursor

.PHONY: clean
