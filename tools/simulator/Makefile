# SPDX-License-Identifier: MIT
# Copyright (c) 2026 GrakovNe
#
# The simulator, built from the firmware's own sources for this computer:
# the interpreter, the host API, the kit, the window manager and the fonts
# are the device's, so a frame here is the frame the panel shows.
#
#     make            build/perinkpheral-sim, for this computer
#     make platforms  build/macos (arm64 and x86_64), build/linux-x86_64, build/linux-aarch64

ROOT     := ../..
FW       := $(ROOT)/firmware
COMP     := $(FW)/components
SDK      := $(ROOT)/sdk
APPSYS   := $(COMP)/appsys
UI       := $(COMP)/ui
GFX      := $(COMP)/gfx
QR       := $(COMP)/qr
WASM3    := $(COMP)/wasm3/wasm3
BUILD    := build

INCLUDES := -I$(BUILD) -I$(APPSYS)/include -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts \
            -I$(QR)/include -I$(QR)/qrcodegen -I$(WASM3) -I$(SDK) -I$(SDK)/lib \
            -I$(COMP)/board/include -I$(COMP)/deadlines/include -I$(FW)/main
OPT      ?= -O2 -g
CFLAGS   := $(OPT) -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -D_GNU_SOURCE \
            -Dd_m3MaxLinearMemoryPages=1 $(INCLUDES)
# The interpreter as the device builds it: one page, no floating point.
WASM3_FLAGS := -std=c11 -O2 -w -Dd_m3MaxLinearMemoryPages=1 -Dd_m3VerboseErrorMessages=0 \
               -Dd_m3LogOutput=0 -Dd_m3HasFloat=0 -Dd_m3HasRefTypes=0 \
               -Dd_m3HasExtendedConst=0 -Dd_m3HasCompactImports=0
LDLIBS   := -lpthread -lm

SOURCES := sim.c \
           $(APPSYS)/app.c $(APPSYS)/app_scene.c $(APPSYS)/app_delivery.c $(APPSYS)/app_store.c \
           $(APPSYS)/app_manifest.c $(APPSYS)/json_tree.c $(APPSYS)/text_template.c \
           $(SDK)/lib/json_pick.c \
           $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c \
           $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/words.c $(GFX)/font_square.c \
           $(FW)/main/screens.c $(COMP)/deadlines/deadline_table.c $(QR)/qrcodegen/qrcodegen.c
WASM3_SOURCES := $(wildcard $(WASM3)/*.c)

$(BUILD)/perinkpheral-sim: $(SOURCES) $(WASM3_SOURCES) $(BUILD)/host_imports.inc
	mkdir -p $(BUILD)/wasm3
	for source in $(WASM3_SOURCES); do \
		$(CC) $(WASM3_FLAGS) $(ARCH) -c $$source -o $(BUILD)/wasm3/$$(basename $$source .c).o || exit 1; \
	done
	$(CC) $(CFLAGS) $(ARCH) -o $@ $(SOURCES) $(BUILD)/wasm3/*.o $(LDLIBS) $(LDFLAGS)

$(BUILD)/host_imports.inc: $(SDK)/host_api.h $(ROOT)/tools/hostapi/gen_imports.py
	mkdir -p $(BUILD)
	python3 $(ROOT)/tools/hostapi/gen_imports.py $(SDK)/host_api.h $(BUILD)

# The binaries the CLion plugin carries. Linux is static (musl): it runs on any
# distribution. Cross-built with zig.
universal:
	$(MAKE) BUILD=build/macos OPT=-O2 ARCH="-arch arm64 -arch x86_64"

linux-x86_64 linux-aarch64:
	$(MAKE) BUILD=build/$@ OPT=-O2 CC="zig cc -target $(subst linux-,,$@)-linux-musl" LDFLAGS="-static -s"

platforms: universal linux-x86_64 linux-aarch64

.PHONY: universal linux-x86_64 linux-aarch64 platforms
