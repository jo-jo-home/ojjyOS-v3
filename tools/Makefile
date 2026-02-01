# ojjyOS v3 Tools Makefile

CC = cc
CFLAGS = -Wall -Wextra -O2

TOOLS = mkojfs mkicon mkwallpaper

.PHONY: all clean

all: $(TOOLS)

mkojfs: mkojfs.c
	$(CC) $(CFLAGS) -o $@ $<

mkicon: mkicon.c
	$(CC) $(CFLAGS) -o $@ $<

mkwallpaper: mkwallpaper.c
	$(CC) $(CFLAGS) -o $@ $< -lm

clean:
	rm -f $(TOOLS)
