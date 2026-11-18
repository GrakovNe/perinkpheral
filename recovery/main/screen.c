// SPDX-License-Identifier: MIT
#include "screen.h"

void recovery_screen_draw(gfx_t *graphics, recovery_state_t state, const char *version,
                          const char *address, const char *access_point)
{
    gfx_clear(graphics, false);
    const gfx_font_t *text = gfx_font(GFX_FONT_TEXT);
    const gfx_font_t *small = gfx_font(GFX_FONT_SMALL);
    gfx_text_font(graphics, 16, 40, text, "Recovery", true);
    gfx_text_font(graphics, 16, 78, small, address[0] ? "Open the device page:" : "Join Wi-Fi:", true);
    gfx_text_font(graphics, 16, 112, text, address[0] ? address : access_point, true);
    gfx_text_font(graphics, 16, 150, small,
                  address[0] ? "Or join perink-recovery" : "Then open the device page:", true);
    gfx_text_font(graphics, 16, 172, small, "http://192.168.4.1", true);
    const char *caption = "Choose a firmware file on the page";
    if (state == CONFIRM) { caption = version; }
    if (state == RECEIVING) { caption = "Checking firmware file"; }
    if (state == RETRYING) { caption = "Checking installed firmware"; }
    if (state == ACCEPTED || state == WRITING) { caption = "Installing. Keep power connected."; }
    if (state == FAILED) { caption = "Failed. Choose a file to try again."; }
    if (state == RESTART) { caption = "Starting firmware"; }
    if (state == CONFIRM) {
        gfx_text_font(graphics, 16, 208, small, "Install firmware:", true);
    }
    gfx_text_font(graphics, 16, 230, small, caption, true);
    gfx_text_font(graphics, 16, 280, small, state == CONFIRM ? "OK Install    BACK Cancel" :
                  state == WAITING || state == FAILED ? "BACK Try installed firmware" : "", true);
}
