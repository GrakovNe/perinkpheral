CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
COMP    := ../../components
APPCONF := $(COMP)/appconf
GFX     := $(COMP)/gfx
INPUT   := $(COMP)/input
EPD     := $(COMP)/epd
TESTS   := test_appconf test_gfx test_btn_core test_refresh_policy

.PHONY: test clean

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

test_appconf: test_appconf.c tinytest.h $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -I$(APPCONF)/include -o $@ test_appconf.c $(APPCONF)/appconf.c

test_gfx: test_gfx.c tinytest.h $(GFX)/gfx.c $(GFX)/font_square.c $(GFX)/include/gfx.h
	$(CC) $(CFLAGS) -I$(GFX)/include -o $@ test_gfx.c $(GFX)/gfx.c $(GFX)/font_square.c

test_btn_core: test_btn_core.c tinytest.h $(INPUT)/btn_core.c $(INPUT)/include/btn_core.h
	$(CC) $(CFLAGS) -I$(INPUT)/include -o $@ test_btn_core.c $(INPUT)/btn_core.c

test_refresh_policy: test_refresh_policy.c tinytest.h $(EPD)/refresh_policy.c $(EPD)/include/refresh_policy.h
	$(CC) $(CFLAGS) -I$(EPD)/include -o $@ test_refresh_policy.c $(EPD)/refresh_policy.c

clean:
	rm -f $(TESTS)
