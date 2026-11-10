CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
COMP    := ../../components
APPCONF := $(COMP)/appconf
GFX     := $(COMP)/gfx
INPUT   := $(COMP)/input
EPD     := $(COMP)/epd
UI      := $(COMP)/ui
MAIN    := ../../main

# Whatever draws is rebuilt when the drawing code changes.
GFX_SRCS := $(GFX)/gfx.c $(GFX)/font_square.c
GFX_DEPS := $(GFX_SRCS) $(wildcard $(GFX)/include/*.h $(GFX)/*.h $(GFX)/fonts/*.h)
TESTS   := test_appconf test_gfx test_btn_core test_refresh_policy test_wm test_scenes


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

clean:
	rm -f $(TESTS)
