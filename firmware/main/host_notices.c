/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What the host puts in front of the user over whatever is on the screen:
 * a question another task waits on, the notices applications asked for,
 * and the screen of work that must not be interrupted. In the host loop,
 * but for the calls that say otherwise.
 */
#include <string.h>

#include "host.h"
#include "host_private.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "deadlines.h"
#include "events.h"
#include "host_api.h"
#include "notice_queue.h"
#include "scenes.h"

static const char *TAG = "host";

/* ---- a question for whoever stands at the device ---- */

#define CONFIRM_WAIT_MS  60000
#define CONFIRM_TEXT_MAX 47

/* One question at a time. The task that asks waits here for the answer;
 * the question itself lives in the host loop. */
static struct {
    SemaphoreHandle_t turn;     /* held by the task whose question is open */
    SemaphoreHandle_t answered;
    bool              is_open;  /* the host loop's */
    bool              is_confirmed;
    bool              is_dismissed; /* answered: the screen goes back once the handler returned */
    scene_t          *scene;
    char              first[CONFIRM_TEXT_MAX + 1];  /* the question, in two lines */
    char              second[CONFIRM_TEXT_MAX + 1];
    int               icon;
    char              action[CONFIRM_TEXT_MAX + 1];
} s_confirm;

/* In the host loop: from the scene. The first call is the answer. */
static void on_confirm_answer(bool is_confirmed)
{
    /* Set aside for a delivery is not an answer: the question is back in a
     * moment. */
    if (!s_confirm.is_open || (host_is_delivering() && !is_confirmed)) {
        return;
    }
    s_confirm.is_open = false;
    s_confirm.is_confirmed = is_confirmed;
    s_confirm.is_dismissed = true;
    deadlines_cancel(DEADLINE_CONFIRM, 0, 0);
    ESP_LOGW(TAG, "\"%s %s\" %s", s_confirm.first, s_confirm.second,
             is_confirmed ? "confirmed" : "not confirmed");
    xSemaphoreGive(s_confirm.answered);
}

/* The question is answered and its scene is still in the stack: on the
 * screen, set aside for an answer in pieces, or under the menu that was
 * opened over it. It comes out, and what was under it is there again. Not
 * from inside a scene's own handler. */
static void dismiss_question(void)
{
    s_confirm.is_dismissed = false;
    wm_close(&host_wm, s_confirm.scene);
}

static void open_confirm(void *argument)
{
    (void)argument;
    s_confirm.is_open = true;
    s_confirm.is_dismissed = false;
    s_confirm.scene = scene_confirm(s_confirm.first, s_confirm.second, s_confirm.icon,
                                    s_confirm.action, on_confirm_answer);
    wm_open(&host_wm, s_confirm.scene);
    /* The same scene with another question is another picture. */
    wm_invalidate(&host_wm, WM_CHANGED_BY_SWITCH);
    if (!deadlines_set_in(DEADLINE_CONFIRM, 0, 0, CONFIRM_WAIT_MS)) {
        on_confirm_answer(false); /* nothing would end the wait: not asked at all */
    }
}

void host_confirm_timed_out(void)
{
    if (s_confirm.is_open) {
        on_confirm_answer(false); /* nobody came */
    }
}

host_answer_t host_confirm_on_device(const char *first, const char *second, int icon,
                                     const char *action)
{
    /* The answer comes from the host loop: asked there, it would never come. */
    configASSERT(!events_is_host_loop());
    if (s_confirm.turn == NULL || xSemaphoreTake(s_confirm.turn, 0) != pdTRUE) {
        return HOST_ANSWER_BUSY;
    }
    HOST_COPY_TEXT(s_confirm.first, first);
    HOST_COPY_TEXT(s_confirm.second, second);
    s_confirm.icon = icon;
    HOST_COPY_TEXT(s_confirm.action, action);
    host_answer_t answer = HOST_ANSWER_BUSY;
    if (events_call(open_confirm, NULL)) {
        /* The host loop answers for certain: by a press, or by the deadline. */
        xSemaphoreTake(s_confirm.answered, portMAX_DELAY);
        answer = s_confirm.is_confirmed ? HOST_ANSWER_CONFIRMED : HOST_ANSWER_NOT_CONFIRMED;
    }
    xSemaphoreGive(s_confirm.turn);
    return answer;
}

