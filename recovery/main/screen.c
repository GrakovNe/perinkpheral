// SPDX-License-Identifier: MIT
#include "screen.h"

#include <stddef.h>
#include "notice.h"
#include "words.h"

enum { TEXT_MARGIN = 24, HEADING_BASELINE = 32, DETAIL_BASELINE = 222 };

static void detail_draw(gfx_t *graphics, const char *text)
{
    const gfx_font_t *font = gfx_font_system();
    if (gfx_text_font_width(font, text) > graphics->w - 2 * TEXT_MARGIN) {
        font = gfx_font(GFX_FONT_SMALL);
    }
    /* Version headers are at most 31 ASCII characters: keep every character visible. */
    gfx_text_font(graphics, TEXT_MARGIN, DETAIL_BASELINE, font, text, true);
}

void recovery_screen_draw(gfx_t *graphics, recovery_state_t state, const char *version,
                          const char *address, const char *access_point)
{
    gfx_clear(graphics, false);
    notice_t notice = { .icon = GFX_ICON_UPDATE };
    const char *heading = NULL;
    const char *detail = NULL;
    switch (state) {
        case WAITING:
        case FAILED:
            heading = word(state == FAILED ? WORD_UPDATE_FAILED : WORD_FIRMWARE_TITLE);
            notice.first = word(address[0] ? WORD_OPEN : WORD_JOIN_WIFI);
            notice.second = address[0] ? word(WORD_DEVICE_PAGE) : access_point;
            notice.detail = address[0] ? address : "192.168.4.1";
            notice.back = word(WORD_TRY_INSTALLED);
            break;
        case CONFIRM:
            notice.first = word(WORD_UPDATE);
            notice.second = word(WORD_UPDATE_FIRMWARE);
            notice.ok = word(WORD_UPDATE);
            notice.back = word(WORD_CANCEL);
            detail = version;
            break;
        case ACCEPTED:
        case WRITING:
            notice.first = word(WORD_UPDATING);
            notice.second = word(WORD_UPDATING_IN_PROGRESS);
            notice.is_working = true;
            detail = word(WORD_KEEP_DEVICE_ON);
            break;
        case RECEIVING:
        case RETRYING:
        case RESTART:
            notice.first = word(state == RESTART ? WORD_STARTING : WORD_CHECKING);
            notice.second = word(WORD_FIRMWARE);
            notice.is_working = true;
            break;
        case CANCELLED:
            notice.first = word(WORD_UPDATE_CANCELLED);
            break;
    }
    notice_draw(graphics, &notice);
    if (heading != NULL) {
        gfx_text_font(graphics, TEXT_MARGIN, HEADING_BASELINE, gfx_font(GFX_FONT_SMALL), heading, true);
    }
    if (detail != NULL) { detail_draw(graphics, detail); }
}
