# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# The host's screens rendered on this computer, from the firmware's own
# sources: gfx, the kit, the window manager and the views (main/screens.c).
# `tools/perinkpheral preview` builds this, runs it and turns its PBMs into PNGs.
#
#     make            build/perinkpheral-preview

ROOT  := ../..
FW    := $(ROOT)/firmware
COMP  := $(FW)/components
GFX   := $(COMP)/gfx
UI    := $(COMP)/ui
QR    := $(COMP)/qr
BUILD := build

CFLAGS := -std=c11 -Wall -Wextra -Werror \
          -I$(ROOT)/recovery/main -I$(GFX)/include -I$(COMP)/words/include -I$(GFX)/fonts -I$(UI)/include -I$(FW)/main -I$(QR)/include -I$(QR)/qrcodegen

SOURCES := preview.c $(ROOT)/recovery/main/screen.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(COMP)/words/words.c $(GFX)/font_square.c \
           $(UI)/notice.c $(UI)/scrollbar.c $(UI)/list.c $(UI)/kit.c $(UI)/wm.c \
           $(FW)/main/screens.c $(QR)/qrcodegen/qrcodegen.c $(QR)/wifi_qr.c

$(BUILD)/perinkpheral-preview: $(SOURCES) $(wildcard $(GFX)/include/*.h $(GFX)/fonts/*.h $(UI)/include/*.h $(FW)/main/screens.h $(ROOT)/recovery/main/*.h)
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

clean:
	rm -rf $(BUILD)

.PHONY: clean