/* ---- notifications ---- */

/* The queue holds what an application hands over, whole. */
_Static_assert(NOTICE_QUEUE_LINE_BYTES == sizeof(((host_notice_t *)0)->first) &&
                   NOTICE_QUEUE_LINE_BYTES == sizeof(((host_notice_t *)0)->second) &&
                   NOTICE_QUEUE_DETAIL_BYTES == sizeof(((host_notice_t *)0)->detail),
               "a notice waits as the contract has it");

static notice_queue_t s_notices;
static scene_t       *s_notice_scene;

bool host_notify(const char *first, const char *second, const char *detail, int icon)
{
    return notice_queue_add(&s_notices, first, second, detail, icon);
}

static void on_notice_seen(void)
{
    notice_queue_seen(&s_notices);
}

static const char *line_or_none(const char *text)
{
    return text[0] != '\0' ? text : NULL;
}

/* The user left a notice with the long press that empties the stack: they want the menu, and
 * the notices that wait are shown at their next press, not over it at once. */
static bool s_is_held_by_user;

void host_notices_note_press(void)
{
    s_is_held_by_user = false;
}

/* The one that was dismissed leaves, and the next one that waits is shown. */
static void process_notifications(void)
{
    if (notice_queue_take_dismissed(&s_notices)) {
        wm_close(&host_wm, s_notice_scene);
    }
    /* The user's own way out - the long press that empties the stack -
     * took it off the screen: seen. */
    if (notice_queue_is_shown(&s_notices) && !wm_has(&host_wm, s_notice_scene)) {
        notice_queue_gone(&s_notices);
        s_is_held_by_user = true;
    }
    if (host_is_delivering() || s_is_held_by_user) {
        return;
    }
    const notice_told_t *told = notice_queue_next(&s_notices);
    if (told != NULL) {
        s_notice_scene = scene_notify(line_or_none(told->first), line_or_none(told->second),
                                      line_or_none(told->detail), told->icon, on_notice_seen);
        wm_open(&host_wm, s_notice_scene);
        wm_invalidate(&host_wm, WM_CHANGED_URGENTLY);
    }
}

void host_notices_after_event(void)
{
    if (s_confirm.is_dismissed) {
        dismiss_question();
    }
    process_notifications();
}

void host_notices_start(void)
{
    s_confirm.turn = xSemaphoreCreateMutex();
    s_confirm.answered = xSemaphoreCreateBinary();
    configASSERT(s_confirm.turn != NULL && s_confirm.answered != NULL);
    notice_queue_init(&s_notices);
}

/* ---- the device is busy with something that must not be interrupted ---- */

static scene_t *s_working; /* in the stack while it is; NULL otherwise */

static void open_working(void *argument)
{
    const char *const *lines = argument;
    /* The question that led here is put away first: it is not what the
     * screen goes back to. */
    if (s_confirm.is_dismissed) {
        dismiss_question();
    }
    s_working = scene_working(lines[0], lines[1]);
    wm_open(&host_wm, s_working);
}

static void close_working(void *argument)
{
    (void)argument;
    if (s_working == NULL) {
        return;
    }
    wm_close(&host_wm, s_working);
    s_working = NULL;
}

/* Whoever is told that the work began has just pressed a button: the
 * notice is on the panel before the work - which may well keep the host
 * loop from the panel for seconds, as erasing the flash does - begins. */
#define WORKING_SHOWN_WAIT_MS 5000
#define WORKING_SHOWN_POLL_MS 50

void host_show_working(const char *first, const char *second)
{
    const char *lines[] = { first, second };
    if (!events_call(open_working, lines)) {
        return; /* a busy queue: the work goes on unannounced */
    }
    for (int waited_ms = 0; waited_ms < WORKING_SHOWN_WAIT_MS && host_scene_on_panel() != s_working;
         waited_ms += WORKING_SHOWN_POLL_MS) {
        vTaskDelay(pdMS_TO_TICKS(WORKING_SHOWN_POLL_MS));
    }
}

void host_work_is_over(void)
{
    events_call_patiently(close_working, NULL);
}
