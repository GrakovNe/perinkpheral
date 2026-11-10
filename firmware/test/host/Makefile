CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
COMP    := ../../components
APPCONF := $(COMP)/appconf
GFX     := $(COMP)/gfx
INPUT   := $(COMP)/input
EPD     := $(COMP)/epd
UI      := $(COMP)/ui
APPSYS  := $(COMP)/appsys
WASM3   := $(COMP)/wasm3/wasm3
SDK     := ../../../sdk
MKAPP   := ../../../tools/mkapp/mkapp.py
MAIN    := ../../main

# Whatever draws is rebuilt when the drawing code changes.
GFX_SRCS := $(GFX)/gfx.c $(GFX)/font_square.c
GFX_DEPS := $(GFX_SRCS) $(wildcard $(GFX)/include/*.h $(GFX)/*.h $(GFX)/fonts/*.h)
TESTS   := test_appconf test_gfx test_btn_core test_refresh_policy test_wm test_scenes

# Running a real application needs wasi-sdk to build it.
ifdef WASI_SDK_PATH
TESTS   += test_app
else
$(info WASI_SDK_PATH is not set: skipping test_app)
endif

.PHONY: test clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_appconf: test_appconf.c tinytest.h $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -I$(APPCONF)/include -o $@ test_appconf.c $(APPCONF)/appconf.c

test_gfx: test_gfx.c tinytest.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(GFX)/include -I$(GFX)/fonts -o $@ test_gfx.c $(GFX)/gfx.c $(GFX)/font_square.c

test_btn_core: test_btn_core.c tinytest.h $(INPUT)/btn_core.c $(INPUT)/include/btn_core.h
	$(CC) $(CFLAGS) -I$(INPUT)/include -o $@ test_btn_core.c $(INPUT)/btn_core.c

test_refresh_policy: test_refresh_policy.c tinytest.h $(EPD)/refresh_policy.c $(EPD)/include/refresh_policy.h
	$(CC) $(CFLAGS) -I$(EPD)/include -o $@ test_refresh_policy.c $(EPD)/refresh_policy.c

test_wm: test_wm.c tinytest.h $(UI)/wm.c $(UI)/include/wm.h $(UI)/include/scene.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_wm.c $(UI)/wm.c $(GFX)/gfx.c $(GFX)/font_square.c

test_scenes: test_scenes.c tinytest.h $(MAIN)/scene_menu.c $(MAIN)/scene_splash.c $(MAIN)/screens.c $(MAIN)/scenes.h $(MAIN)/screens.h $(UI)/include/scene.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(MAIN) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_scenes.c \
		$(MAIN)/scene_menu.c $(MAIN)/scene_splash.c $(MAIN)/screens.c $(GFX)/gfx.c $(GFX)/font_square.c

# The interpreter is upstream code: built once, without our warnings, and
# limited to one page of memory exactly as on the device.
WASM3_FLAGS := -std=c11 -O1 -w -Dd_m3MaxLinearMemoryPages=1 -Dd_m3VerboseErrorMessages=0 -Dd_m3LogOutput=0
WASM3_OBJS  := $(patsubst $(WASM3)/%.c,build/wasm3/%.o,$(wildcard $(WASM3)/*.c))

build/wasm3/%.o: $(WASM3)/%.c
	@mkdir -p build/wasm3
	$(CC) $(WASM3_FLAGS) -c -o $@ $<

build/probe/app.wasm: apps/probe/probe.c $(SDK)/host_api.h $(wildcard $(SDK)/lib/*) $(MKAPP)
	$(MKAPP) apps/probe --out build/probe

build/probe_wasm.h: build/probe/app.wasm
	(echo "static const unsigned char probe_wasm[] = {"; xxd -i < $<; \
	 echo "};"; echo "static const unsigned int probe_wasm_len = sizeof probe_wasm;") > $@

test_app: test_app.c tinytest.h build/probe_wasm.h $(APPSYS)/app.c $(APPSYS)/include/app.h $(GFX)/gfx.c $(WASM3_OBJS)
	$(CC) $(CFLAGS) -Ibuild -I$(APPSYS)/include -I$(GFX)/include -I$(GFX)/fonts -I$(WASM3) -I$(SDK) \
		-Dd_m3MaxLinearMemoryPages=1 -o $@ test_app.c $(APPSYS)/app.c $(GFX)/gfx.c $(GFX)/font_square.c $(WASM3_OBJS)

clean:
	rm -rf build
	rm -f $(TESTS)
