CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
COMP    := ../../components
APPCONF := $(COMP)/appconf
GFX     := $(COMP)/gfx
INPUT   := $(COMP)/input
EPD     := $(COMP)/epd
UI      := $(COMP)/ui
APPSYS  := $(COMP)/appsys
NET     := $(COMP)/net
WASM3   := $(COMP)/wasm3/wasm3
SDK     := ../../../sdk
MKAPP   := ../../../tools/mkapp/mkapp.py
MAIN    := ../../main

# Whatever draws is rebuilt when the drawing code changes.
GFX_SRCS := $(GFX)/gfx.c $(GFX)/font_square.c
GFX_DEPS := $(GFX_SRCS) $(wildcard $(GFX)/include/*.h $(GFX)/*.h $(GFX)/fonts/*.h)
QR      := $(COMP)/qr
QR_SRCS := $(QR)/qrcodegen/qrcodegen.c $(QR)/wifi_qr.c
QR_INCS := -I$(QR)/include -I$(QR)/qrcodegen
TESTS   := test_appconf test_gfx test_btn_core test_refresh_policy test_wm test_scenes test_wifi_plan test_ota test_log_ring test_setup test_json_pick

# The manifest is JSON; the parser comes with ESP-IDF.
ifdef IDF_PATH
TESTS   += test_app_manifest test_app_store test_polls
else
$(info IDF_PATH is not set: skipping test_app_manifest and test_app_store)
endif

# Running a real application needs wasi-sdk to build it.
ifdef WASI_SDK_PATH
TESTS   += test_app
else
$(info WASI_SDK_PATH is not set: skipping test_app)
endif

# The whole way of an answer to an application needs the manifest reader
# and a built application both.
ifdef IDF_PATH
ifdef WASI_SDK_PATH
TESTS   += test_delivery
endif
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
	$(CC) $(CFLAGS) -I$(MAIN) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts $(QR_INCS) -o $@ test_scenes.c \
		$(MAIN)/scene_menu.c $(MAIN)/scene_splash.c $(MAIN)/screens.c $(GFX)/gfx.c $(GFX)/font_square.c $(QR_SRCS)

# The interpreter is upstream code: built once, without our warnings, and
# limited to one page of memory exactly as on the device.
# The native stack budget is the device's, scaled to the frames an
# unoptimized build with sanitizers makes.
WASM3_FLAGS := -std=c11 -O1 -w -Dd_m3MaxLinearMemoryPages=1 -Dd_m3MaxNativeStack=262144 \
               -Dd_m3VerboseErrorMessages=0 -Dd_m3LogOutput=0
WASM3_OBJS  := $(patsubst $(WASM3)/%.c,build/wasm3/%.o,$(wildcard $(WASM3)/*.c))

build/wasm3/%.o: $(WASM3)/%.c
	@mkdir -p build/wasm3
	$(CC) $(WASM3_FLAGS) -c -o $@ $<

build/probe/app.wasm: apps/probe/probe.c apps/probe/manifest.json $(SDK)/host_api.h $(wildcard $(SDK)/lib/*) $(MKAPP)
	$(MKAPP) apps/probe --out build/probe

build/probe_wasm.h: build/probe/app.wasm
	(echo "static const unsigned char probe_wasm[] = {"; xxd -i < $<; \
	 echo "};"; echo "static const unsigned int probe_wasm_len = sizeof probe_wasm;") > $@

test_app: test_app.c tinytest.h build/probe_wasm.h $(APPSYS)/app.c $(APPSYS)/include/app.h $(GFX)/gfx.c $(WASM3_OBJS)
	$(CC) $(CFLAGS) -Ibuild -I$(APPSYS)/include -I$(GFX)/include -I$(GFX)/fonts -I$(WASM3) -I$(SDK) \
		-Dd_m3MaxLinearMemoryPages=1 -o $@ test_app.c $(APPSYS)/app.c $(GFX)/gfx.c $(GFX)/font_square.c $(WASM3_OBJS)

test_wifi_plan: test_wifi_plan.c tinytest.h $(NET)/wifi_plan.c $(NET)/include/wifi_plan.h
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_wifi_plan.c $(NET)/wifi_plan.c

test_setup: test_setup.c tinytest.h $(NET)/captive_dns.c $(NET)/include/captive_dns.h $(QR_SRCS) $(QR)/include/wifi_qr.h
	$(CC) $(CFLAGS) -I$(NET)/include $(QR_INCS) -o $@ test_setup.c $(NET)/captive_dns.c $(QR_SRCS)

test_json_pick: test_json_pick.c tinytest.h $(APPSYS)/json_pick.c $(APPSYS)/include/json_pick.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -o $@ test_json_pick.c $(APPSYS)/json_pick.c

test_ota: test_ota.c tinytest.h $(NET)/ota_stream.c $(NET)/ota_health.c $(wildcard $(NET)/include/ota_*.h)
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_ota.c $(NET)/ota_stream.c $(NET)/ota_health.c

test_log_ring: test_log_ring.c tinytest.h $(MAIN)/log_ring.c $(MAIN)/log_ring.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_log_ring.c $(MAIN)/log_ring.c

CJSON := $(IDF_PATH)/components/json/cJSON
MANIFEST_SRCS := $(APPSYS)/app_manifest.c build/cJSON.o
MANIFEST_FLAGS := -I$(APPSYS)/include -I$(CJSON) -I$(SDK)

build/cJSON.o: $(CJSON)/cJSON.c
	@mkdir -p build
	$(CC) -std=c11 -w -c -o $@ $<

test_app_manifest: test_app_manifest.c tinytest.h $(MANIFEST_SRCS) $(APPSYS)/include/app_manifest.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_app_manifest.c $(MANIFEST_SRCS) -lm

test_polls: test_polls.c tinytest.h $(MANIFEST_SRCS) $(APPSYS)/text_template.c $(APPSYS)/poll_schedule.c $(wildcard $(APPSYS)/include/*.h)
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_polls.c $(MANIFEST_SRCS) $(APPSYS)/text_template.c \
		$(APPSYS)/poll_schedule.c -lm

# mkdtemp and friends are POSIX, not C11.
test_delivery: test_delivery.c tinytest.h build/probe/app.wasm $(wildcard $(APPSYS)/*.c $(APPSYS)/include/*.h) $(UI)/wm.c $(WASM3_OBJS) $(MANIFEST_SRCS)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(MANIFEST_FLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -I$(WASM3) \
		-Dd_m3MaxLinearMemoryPages=1 -o $@ test_delivery.c $(APPSYS)/app.c $(APPSYS)/app_scene.c $(APPSYS)/app_delivery.c \
		$(APPSYS)/app_store.c $(MANIFEST_SRCS) $(UI)/wm.c $(GFX)/gfx.c $(GFX)/font_square.c $(WASM3_OBJS) -lm

test_app_store: test_app_store.c tinytest.h $(APPSYS)/app_store.c $(APPSYS)/include/app_store.h $(MANIFEST_SRCS)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(MANIFEST_FLAGS) -o $@ test_app_store.c \
		$(APPSYS)/app_store.c $(MANIFEST_SRCS) -lm

clean:
	rm -rf build
	rm -f $(TESTS)
