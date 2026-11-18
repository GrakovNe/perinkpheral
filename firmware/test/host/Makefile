CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
# POSIX beside C11, as the device's newlib has it (strnlen): glibc hides it under -std=c11
CFLAGS  += -D_DEFAULT_SOURCE
COMP    := ../../components
# Units and other headers with no code, shared by every component.
CFLAGS  += -I$(COMP)/board/include
APPCONF := $(COMP)/appconf
GFX     := $(COMP)/gfx
WORDS   := $(COMP)/words
CFLAGS  += -I$(WORDS)/include
INPUT   := $(COMP)/input
EPD     := $(COMP)/epd
UI      := $(COMP)/ui
APPSYS  := $(COMP)/appsys
APPSTORE := $(COMP)/appstore
CFLAGS  += -I$(APPSTORE)/include
NET     := $(COMP)/net
WASM3   := $(COMP)/wasm3/wasm3
SDK     := ../../../sdk
SDK_LIB := $(SDK)/lib
TOOLS   := ../../../tools/perinkpheral
MKAPP   := $(TOOLS) build
MAIN    := ../../main

# Whatever draws is rebuilt when the drawing code changes.
GFX_SRCS := $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c
GFX_DEPS := $(GFX_SRCS) $(wildcard $(GFX)/include/*.h $(GFX)/*.h $(GFX)/fonts/*.h)
APP_SRCS := $(APPSYS)/app.c $(APPSYS)/app_api_draw.c $(APPSYS)/app_api_ui.c $(APPSYS)/app_api_data.c \
            $(APPSYS)/app_api_time.c $(APPSYS)/app_api_sys.c
QR      := $(COMP)/qr
QR_SRCS := $(QR)/qrcodegen/qrcodegen.c $(QR)/wifi_qr.c
QR_INCS := -I$(QR)/include -I$(QR)/qrcodegen
TESTS   := test_sdk_lib test_json_tree test_appconf test_gfx test_btn_core test_refresh_policy test_frame_box test_deadline_table test_scrollbar test_wm test_notice_queue test_scenes test_wifi_plan test_ota test_recovery_ota test_recovery_settings test_log_ring test_setup test_json_pick test_catalog test_app_manifest test_app_store test_polls test_request_queue test_app_room

# Running a real application needs wasi-sdk to build it.
ifdef WASI_SDK_PATH
TESTS   += test_app
else
$(info WASI_SDK_PATH is not set: skipping test_app)
endif

# The whole way of an answer to an application needs a built application.
ifdef WASI_SDK_PATH
TESTS   += test_delivery
endif

.PHONY: test clean

test: $(TESTS) $(if $(WASI_SDK_PATH),check-abi)
	@for t in $(TESTS); do ./$$t || exit 1; done
	@$(TOOLS) check-languages
	@$(TOOLS) check-layers ../..
	@$(TOOLS) cities --check && echo "cities.h is what cities.json makes"

test_json_tree: test_json_tree.c tinytest.h $(APPSTORE)/json_tree.c $(APPSTORE)/include/json_tree.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -o $@ test_json_tree.c $(APPSTORE)/json_tree.c

test_appconf: test_appconf.c tinytest.h $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -I$(APPCONF)/include -o $@ test_appconf.c $(APPCONF)/appconf.c

test_gfx: test_gfx.c tinytest.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(GFX)/include -I$(GFX)/fonts -o $@ test_gfx.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c

test_btn_core: test_btn_core.c tinytest.h $(INPUT)/btn_core.c $(INPUT)/include/btn_core.h
	$(CC) $(CFLAGS) -I$(INPUT)/include -o $@ test_btn_core.c $(INPUT)/btn_core.c

DEADLINES := ../../components/deadlines
test_deadline_table: test_deadline_table.c tinytest.h $(DEADLINES)/deadline_table.c $(DEADLINES)/include/deadline_table.h
	$(CC) $(CFLAGS) -I$(DEADLINES)/include -o $@ test_deadline_table.c $(DEADLINES)/deadline_table.c

test_scrollbar: test_scrollbar.c tinytest.h $(UI)/scrollbar.c $(UI)/include/scrollbar.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_scrollbar.c $(UI)/scrollbar.c $(GFX)/gfx.c $(GFX)/font_square.c

test_frame_box: test_frame_box.c tinytest.h $(EPD)/frame_box.c $(EPD)/include/frame_box.h
	$(CC) $(CFLAGS) -I$(EPD)/include -o $@ test_frame_box.c $(EPD)/frame_box.c

test_refresh_policy: test_refresh_policy.c tinytest.h $(EPD)/refresh_policy.c $(EPD)/include/refresh_policy.h
	$(CC) $(CFLAGS) -I$(EPD)/include -o $@ test_refresh_policy.c $(EPD)/refresh_policy.c

test_wm: test_wm.c tinytest.h $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(UI)/include/wm.h $(UI)/include/scene.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_wm.c $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c

test_notice_queue: test_notice_queue.c tinytest.h $(UI)/notice_queue.c $(UI)/include/notice_queue.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_notice_queue.c $(UI)/notice_queue.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c

test_scenes: test_scenes.c tinytest.h $(MAIN)/header.c $(MAIN)/header.h $(MAIN)/menu_order.c $(MAIN)/menu_order.h $(MAIN)/scene_menu.c $(MAIN)/scene_splash.c $(MAIN)/scene_confirm.c $(MAIN)/scene_working.c $(MAIN)/scene_storage.c $(MAIN)/screens.c $(MAIN)/scenes.h $(MAIN)/screens.h $(UI)/include/scene.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(MAIN) -I$(SDK) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts $(QR_INCS) -o $@ test_scenes.c \
		$(MAIN)/header.c $(MAIN)/menu_order.c $(MAIN)/scene_menu.c $(MAIN)/scene_splash.c $(MAIN)/scene_confirm.c $(MAIN)/scene_working.c $(MAIN)/scene_storage.c $(MAIN)/screens.c $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c $(QR_SRCS)

# The interpreter is upstream code: built once, without our warnings, and
# limited to one page of memory exactly as on the device.
# The native stack budget is the device's, scaled to the frames an
# unoptimized build with sanitizers makes.
WASM3_FLAGS := -std=c11 -O1 -w -Dd_m3MaxLinearMemoryPages=1 -Dd_m3MaxNativeStack=262144 \
               -Dd_m3VerboseErrorMessages=0 -Dd_m3LogOutput=0 \
               -Dd_m3HasFloat=0 -Dd_m3HasRefTypes=0 -Dd_m3HasExtendedConst=0 -Dd_m3HasCompactImports=0
WASM3_OBJS  := $(patsubst $(WASM3)/%.c,build/wasm3/%.o,$(wildcard $(WASM3)/*.c))

build/wasm3/%.o: $(WASM3)/%.c
	@mkdir -p build/wasm3
	$(CC) $(WASM3_FLAGS) -c -o $@ $<

# A file as the bytes of a C array, by POSIX od: xxd is not everywhere.
c_bytes = od -An -v -tx1 $(1) | sed -e 's/\([0-9a-f][0-9a-f]\)/0x\1,/g'

build/probe/app.wasm: apps/probe/probe.c apps/probe/manifest.json $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h) $(wildcard $(SDK)/lib/*) $(TOOLS)
	$(MKAPP) apps/probe --out build/probe

build/probe_wasm.h: build/probe/app.wasm
	(echo "static const unsigned char probe_wasm[] = {"; $(call c_bytes,$<); \
	 echo "};"; echo "static const unsigned int probe_wasm_len = sizeof probe_wasm;") > $@

build/wrongret/app.wasm: apps/wrongret/wrongret.c apps/wrongret/manifest.json $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h) $(TOOLS)
	@mkdir -p build/wrongret
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) apps/wrongret/wrongret.c -o $@

build/wrongret_wasm.h: build/wrongret/app.wasm
	(echo "static const unsigned char wrongret_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

build/wrongarg/app.wasm: apps/wrongarg/wrongarg.c apps/wrongarg/manifest.json $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h) $(TOOLS)
	@mkdir -p build/wrongarg
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) apps/wrongarg/wrongarg.c -o $@

build/wrongarg_wasm.h: build/wrongarg/app.wasm
	(echo "static const unsigned char wrongarg_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

build/host_imports.inc: $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	$(TOOLS) imports build

build/src/every-call/every_call.c: $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	$(TOOLS) every-call build/src/every-call

build/every_call/app.wasm: build/src/every-call/every_call.c $(TOOLS)
	$(MKAPP) build/src/every-call --out build/every_call

build/every_call_wasm.h: build/every_call/app.wasm
	(echo "static const unsigned char every_call_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

build/frozen_probe_wasm.h: ../../../sdk/abi/3/probe/app.wasm
	@mkdir -p build
	(echo "static const unsigned char frozen_probe_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

test_app: test_app.c tinytest.h build/frozen_probe_wasm.h build/host_imports.inc build/probe_wasm.h build/every_call_wasm.h build/wrongret_wasm.h build/wrongarg_wasm.h $(APP_SRCS) $(APPSYS)/app_internal.h $(APPSYS)/include/app.h $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(WASM3_OBJS)
	$(CC) $(CFLAGS) -Ibuild -I$(APPSYS)/include -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -I$(WASM3) -I$(SDK) -I$(SDK_LIB) \
		-Dd_m3MaxLinearMemoryPages=1 $(QR_INCS) -o $@ test_app.c $(APP_SRCS) $(SDK_LIB)/json_pick.c $(UI)/list.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c $(QR)/qrcodegen/qrcodegen.c $(WASM3_OBJS)

test_wifi_plan: test_wifi_plan.c tinytest.h $(NET)/wifi_plan.c $(NET)/include/wifi_plan.h
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_wifi_plan.c $(NET)/wifi_plan.c

test_setup: test_setup.c tinytest.h $(NET)/captive_dns.c $(NET)/include/captive_dns.h $(QR_SRCS) $(QR)/include/wifi_qr.h
	$(CC) $(CFLAGS) -I$(NET)/include $(QR_INCS) -o $@ test_setup.c $(NET)/captive_dns.c $(QR_SRCS)

test_sdk_lib: test_sdk_lib.c tinytest.h $(wildcard $(SDK_LIB)/*.c) $(wildcard $(SDK_LIB)/*.h) $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	$(CC) $(CFLAGS) -I$(SDK) -I$(SDK_LIB) -o $@ test_sdk_lib.c

test_json_pick: test_json_pick.c tinytest.h $(SDK_LIB)/json_pick.c $(SDK_LIB)/json_pick.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -I$(SDK_LIB) -o $@ test_json_pick.c $(SDK_LIB)/json_pick.c

test_catalog: test_catalog.c tinytest.h $(APPSTORE)/catalog.c $(APPSTORE)/include/catalog.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -o $@ test_catalog.c $(APPSTORE)/catalog.c

test_ota: test_ota.c tinytest.h ../../../recovery/main/control.h $(NET)/ota_stream.c $(NET)/ota_health.c $(wildcard $(NET)/include/ota_*.h)
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_ota.c $(NET)/ota_stream.c $(NET)/ota_health.c

test_recovery_ota: test_recovery_ota.c tinytest.h ../../../recovery/main/ota.c ../../../recovery/main/ota.h ../../../recovery/main/control.h $(wildcard recovery_stubs/*.h) $(NET)/ota_stream.c $(NET)/include/ota_stream.h
	$(CC) $(CFLAGS) -DRECOVERY_LAYOUT=0x1234abcd -Irecovery_stubs -I$(NET)/include -o $@ test_recovery_ota.c ../../../recovery/main/ota.c $(NET)/ota_stream.c

test_recovery_settings: test_recovery_settings.c tinytest.h ../../../recovery/main/settings.c ../../../recovery/main/settings.h $(wildcard recovery_stubs/*.h) $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -Irecovery_stubs -I$(APPCONF)/include -o $@ test_recovery_settings.c ../../../recovery/main/settings.c $(APPCONF)/appconf.c

test_log_ring: test_log_ring.c tinytest.h $(MAIN)/log_ring.c $(MAIN)/log_ring.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_log_ring.c $(MAIN)/log_ring.c

MANIFEST_SRCS := $(APPSTORE)/app_manifest.c $(APPSTORE)/json_tree.c $(SDK_LIB)/json_pick.c
MANIFEST_FLAGS := -I$(APPSYS)/include -I$(SDK) -I$(SDK_LIB)

test_app_manifest: test_app_manifest.c tinytest.h $(MANIFEST_SRCS) $(APPSTORE)/include/app_manifest.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_app_manifest.c $(MANIFEST_SRCS) -lm

test_app_room: test_app_room.c tinytest.h $(APPSTORE)/app_room.c $(APPSTORE)/include/app_room.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_app_room.c $(APPSTORE)/app_room.c

test_request_queue: test_request_queue.c tinytest.h $(APPSTORE)/request_queue.c $(APPSTORE)/include/request_queue.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -o $@ test_request_queue.c $(APPSTORE)/request_queue.c

test_polls: test_polls.c tinytest.h $(MANIFEST_SRCS) $(APPSTORE)/text_template.c $(APPSTORE)/poll_schedule.c $(wildcard $(APPSYS)/include/*.h $(APPSTORE)/include/*.h)
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_polls.c $(MANIFEST_SRCS) $(APPSTORE)/text_template.c \
		$(APPSTORE)/poll_schedule.c -lm

# mkdtemp and friends are POSIX, not C11.
test_delivery: test_delivery.c tinytest.h build/host_imports.inc build/probe/app.wasm $(wildcard $(APPSYS)/*.c $(APPSYS)/include/*.h $(APPSTORE)/*.c $(APPSTORE)/include/*.h) $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(WASM3_OBJS) $(MANIFEST_SRCS)
	$(CC) $(CFLAGS) -Ibuild -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(MANIFEST_FLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -I$(WASM3) \
		-Dd_m3MaxLinearMemoryPages=1 $(QR_INCS) -o $@ test_delivery.c $(QR)/qrcodegen/qrcodegen.c $(APP_SRCS) $(APPSYS)/app_scene.c $(APPSYS)/app_delivery.c \
		$(APPSTORE)/app_store.c $(MANIFEST_SRCS) $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c $(WASM3_OBJS) -lm

test_app_store: test_app_store.c tinytest.h $(APPSTORE)/app_store.c $(APPSTORE)/include/app_store.h $(MANIFEST_SRCS)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(MANIFEST_FLAGS) -o $@ test_app_store.c \
		$(APPSTORE)/app_store.c $(MANIFEST_SRCS) -lm

clean:
	rm -rf build
	rm -f $(TESTS)

# Frozen contracts and packages are inputs, never rebuilt by this target.
.PHONY: check-abi
check-abi: build/probe/app.wasm build/wrongarg/app.wasm build/wrongret/app.wasm build/wrongimport/app.wasm
	@$(TOOLS) contract check
	@$(TOOLS) check-wasm build/probe/app.wasm
	@$(TOOLS) check-wasm ../../../sdk/abi/3/probe/app.wasm
	@$(TOOLS) check-wasm ../../../sdk/abi/3/hello/app.wasm
	@if $(TOOLS) check-wasm build/wrongarg/app.wasm > build/wrongarg/check.log 2>&1; then cat build/wrongarg/check.log; exit 1; fi
	@grep -q 'export app_on_timer has wasm type' build/wrongarg/check.log
	@if $(TOOLS) check-wasm build/wrongret/app.wasm > build/wrongret/check.log 2>&1; then cat build/wrongret/check.log; exit 1; fi
	@grep -q 'export app_strings_buffer has wasm type' build/wrongret/check.log
	@if $(TOOLS) check-wasm build/wrongimport/app.wasm > build/wrongimport/check.log 2>&1; then cat build/wrongimport/check.log; exit 1; fi
	@grep -q 'import host.gfx_width has wasm type' build/wrongimport/check.log

build/wrongimport/app.wasm: apps/wrongimport/wrongimport.c $(TOOLS)
	@mkdir -p build/wrongimport
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) $< -o $@
