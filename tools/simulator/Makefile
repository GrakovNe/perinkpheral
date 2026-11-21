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

INCLUDES := -I$(BUILD) -I$(APPSYS)/include -I$(COMP)/appstore/include -I$(UI)/include -I$(GFX)/include -I$(COMP)/words/include -I$(GFX)/fonts \
            -I$(QR)/include -I$(QR)/qrcodegen -I$(WASM3) -I$(SDK) -I$(SDK)/lib \
            -I$(COMP)/board/include -I$(COMP)/appconf/include -I$(COMP)/deadlines/include -I$(FW)/main
OPT      ?= -O2 -g
CFLAGS   := $(OPT) -std=c11 -Wall -Wextra -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -D_GNU_SOURCE \
            -Dd_m3MaxLinearMemoryPages=1 $(INCLUDES)
# The interpreter as the device builds it: one page, no floating point.
WASM3_FLAGS := -std=c11 -O2 -w -Dd_m3MaxLinearMemoryPages=1 -Dd_m3VerboseErrorMessages=0 \
               -Dd_m3LogOutput=0 -Dd_m3HasFloat=0 -Dd_m3HasRefTypes=0 \
               -Dd_m3HasExtendedConst=0 -Dd_m3HasCompactImports=0
LDLIBS   := -lpthread -lm

SOURCES := sim.c \
           $(APPSYS)/answer_headers.c $(APPSYS)/app.c $(APPSYS)/app_api_draw.c $(APPSYS)/app_api_ui.c $(APPSYS)/app_api_data.c \
           $(APPSYS)/app_api_time.c $(APPSYS)/app_api_sys.c \
           $(APPSYS)/app_scene.c $(APPSYS)/app_delivery.c $(COMP)/appstore/app_store.c \
           $(COMP)/appstore/app_manifest.c $(COMP)/appstore/poll_schedule.c $(COMP)/appstore/json_tree.c $(COMP)/appstore/text_template.c \
           $(SDK)/lib/json_pick.c \
           $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c \
           $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(COMP)/words/words.c $(GFX)/font_square.c \
           $(FW)/main/screens.c $(COMP)/appconf/appconf.c $(FW)/main/header.c $(FW)/main/scene_menu.c $(COMP)/deadlines/deadline_table.c $(QR)/qrcodegen/qrcodegen.c
WASM3_SOURCES := $(wildcard $(WASM3)/*.c)

# The page of the tools, which the simulator serves at its address.
PAGE := $(ROOT)/tools/cli/src/main/resources/dev/perinkpheral/cli/page.html

$(BUILD)/perinkpheral-sim: $(SOURCES) $(WASM3_SOURCES) $(BUILD)/host_imports.inc $(BUILD)/page.inc
	mkdir -p $(BUILD)/wasm3
	for source in $(WASM3_SOURCES); do \
		$(CC) $(WASM3_FLAGS) $(ARCH) -c $$source -o $(BUILD)/wasm3/$$(basename $$source .c).o || exit 1; \
	done
	$(CC) $(CFLAGS) $(ARCH) -o $@ $(SOURCES) $(BUILD)/wasm3/*.o $(LDLIBS) $(LDFLAGS)

$(BUILD)/test-jobs: test_jobs.c $(SOURCES) $(WASM3_SOURCES) $(BUILD)/host_imports.inc $(BUILD)/page.inc
	$(MAKE) $(BUILD)/perinkpheral-sim
	$(CC) $(CFLAGS) -o $@ test_jobs.c $(filter-out sim.c,$(SOURCES)) $(BUILD)/wasm3/*.o $(LDLIBS) $(LDFLAGS)

test: $(BUILD)/test-jobs
	$(BUILD)/test-jobs

.PHONY: test

# The page as bytes for C, by POSIX od and sed.
$(BUILD)/page.inc: $(PAGE)
	mkdir -p $(BUILD)
	od -An -v -tx1 $(PAGE) | sed -e 's/  */ /g' -e 's/^ //' -e 's/ $$//' -e 's/\([0-9a-f][0-9a-f]\)/0x\1,/g' > $@

$(BUILD)/host_imports.inc: $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	mkdir -p $(BUILD)
	$(ROOT)/tools/perinkpheral imports $(BUILD)

# The binaries the CLion plugin carries. Linux is static (musl): it runs on any
# distribution. Cross-built with zig.
universal:
	$(MAKE) BUILD=build/macos OPT=-O2 ARCH="-arch arm64 -arch x86_64"

linux-x86_64 linux-aarch64:
	$(MAKE) BUILD=build/$@ OPT=-O2 CC="zig cc -target $(subst linux-,,$@)-linux-musl" LDFLAGS="-static -s"

platforms: universal linux-x86_64 linux-aarch64

.PHONY: universal linux-x86_64 linux-aarch64 platforms
