# ojjyOS v3 Tools Makefile
#
# Host tools are BUILT PRODUCTS (QA-22): binaries go to ../build/tools,
# never into this source directory.

CC = cc
CFLAGS = -Wall -Wextra -O2

BUILD = ../build/tools
TOOLS = mkojfs mkicon mkwallpaper mkarray
BINS = $(addprefix $(BUILD)/,$(TOOLS))

.PHONY: all clean

all: $(BINS)

$(BUILD)/mkojfs: mkojfs.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/mkicon: mkicon.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/mkwallpaper: mkwallpaper.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< -lm

$(BUILD)/mkarray: mkarray.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD)
