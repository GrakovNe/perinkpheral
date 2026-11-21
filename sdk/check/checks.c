/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The capability checks: what the contract promises of any application,
 * asked of one that the program was linked with. Nothing here knows which
 * application that is. What it knows comes from the tools as
 * standin_subject: the manifest, and the answers and the store kept beside
 * the application for its checks.
 *
 *     checks <capability>...      sessions store timers network drawing
 *
 * How "the same" is told without knowing the application: by the picture -
 * what app_on_draw() asked the host to draw - and by how many presses of
 * BACK leave it. The clock stands still unless a check moves it, so an
 * application that shows the time shows the same time before an unload and
 * after it.
 *
 * Every scene is a run (standin_live): a process of its own, so an
 * instance starts with nothing of the one before but the store, the files
 * and the session. Each prints lines the tools read:
 *
 *     check <capability> ok|fail <checks made>
 *     problem <capability> <what was expected and what was seen>
 *     note <capability> <what was not checked, and why>
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "standin.h"

/* Made by the tools: the application's handlers into standin_handlers. */
extern void standin_subject_handlers(void);

typedef enum { SESSIONS, STORE, TIMERS, NETWORK, DRAWING, CAPABILITY_COUNT } capability_t;
static const char *const CAPABILITY_NAMES[CAPABILITY_COUNT] = { "sessions", "store", "timers", "network", "drawing" };

#define PROBLEMS_MAX  10
#define PROBLEM_BYTES 480

static struct {
    bool is_asked;
    int  checks;
    int  problem_count, problems_unsaid;
    char problems[PROBLEMS_MAX][PROBLEM_BYTES];
    char notes[4][PROBLEM_BYTES];
    int  note_count;
} results[CAPABILITY_COUNT];

/* The capability whose scenes are running: a fault of the application is its finding. */
static capability_t checking;

static void problem(capability_t capability, const char *format, ...)
{
    char text[PROBLEM_BYTES];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    /* A finding of a capability nobody asked about is said with the one being checked. */
    if (!results[capability].is_asked) {
        capability = checking;
    }
    for (int index = 0; index < results[capability].problem_count; index++) {
        if (strcmp(results[capability].problems[index], text) == 0) {
            return;
        }
    }
    if (results[capability].problem_count >= PROBLEMS_MAX) {
        results[capability].problems_unsaid++;
        return;
    }
    strcpy(results[capability].problems[results[capability].problem_count++], text);
}

static void note(capability_t capability, const char *format, ...)
{
    char text[PROBLEM_BYTES];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    for (int index = 0; index < results[capability].note_count; index++) {
        if (strcmp(results[capability].notes[index], text) == 0) {
            return;
        }
    }
    if (results[capability].note_count < 4) {
        strcpy(results[capability].notes[results[capability].note_count++], text);
    }
}

/* ---- Answers kept beside the application ---- */

typedef struct {
    int32_t        status;
    const uint8_t *body;
    int32_t        length;
} answer_t;

static bool is_answered_by(const char *address, const char *url)
{
    size_t length = strlen(address);
    if (length > 0 && address[length - 1] == '*') {
        return strncmp(address, url, length - 1) == 0;
    }
    return strcmp(address, url) == 0;
}

static bool answer_for(const char *url, answer_t *answer)
{
    static uint8_t *bodies[64];
    static int32_t lengths[64];
    for (int32_t index = 0; index < standin_subject.answer_count && index < 64; index++) {
        const standin_answer_t *kept = &standin_subject.answers[index];
        if (!is_answered_by(kept->address, url)) {
            continue;
        }
        if (kept->file != NULL && bodies[index] == NULL) {
            FILE *file = fopen(kept->file, "rb");
            if (file == NULL) {
                fprintf(stderr, "checks: cannot read %s\n", kept->file);
                exit(2);
            }
            bodies[index] = malloc(HOST_ANSWER_MAX);
            lengths[index] = (int32_t)fread(bodies[index], 1, HOST_ANSWER_MAX, file);
            fclose(file);
        }
        answer->status = kept->status;
        answer->body = kept->file != NULL ? bodies[index] : (const uint8_t *)kept->body;
        answer->length = kept->file != NULL ? lengths[index] : (int32_t)strlen(kept->body);
        return true;
    }
    return false;
}

/* ---- A scene: one instance of the application, told as data ---- */

typedef enum {
    ROUND_NONE,
    ROUND_WHOLE,       /* every answer whole, in the device's pieces */
    ROUND_PIECES,      /* whole, in pieces of `size` bytes */
    ROUND_CUT,         /* every streamed answer stops half way: no LAST, no FAILED */
    ROUND_FAILED_LATE, /* half of every streamed answer, then HOST_DATA_FAILED */
    ROUND_STATUS,      /* every request fails with the HOST_FAILED_* in `size` */
    ROUND_HTTP,        /* every request is answered with the HTTP status in `size` and no data of use */
} round_kind_t;

typedef struct {
    round_kind_t kind;
    int32_t      size;
    int32_t      times; /* 0 is once */
} round_t;

typedef enum { FIRE_NONE, FIRE_ON_TIME, FIRE_LATE, FIRE_TWICE } fire_t;
typedef enum { CLOCK_KEPT, CLOCK_FORWARD, CLOCK_BACK, CLOCK_BECOMES_KNOWN } clock_change_t;

#define ROUNDS_MAX 4

typedef struct {
    bool           is_off_screen;  /* loaded for a delivery: no focus, no blur, nothing drawn */
    bool           is_updated;     /* another package replaced it: app_on_updated() */
    bool           has_settings;   /* the user saved the settings: app_on_settings() */
    int32_t        intent;         /* a HOST_INTENT_* delivered after the load, or 0 */
    bool           has_data;       /* a whole round of answers before anything else */
    const char    *presses;        /* U O D B, lower case for a long press */
    fire_t         fire;           /* the timer that waits soonest */
    bool           has_alarms;     /* every alarm that waits comes */
    int64_t        alarms_late_ms; /* so much after their time */
    clock_change_t clock;
    int32_t        refusal;        /* a HOST_ERR_*: what every write answers while the first round arrives */
    round_t        rounds[ROUNDS_MAX];
} scene_t;

/* What a scene leaves in standin()->marks. */
enum {
    MARK_PICTURE,       /* at its end */
    MARK_FIRST_PICTURE, /* after it took the screen */
    MARK_CALLS,         /* drawing calls of the least frame it drew */
    MARK_DEPTH,         /* presses of BACK that leave it */
    MARK_TIMERS,
    MARK_ALARMS,
    MARK_TIMERS_BEFORE, /* before a timer was fired or the clock set */
    MARK_ALARMS_BEFORE, /* before an update */
    MARK_LEFT,          /* it left the screen itself */
    MARK_WRITES_AGAIN,  /* writes of the flash in the repeats of a round after its first */
    MARK_LONGEST_TIMER, /* milliseconds */
    MARK_UNANSWERED,    /* requests no answer is kept for */
    MARK_ROUND_PICTURE, /* and the three after it: after each round */
    MARK_COUNT = MARK_ROUND_PICTURE + ROUNDS_MAX
};

#define DEPTH_MAX        12
#define NEVER_LEAVES     99
#define REQUESTS_A_ROUND 64 /* answers that ask for more: an application that never stops asking is stopped here */
#define MS_PER_HOUR      3600000LL
#define SECONDS_PER_DAY  86400
/* How late a late timer is: a handler's whole time and a refresh of the panel before it. */
#define TIMER_LATE_MS    5000

/* The time the checks are made at: what a fresh host says, kept for a scene that takes the clock away. */
static int64_t time_known;

static uint64_t frame(void)
{
    standin_t *host = standin();
    uint64_t picture = standin_draw();
    if (host->marks[MARK_CALLS] == 0 || (uint64_t)host->draw_calls < host->marks[MARK_CALLS] - 1) {
        host->marks[MARK_CALLS] = (uint64_t)host->draw_calls + 1; /* 1 is a frame of no calls */
    }
    return picture;
}

/* What the panel shows (app_scene.c, app.c): a frame is drawn when the application takes the
 * screen and again when it asked for one - screen_redraw(), or the screen's inversion changed.
 * A state it changed without asking stays unseen. Of one instance on the screen. */
static struct {
    bool     is_shown;
    uint64_t picture;
    int32_t  redraws;
    bool     is_inverted;
} shown;

static uint64_t show(void)
{
    standin_t *host = standin();
    if (!shown.is_shown || host->redraws != shown.redraws || host->is_inverted != shown.is_inverted) {
        shown.picture = frame();
        shown.is_shown = true;
        shown.redraws = host->redraws;
        shown.is_inverted = host->is_inverted;
    }
    return standin_screen(shown.picture);
}

static void back_out(void *context)
{
    (void)context;
    standin_t *host = standin();
    int32_t presses = 0;
    while (!host->is_leaving && presses < DEPTH_MAX) {
        standin_press(BTN_BACK);
        presses++;
    }
    host->marks[MARK_DEPTH] = host->is_leaving ? (uint64_t)presses : NEVER_LEAVES;
}

static void deliver(int32_t request, int32_t item, bool is_picked, const char *url, bool is_ready, bool is_permitted,
                    const round_t *round)
{
    standin_t *host = standin();
    answer_t answer = { 0 };
    int32_t status = 200;
    if (round->kind == ROUND_STATUS) {
        status = round->size;
    } else if (!is_ready) {
        status = HOST_FAILED_NOT_READY;
    } else if (!is_permitted) {
        status = HOST_FAILED_NOT_ALLOWED;
    } else if (round->kind == ROUND_HTTP) {
        static const char UNAVAILABLE[] = "Service Unavailable";
        answer = (answer_t){ round->size, (const uint8_t *)UNAVAILABLE, (int32_t)sizeof UNAVAILABLE - 1 };
        status = round->size;
    } else if (answer_for(url, &answer)) {
        status = answer.status;
    } else {
        host->marks[MARK_UNANSWERED]++;
        standin_note("no answer is kept for %s", url);
        status = HOST_FAILED_NO_ANSWER;
    }
    /* An answer that stops is some pieces and then nothing: of a request that
     * failed there are no pieces, and Perinkpheral OS reads an answer it picks
     * values from to its end itself. Either way the application is told nothing. */
    if (round->kind == ROUND_CUT && (is_picked || status < 0)) {
        return;
    }
    if (is_picked) {
        /* One that fails late is, for the application, one that failed. */
        if (round->kind == ROUND_FAILED_LATE) {
            status = HOST_FAILED_NO_ANSWER;
        }
        standin_picked(request, item, status, answer.body, answer.length);
        return;
    }
    /* A request with no pieces to give fails the way the round's answers fail. */
    if (round->kind == ROUND_FAILED_LATE && status < 0) {
        status = HOST_FAILED_NO_ANSWER;
    }
    int32_t piece = round->kind == ROUND_PIECES ? round->size : HOST_DATA_CHUNK_MAX;
    int32_t ending = round->kind == ROUND_CUT ? STANDIN_CUT : round->kind == ROUND_FAILED_LATE ? STANDIN_FAILED : STANDIN_WHOLE;
    standin_data(request, item, status, answer.body, answer.length, piece, ending, HOST_FAILED_NO_ANSWER);
}

/* One thing the device gives an application. Off the screen each is given to an instance
 * loaded for it and unloaded after it (app_delivery.c), so nothing but the store, the files and
 * the session carries from one to the next; on the screen the instance there takes them all. */
typedef struct {
    int32_t        alarm;   /* >= 0: this alarm comes */
    int32_t        intent;  /* a HOST_INTENT_*, or 0 */
    bool           is_updated;
    int32_t        request; /* otherwise an answer: a poll or a request's tag */
    int32_t        item;
    bool           is_picked, is_ready, is_permitted;
    char           url[STANDIN_URL_BYTES];
    const round_t *round;
} delivery_t;

static void give(const delivery_t *delivery)
{
    if (delivery->is_updated) {
        standin_updated();
    } else if (delivery->intent != 0) {
        standin_intent(delivery->intent);
    } else if (delivery->alarm >= 0) {
        standin_alarm(delivery->alarm);
    } else {
        deliver(delivery->request, delivery->item, delivery->is_picked, delivery->url, delivery->is_ready,
                delivery->is_permitted, delivery->round);
    }
}

static void given_alone(void *context)
{
    standin_load();
    give(context);
    standin_terminate();
}

/* Off the screen: false once an instance did not live through what it was given. */
static bool is_away;
static bool has_away_failed;

static void hand(const delivery_t *delivery)
{
    if (!is_away) {
        give(delivery);
    } else if (!has_away_failed && !standin_live(given_alone, (void *)delivery)) {
        has_away_failed = true;
    }
}

/* An application that asks by itself asks again when its repeating alarm
 * comes. The alarms come with the clock where it was: a picture with the
 * time in it stays comparable. */
static void ask_again(void)
{
    for (int32_t id = 0; id < HOST_ALARM_COUNT; id++) {
        if (standin()->alarms[id].kind == STANDIN_ALARM_EVERY) {
            hand(&(delivery_t){ .alarm = id });
        }
    }
}

/* Every poll of the manifest, an item of its list at a time, then the requests the application
 * made, each sent to the address it was queued with, authorized again and its headers filled. */
static void answer_round(const round_t *round)
{
    standin_t *host = standin();
    for (int32_t poll = 0; poll < standin_subject.poll_count; poll++) {
        int32_t items = standin_poll_items(poll);
        for (int32_t index = 0; index < (items < 0 ? 1 : items); index++) {
            delivery_t answer = { .alarm = -1, .request = poll, .item = items < 0 ? -1 : standin_poll_item(poll, index),
                                  .is_picked = standin_subject.polls[poll].picks != NULL, .is_permitted = true,
                                  .round = round };
            answer.is_ready = standin_poll_url(poll, answer.item, answer.url, sizeof answer.url);
            hand(&answer);
        }
    }
    for (int32_t answered = 0; host->request_count > 0 && answered < REQUESTS_A_ROUND && !has_away_failed;
         answered++) {
        standin_request_t request = host->requests[0];
        host->request_count--;
        memmove(&host->requests[0], &host->requests[1], (size_t)host->request_count * sizeof host->requests[0]);
        delivery_t answer = { .alarm = -1, .request = request.tag, .item = -1, .round = round,
                              .is_ready = standin_headers_ready(&request), .is_permitted = standin_url_allowed(request.url) };
        snprintf(answer.url, sizeof answer.url, "%s", request.url);
        hand(&answer);
    }
}

static int32_t soonest_timer(void)
{
    int32_t soonest = -1;
    for (int32_t id = 0; id < HOST_TIMER_COUNT; id++) {
        int32_t left = standin_timer_left(id);
        if (left > 0 && (soonest < 0 || left < standin_timer_left(soonest))) {
            soonest = id;
        }
    }
    return soonest;
}

static void press(char key)
{
    static const char KEYS[] = "UODB";
    char upper = (char)(key >= 'a' ? key - 'a' + 'A' : key);
    int32_t pressed = (int32_t)(strchr(KEYS, upper) - KEYS) + BTN_UP;
    if (key >= 'a') {
        standin_long_press(pressed);
    } else {
        standin_press(pressed);
    }
}

static void scene_run(void *context)
{
    const scene_t *scene = context;
    standin_t *host = standin();
    memset(host->marks, 0, sizeof host->marks);
    host->note_count = 0;

    if (!is_away) {
        standin_load();
    }
    if (scene->is_updated) {
        host->marks[MARK_ALARMS_BEFORE] = standin_alarms();
        hand(&(delivery_t){ .alarm = -1, .is_updated = true });
    }
    if (scene->intent != 0) {
        hand(&(delivery_t){ .alarm = -1, .intent = scene->intent });
    }
    if (!scene->is_off_screen) {
        standin_focus();
        shown.is_shown = false;
        host->marks[MARK_FIRST_PICTURE] = show();
    }
    if (scene->has_settings) {
        standin_settings();
    }
    if (scene->has_data) {
        answer_round(&(round_t){ ROUND_WHOLE, 0, 0 });
    }
    for (const char *key = scene->presses; key != NULL && *key != '\0' && !host->is_leaving; key++) {
        press(*key);
    }
    /* The presses are shown before anything else comes, as the panel shows them. */
    if (!scene->is_off_screen && scene->presses != NULL && scene->presses[0] != '\0') {
        show();
    }
    host->marks[MARK_TIMERS_BEFORE] = standin_timers();
    int32_t timer = soonest_timer();
    if (scene->fire != FIRE_NONE && timer >= 0 && !host->is_leaving) {
        standin_pass(standin_timer_left(timer) + (scene->fire == FIRE_LATE ? TIMER_LATE_MS : 0));
        /* What came due meanwhile comes too, as on the device: an alarm and the timer at one minute. */
        for (int32_t id = 0; id < HOST_ALARM_COUNT; id++) {
            if (standin_alarm_due(id)) {
                hand(&(delivery_t){ .alarm = id });
            }
        }
        standin_timer(timer);
        if (scene->fire == FIRE_TWICE) {
            standin_timer(timer);
        }
    }
    if (scene->clock != CLOCK_KEPT) {
        standin_clock(scene->clock == CLOCK_BECOMES_KNOWN ? time_known
                      : host->now + (scene->clock == CLOCK_FORWARD ? SECONDS_PER_DAY : -SECONDS_PER_DAY));
        standin_intent(HOST_INTENT_TIME_CHANGED);
    }
    for (int32_t id = 0; scene->has_alarms && id < HOST_ALARM_COUNT; id++) {
        if (standin_alarm_left(id) > 0) {
            standin_pass((int64_t)standin_alarm_left(id) * 1000 + scene->alarms_late_ms);
            hand(&(delivery_t){ .alarm = id });
        }
    }
    for (int32_t index = 0; index < ROUNDS_MAX && scene->rounds[index].kind != ROUND_NONE; index++) {
        if (index > 0 || scene->has_data || scene->is_off_screen) {
            ask_again();
        }
        host->refuse_writes = index == 0 ? scene->refusal : HOST_OK;
        answer_round(&scene->rounds[index]);
        int32_t writes = host->store_changes + host->file_writes;
        for (int32_t again = 1; again < scene->rounds[index].times; again++) {
            ask_again();
            answer_round(&scene->rounds[index]);
        }
        host->marks[MARK_WRITES_AGAIN] += (uint64_t)(host->store_changes + host->file_writes - writes);
        host->refuse_writes = HOST_OK;
        if (!scene->is_off_screen) {
            host->marks[MARK_ROUND_PICTURE + index] = show();
        }
    }
    host->marks[MARK_TIMERS] = standin_timers();
    for (int32_t id = 0; id < HOST_TIMER_COUNT; id++) {
        if ((uint64_t)standin_timer_left(id) > host->marks[MARK_LONGEST_TIMER]) {
            host->marks[MARK_LONGEST_TIMER] = (uint64_t)standin_timer_left(id);
        }
    }
    host->marks[MARK_ALARMS] = standin_alarms();
    host->marks[MARK_LEFT] = host->is_leaving;
    if (scene->is_off_screen) {
        if (!is_away) {
            standin_terminate();
        }
        return;
    }
    host->marks[MARK_PICTURE] = show();
    /* After the frame: an application that shows pictures in a row sets its timer when one is drawn. */
    host->marks[MARK_TIMERS] = standin_timers();
    host->marks[MARK_DEPTH] = 0;
    if (!host->is_leaving && !standin_try(back_out, NULL)) {
        standin_note("pressing BACK until it leaves: %s", standin_fault());
        host->marks[MARK_DEPTH] = NEVER_LEAVES;
    }
    standin_leave();
}

static const char *round_words(const round_t *round)
{
    static char words[64];
    switch (round->kind) {
    case ROUND_WHOLE: snprintf(words, sizeof words, "a whole answer"); break;
    case ROUND_PIECES: snprintf(words, sizeof words, "an answer in pieces of %d", round->size); break;
    case ROUND_CUT: snprintf(words, sizeof words, "an answer that stops half way"); break;
    case ROUND_FAILED_LATE: snprintf(words, sizeof words, "an answer that fails half way"); break;
    case ROUND_STATUS: snprintf(words, sizeof words, "a request failed with %d", round->size); break;
    case ROUND_HTTP: snprintf(words, sizeof words, "HTTP %d", round->size); break;
    default: words[0] = '\0'; break;
    }
    if (round->times > 1) {
        snprintf(words + strlen(words), sizeof words - strlen(words), ", %d times", round->times);
    }
    return words;
}

/* What the scene did, for a person who reads the finding. */
static const char *told(const scene_t *scene)
{
    static char text[320];
    static const char *const FIRES[] = { "", ", its timer", ", its timer 5 s late", ", its timer twice" };
    static const char *const CLOCKS[] = { "", ", the clock set a day on", ", the clock set a day back", ", the clock set for the first time" };
    snprintf(text, sizeof text, "%s%s%s%s%s%s%s%s%s%s", scene->is_off_screen ? "off the screen" : "on the screen",
             scene->is_updated ? ", just updated" : "",
             scene->intent == HOST_INTENT_STARTED ? ", the device restarted" : "",
             scene->has_settings ? ", settings saved" : "", scene->has_data ? ", answers" : "",
             scene->presses != NULL && scene->presses[0] != '\0' ? ", buttons " : "",
             scene->presses != NULL ? scene->presses : "", FIRES[scene->fire],
             scene->has_alarms ? ", its alarms" : "", CLOCKS[scene->clock]);
    for (int32_t index = 0; index < ROUNDS_MAX && scene->rounds[index].kind != ROUND_NONE; index++) {
        snprintf(text + strlen(text), sizeof text - strlen(text), ", %s%s", round_words(&scene->rounds[index]),
                 index == 0 && scene->refusal != HOST_OK ? " with every write refused" : "");
    }
    return text;
}

/* Handlers that did not return, after which the checks stop: each is three
 * seconds of waiting, and an application that hangs hangs in every scene. */
#define HANGS_MAX 3

static int report(void);

static void stop_if_hung(const char *fault)
{
    static int hangs;
    if (strstr(fault, "did not return") != NULL && ++hangs >= HANGS_MAX) {
        problem(checking, "%d handlers did not return: the checks stopped here, the rest was not checked", hangs);
        exit(report());
    }
}

/* The least a frame is made of: fewer calls draw a blank screen. */
#define BLANK_FRAME 1

/* Runs the scene; false when the application did not live through it. What
 * every scene is held to, whatever it is for, is looked at here. */
static bool play(const scene_t *scene)
{
    standin_t *host = standin();
    host->stray_draws = host->refused_draws = 0;
    host->stray_draw[0] = host->refused_draw[0] = host->refused_what[0] = '\0';
    host->most_handler_writes = 0;
    host->is_leaving = false;
    host->refused_request[0] = '\0';
    scene_t copy = *scene;
    results[checking].checks++;
    /* Off the screen the scene is told here, and each thing it is given runs in an instance of its own. */
    is_away = scene->is_off_screen;
    has_away_failed = false;
    bool is_whole = is_away ? (scene_run(&copy), !has_away_failed) : standin_live(scene_run, &copy);
    is_away = false;
    if (!is_whole) {
        problem(checking, "%s: %s", told(scene), standin_fault());
        stop_if_hung(standin_fault());
        return false;
    }
    for (int32_t index = 0; index < host->note_count; index++) {
        if (strncmp(host->notes[index], "no answer is kept", 17) == 0) {
            note(NETWORK, "%s: it was told HOST_FAILED_NO_ANSWER", host->notes[index]);
        } else {
            problem(checking, "%s: %s", told(scene), host->notes[index]);
            stop_if_hung(host->notes[index]);
        }
    }
    if (host->stray_draws > 0) {
        problem(DRAWING, "%s: %s draws outside app_on_draw(), %d calls", told(scene), host->stray_draw, host->stray_draws);
    }
    if (host->refused_draws > 0) {
        problem(DRAWING, "%s: %s, %d such calls", told(scene), host->refused_draw, host->refused_draws);
    }
    if (!scene->is_off_screen && standin_handlers.on_draw != NULL && host->marks[MARK_CALLS] <= BLANK_FRAME) {
        problem(DRAWING, "%s: app_on_draw() drew nothing", told(scene));
    }
    if (host->refused_request[0] != '\0') {
        problem(NETWORK, "%s: %s", told(scene), host->refused_request);
    }
    if (host->refused_what[0] != '\0') {
        problem(STORE, "%s: a write is over the limits: %s", told(scene), host->refused_what);
    }
    if (host->marks[MARK_DEPTH] == NEVER_LEAVES) {
        problem(SESSIONS, "%s: %d presses of BACK do not leave it", told(scene), DEPTH_MAX);
    }
    return true;
}

static void expect(capability_t capability, bool is_so, const char *format, ...)
{
    results[capability].checks++;
    if (is_so) {
        return;
    }
    char text[PROBLEM_BYTES];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    problem(capability, "%s", text);
}

static uint64_t mark(int which)
{
    return standin()->marks[which];
}

static bool has_polls_or_requests;

static const scene_t ON_SCREEN = { 0 };

/* What an application is put through before it is unloaded: nothing, a
 * press, a way into a page and down it, a long press, a way in and back. */
static const char *const WAYS[] = { "", "O", "D", "U", "DO", "OO", "ODD", "DDDDDD", "o", "OB" };
#define WAY_COUNT ((int)(sizeof WAYS / sizeof WAYS[0]))

/* ---- Sessions: the application comes back from an unload as it was ---- */

static uint64_t cold_depth;

static void check_cold_start(void)
{
    standin_reset();
    if (play(&ON_SCREEN)) {
        cold_depth = mark(MARK_DEPTH);
    }
    /* A start from what that run stored, with nothing of its session. */
    standin_forget_session();
    play(&ON_SCREEN);
}

static void check_return(const char *way, fire_t fire)
{
    standin_reset();
    scene_t before = { .has_data = has_polls_or_requests, .presses = way, .fire = fire };
    if (!play(&before) || mark(MARK_LEFT)) {
        return; /* it left the screen itself: there is nothing to come back to */
    }
    uint64_t picture = mark(MARK_PICTURE), depth = mark(MARK_DEPTH);
    char what[320];
    snprintf(what, sizeof what, "%s", told(&before));

    /* A scene that waited in the stack: unloaded, loaded again, with its session. */
    if (!play(&ON_SCREEN)) {
        return;
    }
    expect(SESSIONS, mark(MARK_PICTURE) == picture,
           "%s, then unloaded and back from the stack: it shows another picture than before the unload", what);
    expect(SESSIONS, mark(MARK_DEPTH) == depth,
           "%s, then unloaded and back from the stack: %d presses of BACK leave it, %d did before the unload", what,
           (int)mark(MARK_DEPTH), (int)depth);

    /* Opened anew from the menu: no session; its first page, from the store only. */
    standin_forget_session();
    if (!play(&ON_SCREEN)) {
        return;
    }
    uint64_t anew = mark(MARK_PICTURE);
    expect(SESSIONS, mark(MARK_DEPTH) == cold_depth,
           "%s, then opened anew from the menu: %d presses of BACK leave it, %d leave its first page", what,
           (int)mark(MARK_DEPTH), (int)cold_depth);
    standin_forget_session();
    if (play(&ON_SCREEN)) {
        expect(SESSIONS, mark(MARK_PICTURE) == anew,
               "%s, then opened anew twice: the second time it shows another picture", what);
    }

    /* A restart: no session, no alarms, another boot; the store as that run left it. */
    standin_restart();
    scene_t started = { .is_off_screen = true, .intent = HOST_INTENT_STARTED };
    play(&started);
    if (!play(&ON_SCREEN)) {
        return;
    }
    uint64_t restarted = mark(MARK_PICTURE);
    standin_restart();
    play(&started);
    if (play(&ON_SCREEN)) {
        expect(SESSIONS, mark(MARK_PICTURE) == restarted,
               "%s, then two restarts: after the second it shows another picture than after the first", what);
    }
}

static void check_sessions(void)
{
    checking = SESSIONS;
    for (int way = 0; way < WAY_COUNT; way++) {
        check_return(WAYS[way], FIRE_NONE);
        check_return(WAYS[way], FIRE_ON_TIME);
    }
}

/* ---- Store: within the limits, nothing written twice, a refusal survived ---- */

#define UNCHANGED_ANSWERS 50
#define UNCHANGED_VISITS  10
#define REFUSED_TRIES_MAX 8 /* writes one handler may try when every one is refused: more is a loop */

static int32_t flash_writes(void)
{
    return standin()->store_changes + standin()->file_writes;
}

static void check_unchanged(void)
{
    if (has_polls_or_requests) {
        standin_reset();
        scene_t polled = { .has_data = true, .rounds = { { ROUND_WHOLE, 0, UNCHANGED_ANSWERS + 1 } } };
        if (play(&polled)) {
            expect(STORE, mark(MARK_WRITES_AGAIN) == 0,
                   "%d unchanged answers after the first: the flash was written %d times, every write wears it",
                   UNCHANGED_ANSWERS, (int)mark(MARK_WRITES_AGAIN));
        }
        standin_reset();
        scene_t failing = { .has_data = true, .rounds = { { ROUND_STATUS, HOST_FAILED_NO_NETWORK, UNCHANGED_ANSWERS + 1 } } };
        if (play(&failing)) {
            expect(STORE, mark(MARK_WRITES_AGAIN) == 0,
                   "%d requests failed alike after the first: the flash was written %d times", UNCHANGED_ANSWERS,
                   (int)mark(MARK_WRITES_AGAIN));
        }
    }
    standin_reset();
    scene_t used = { .has_data = has_polls_or_requests, .presses = "DO" };
    if (!play(&used) || !play(&ON_SCREEN)) {
        return;
    }
    int32_t writes = flash_writes();
    for (int visit = 0; visit < UNCHANGED_VISITS; visit++) {
        if (!play(&ON_SCREEN)) {
            return;
        }
    }
    expect(STORE, flash_writes() == writes,
           "%d times on the screen and off it with nothing changed: the flash was written %d times",
           UNCHANGED_VISITS, flash_writes() - writes);
}

static void check_longest_settings(void)
{
    int32_t taken = 0, set = 0;
    for (int32_t index = 0; index < standin_subject.setting_count; index++) {
        const char *longest = standin_subject.settings[index].longest;
        if (longest != NULL) {
            taken += 64 + ((int32_t)strlen(longest) + 31) / 32 * 32;
            set++;
        }
    }
    if (set == 0) {
        return;
    }
    if (taken > HOST_KV_TOTAL_MAX) {
        note(STORE, "its settings at their longest take %d bytes of the %d an application has: not checked with them",
             taken, HOST_KV_TOTAL_MAX);
        return;
    }
    standin_reset();
    for (int32_t index = 0; index < standin_subject.setting_count; index++) {
        if (standin_subject.settings[index].longest != NULL) {
            standin_set(standin_subject.settings[index].key, standin_subject.settings[index].longest);
        }
    }
    scene_t full = { .has_settings = true, .has_data = has_polls_or_requests, .presses = "DO" };
    standin()->refused_writes = 0;
    if (play(&full)) {
        expect(STORE, standin()->refused_writes == 0,
               "with its settings at their longest (%d bytes of %d): %d writes were refused", taken,
               HOST_KV_TOTAL_MAX, standin()->refused_writes);
    }
}

static void check_refused(int32_t refusal)
{
    /* What it shows from a store that took its writes. What it shows while they
     * are refused is not held to anything: one application shows what it has
     * in memory, another only what it has stored, and both are right. */
    scene_t used = { .has_data = has_polls_or_requests, .presses = "DO" };
    standin_reset();
    scene_t polled = { .has_data = has_polls_or_requests };
    if (!play(&polled)) {
        return;
    }
    standin_forget_session();
    if (!play(&ON_SCREEN)) {
        return;
    }
    uint64_t kept_anew = mark(MARK_PICTURE);

    standin_reset();
    standin()->refuse_writes = refusal;
    if (!play(&used)) {
        return;
    }
    expect(STORE, standin()->most_handler_writes <= REFUSED_TRIES_MAX,
           "every write answered %d: one handler tried %d writes", refusal, standin()->most_handler_writes);
    /* The store takes writes again and the same answers come: what was not saved is saved now. */
    standin()->refuse_writes = HOST_OK;
    standin_forget_session();
    if (!play(&polled)) {
        return;
    }
    standin_forget_session();
    if (play(&ON_SCREEN)) {
        expect(STORE, mark(MARK_PICTURE) == kept_anew,
               "writes answered %d, then kept again and the same answers: opened anew it shows another picture "
               "than an application whose writes were never refused", refusal);
    }
    /* The same within one instance, which still has in memory what it could not save. */
    standin_reset();
    scene_t retried = { .refusal = refusal, .rounds = { { ROUND_WHOLE, 0, 0 }, { ROUND_WHOLE, 0, 0 } } };
    if (!play(&retried)) {
        return;
    }
    standin_forget_session();
    if (play(&ON_SCREEN)) {
        expect(STORE, mark(MARK_PICTURE) == kept_anew,
               "%s: opened anew it shows another picture than an application whose writes were never refused - "
               "what a refused write did not save is not saved when the same answer comes again", told(&retried));
    }
}

static void check_store(void)
{
    checking = STORE;
    /* Every scene is held to the store's limits (play): here are the ways through the application. */
    for (int way = 0; way < WAY_COUNT; way++) {
        standin_reset();
        scene_t used = { .has_data = has_polls_or_requests, .presses = WAYS[way], .fire = FIRE_ON_TIME };
        play(&used);
    }
    check_unchanged();
    check_longest_settings();
    check_refused(HOST_ERR_NO_SPACE);
    check_refused(HOST_ERR_FAILED);
}

/* ---- Timers and alarms ---- */

#define TIMER_LONGEST_MS (24 * MS_PER_HOUR)

static void check_timer(const char *way)
{
    standin_reset();
    scene_t on_time = { .has_data = has_polls_or_requests, .presses = way, .fire = FIRE_ON_TIME };
    if (!play(&on_time) || mark(MARK_TIMERS_BEFORE) == 0 || mark(MARK_LEFT)) {
        return;
    }
    uint64_t waiting = mark(MARK_TIMERS);
    static const fire_t OTHERWISE[] = { FIRE_LATE, FIRE_TWICE };
    for (size_t index = 0; index < sizeof OTHERWISE / sizeof OTHERWISE[0]; index++) {
        standin_reset();
        scene_t other = on_time;
        other.fire = OTHERWISE[index];
        if (play(&other) && !mark(MARK_LEFT)) {
            expect(TIMERS, mark(MARK_TIMERS) == waiting, "%s: timers %#x wait after it, %#x after a timer on time",
                   told(&other), (unsigned)mark(MARK_TIMERS), (unsigned)waiting);
        }
    }
}

/* Timers stop when the application leaves the screen: back on it, those that waited wait again. */
static void check_timers_return(const char *way, fire_t fire)
{
    standin_reset();
    scene_t before = { .has_data = has_polls_or_requests, .presses = way, .fire = fire };
    if (!play(&before) || mark(MARK_LEFT)) {
        return;
    }
    uint64_t timers = mark(MARK_TIMERS);
    if (play(&ON_SCREEN)) {
        expect(TIMERS, mark(MARK_TIMERS) == timers,
               "%s, then unloaded and back from the stack: timers %#x wait, %#x did before the unload - "
               "timers stop off the screen and are set again in app_on_focus()", told(&before),
               (unsigned)mark(MARK_TIMERS), (unsigned)timers);
    }
}

static void check_clock(const char *way)
{
    static const clock_change_t CHANGES[] = { CLOCK_FORWARD, CLOCK_BACK };
    for (size_t index = 0; index < sizeof CHANGES / sizeof CHANGES[0]; index++) {
        standin_reset();
        scene_t set = { .has_data = has_polls_or_requests, .presses = way, .clock = CHANGES[index] };
        if (!play(&set) || mark(MARK_LEFT)) {
            continue;
        }
        expect(TIMERS, mark(MARK_TIMERS) == mark(MARK_TIMERS_BEFORE), "%s: timers %#x wait after it, %#x before",
               told(&set), (unsigned)mark(MARK_TIMERS), (unsigned)mark(MARK_TIMERS_BEFORE));
        expect(TIMERS, mark(MARK_LONGEST_TIMER) <= (uint64_t)TIMER_LONGEST_MS,
               "%s: a timer is set %lld hours ahead", told(&set), (long long)(mark(MARK_LONGEST_TIMER) / MS_PER_HOUR));
        /* And the timer that waits after it still comes to an application that takes it. */
        scene_t then = set;
        then.fire = FIRE_ON_TIME;
        standin_reset();
        play(&then);
    }
}

static void check_first_time(void)
{
    /* The device starts not knowing the time: 0, and no local time. */
    standin_reset();
    if (!play(&ON_SCREEN)) {
        return;
    }
    uint64_t picture = mark(MARK_PICTURE);
    standin_reset();
    standin_clock(0);
    play(&ON_SCREEN);
    standin_reset();
    standin_clock(0);
    scene_t learned = { .clock = CLOCK_BECOMES_KNOWN };
    if (play(&learned) && (standin_subject.intents & (1u << HOST_INTENT_TIME_CHANGED)) != 0) {
        expect(TIMERS, mark(MARK_PICTURE) == picture,
               "started before the clock was set, then told the time (time_changed): it shows another picture "
               "than when started with the clock set");
    }
}

static void check_alarms(const char *way)
{
    standin_reset();
    scene_t before = { .has_data = has_polls_or_requests, .presses = way };
    if (!play(&before) || mark(MARK_ALARMS) == 0) {
        return;
    }
    uint64_t alarms = mark(MARK_ALARMS);
    /* Off the screen the application is loaded for the alarm and unloaded after it. */
    static const int64_t LATENESS[] = { 0, 6 * MS_PER_HOUR };
    for (size_t index = 0; index < sizeof LATENESS / sizeof LATENESS[0]; index++) {
        standin_reset();
        if (!play(&before)) {
            return;
        }
        scene_t away = { .is_off_screen = true, .has_alarms = true, .alarms_late_ms = LATENESS[index] };
        play(&away);
        play(&ON_SCREEN);
    }
    standin_reset();
    scene_t on_screen = before;
    on_screen.has_alarms = true;
    play(&on_screen);
    /* Another package: the alarms of the old one are gone, and app_on_updated() sets them again. */
    standin_reset();
    if (!play(&before)) {
        return;
    }
    scene_t updated = { .is_off_screen = true, .is_updated = true };
    if (play(&updated)) {
        expect(TIMERS, mark(MARK_ALARMS) == alarms,
               "%s, then updated: alarms %#x wait, %#x did before - a new version does not inherit them, "
               "app_on_updated() sets them again", told(&before), (unsigned)mark(MARK_ALARMS), (unsigned)alarms);
    }
}

static void check_timers(void)
{
    checking = TIMERS;
    check_first_time();
    for (int way = 0; way < WAY_COUNT; way++) {
        check_timers_return(WAYS[way], FIRE_NONE);
        check_timers_return(WAYS[way], FIRE_ON_TIME);
        check_timer(WAYS[way]);
        check_clock(WAYS[way]);
        check_alarms(WAYS[way]);
    }
}

/* ---- Network: an answer however it arrives ---- */

static void check_network(void)
{
    checking = NETWORK;
    standin_reset();
    scene_t whole = { .rounds = { { ROUND_WHOLE, 0, 0 } } };
    if (!play(&whole)) {
        return;
    }
    uint64_t answered = mark(MARK_ROUND_PICTURE);
    if (mark(MARK_UNANSWERED) > 0) {
        note(NETWORK, "not every request has an answer kept beside the application (test/check.json): "
                      "those are checked with failures only");
    }
    /* What an answer changed is drawn when the application asks for it: back from the stack,
     * drawn anew, it shows what it showed. */
    if (play(&ON_SCREEN)) {
        expect(NETWORK, mark(MARK_PICTURE) == answered,
               "%s: drawn anew it shows another picture than it showed - what the answer changed was not asked "
               "for with screen_redraw()", told(&whole));
    }

    static const int32_t PIECES[] = { 1, 7, 1000 };
    for (size_t index = 0; index < sizeof PIECES / sizeof PIECES[0]; index++) {
        standin_reset();
        scene_t pieces = { .rounds = { { ROUND_PIECES, PIECES[index], 0 } } };
        if (play(&pieces)) {
            expect(NETWORK, mark(MARK_ROUND_PICTURE) == answered,
                   "%s: it shows another picture than after the same answers whole", told(&pieces));
        }
    }

    /* An answer stops without HOST_DATA_LAST or HOST_DATA_FAILED only for an instance that is
     * unloaded then (sdk/host/net.h): it is played off the screen, below. */

    /* HOST_DATA_FAILED after pieces invalidates them all: it is a request that failed, no more. */
    standin_reset();
    scene_t failed = { .rounds = { { ROUND_STATUS, HOST_FAILED_NO_ANSWER, 0 } } };
    uint64_t after_failure = play(&failed) ? mark(MARK_ROUND_PICTURE) : 0;
    standin_reset();
    scene_t failed_late = { .rounds = { { ROUND_FAILED_LATE, 0, 0 }, { ROUND_WHOLE, 0, 0 } } };
    if (play(&failed_late)) {
        expect(NETWORK, mark(MARK_ROUND_PICTURE) == after_failure,
               "%s: after the failure it shows another picture than after a request that failed with no pieces",
               told(&failed_late));
        expect(NETWORK, mark(MARK_ROUND_PICTURE + 1) == answered,
               "%s: after the whole answer it shows another picture than after a whole answer alone", told(&failed_late));
    }
    standin_reset();
    scene_t failed_with_data = { .rounds = { { ROUND_WHOLE, 0, 0 }, { ROUND_STATUS, HOST_FAILED_NO_ANSWER, 0 } } };
    uint64_t stale = play(&failed_with_data) ? mark(MARK_ROUND_PICTURE + 1) : 0;
    standin_reset();
    scene_t failed_late_with_data = { .rounds = { { ROUND_WHOLE, 0, 0 }, { ROUND_FAILED_LATE, 0, 0 } } };
    if (play(&failed_late_with_data)) {
        expect(NETWORK, mark(MARK_ROUND_PICTURE + 1) == stale,
               "%s: after the failure it shows another picture than after a request that failed with no pieces",
               told(&failed_late_with_data));
    }

    /* Every way a request fails, and a server that is unwell: it goes on, and the next answer is shown. */
    for (int32_t status = HOST_FAILED_NO_NETWORK; status >= HOST_FAILED_NO_MEMORY; status--) {
        standin_reset();
        scene_t refused = { .rounds = { { ROUND_STATUS, status, 0 }, { ROUND_WHOLE, 0, 0 } } };
        if (play(&refused)) {
            expect(NETWORK, mark(MARK_ROUND_PICTURE + 1) == answered,
                   "%s: after the whole answer it shows another picture than after a whole answer alone", told(&refused));
        }
    }
    static const int32_t UNWELL[] = { 503, 404, 304 };
    for (size_t index = 0; index < sizeof UNWELL / sizeof UNWELL[0]; index++) {
        standin_reset();
        scene_t unwell = { .rounds = { { ROUND_HTTP, UNWELL[index], 0 }, { ROUND_WHOLE, 0, 0 } } };
        if (play(&unwell)) {
            expect(NETWORK, mark(MARK_ROUND_PICTURE + 1) == answered,
                   "%s: after the whole answer it shows another picture than after a whole answer alone", told(&unwell));
        }
    }

    standin_reset();
    scene_t again = { .rounds = { { ROUND_WHOLE, 0, 0 }, { ROUND_WHOLE, 0, 0 } } };
    if (play(&again)) {
        expect(NETWORK, mark(MARK_ROUND_PICTURE + 1) == answered,
               "%s: after the second, unchanged, it shows another picture than after the first", told(&again));
    }

    /* Off the screen it is loaded for the delivery and unloaded after it:
     * back on the screen it shows what it would had it been there. It was on
     * the screen once before either: what it asked for then is what arrives. */
    standin_reset();
    if (!play(&ON_SCREEN) || !play(&whole) || !play(&ON_SCREEN)) {
        return;
    }
    uint64_t seen_it_arrive = mark(MARK_PICTURE);
    static const int32_t AWAY_PIECES[] = { HOST_DATA_CHUNK_MAX, 1 };
    for (size_t index = 0; index < sizeof AWAY_PIECES / sizeof AWAY_PIECES[0]; index++) {
        standin_reset();
        scene_t away = { .is_off_screen = true, .rounds = { { ROUND_PIECES, AWAY_PIECES[index], 0 } } };
        if (play(&ON_SCREEN) && play(&away) && play(&ON_SCREEN)) {
            expect(NETWORK, mark(MARK_PICTURE) == seen_it_arrive,
                   "%s, then on the screen: it shows another picture than an application that was on the screen "
                   "when the answers came, was unloaded and came back", told(&away));
        }
    }
    /* An answer that did not end whole leaves what a request that failed leaves; one that
     * stopped, cut off with the instance, leaves nothing. */
    standin_reset();
    uint64_t nothing_came = play(&ON_SCREEN) && play(&ON_SCREEN) ? mark(MARK_PICTURE) : 0;
    standin_reset();
    scene_t failed_away = { .is_off_screen = true, .rounds = { { ROUND_STATUS, HOST_FAILED_NO_ANSWER, 0 } } };
    uint64_t failed_seen = play(&ON_SCREEN) && play(&failed_away) && play(&ON_SCREEN) ? mark(MARK_PICTURE) : 0;
    static const round_kind_t AWAY_ENDINGS[] = { ROUND_CUT, ROUND_FAILED_LATE };
    for (size_t index = 0; index < sizeof AWAY_ENDINGS / sizeof AWAY_ENDINGS[0]; index++) {
        standin_reset();
        scene_t away = { .is_off_screen = true, .rounds = { { AWAY_ENDINGS[index], 0, 0 } } };
        uint64_t expected = AWAY_ENDINGS[index] == ROUND_CUT ? nothing_came : failed_seen;
        if (expected != 0 && play(&ON_SCREEN) && play(&away) && play(&ON_SCREEN)) {
            expect(NETWORK, mark(MARK_PICTURE) == expected,
                   "%s, then on the screen: it shows another picture than %s - only HOST_DATA_LAST makes an "
                   "answer whole", told(&away),
                   AWAY_ENDINGS[index] == ROUND_CUT ? "before the answer began" : "after a request that failed");
        }
        standin_reset();
        scene_t then = { .is_off_screen = true, .rounds = { { ROUND_WHOLE, 0, 0 } } };
        if (play(&ON_SCREEN) && play(&away) && play(&then) && play(&ON_SCREEN)) {
            expect(NETWORK, mark(MARK_PICTURE) == seen_it_arrive,
                   "%s, then a whole answer off the screen, then on the screen: it shows another picture than "
                   "after the whole answer alone", told(&away));
        }
    }
}

/* ---- Drawing: a frame from any state, with calls the host takes ---- */

static void check_drawing(void)
{
    checking = DRAWING;
    /* Every scene above is held to it too; these are for when it is asked for alone. */
    for (int way = 0; way < WAY_COUNT; way++) {
        standin_reset();
        scene_t drawn = { .has_data = has_polls_or_requests, .presses = WAYS[way], .fire = FIRE_ON_TIME };
        play(&drawn);
        standin_reset();
        standin_clock(0);
        scene_t no_time = { .presses = WAYS[way] };
        play(&no_time);
    }
}

int main(int count, char **arguments)
{
    static void (*const CHECKS[CAPABILITY_COUNT])(void) = { check_sessions, check_store, check_timers, check_network,
                                                            check_drawing };
    standin_subject_handlers();
    for (int argument = 1; argument < count; argument++) {
        bool is_known = false;
        for (int capability = 0; capability < CAPABILITY_COUNT; capability++) {
            if (strcmp(arguments[argument], CAPABILITY_NAMES[capability]) == 0) {
                results[capability].is_asked = is_known = true;
            }
        }
        if (!is_known) {
            fprintf(stderr, "checks: no capability \"%s\"\n", arguments[argument]);
            return 2;
        }
    }
    standin_reset();
    time_known = standin()->now;
    has_polls_or_requests = standin_subject.poll_count > 0 || standin_subject.host_count > 0;
    for (int capability = CAPABILITY_COUNT - 1; capability >= 0; capability--) {
        if (results[capability].is_asked) {
            checking = (capability_t)capability;
        }
    }
    /* The first page's depth is what every "opened anew" is held to. */
    check_cold_start();
    for (int capability = 0; capability < CAPABILITY_COUNT; capability++) {
        if (results[capability].is_asked) {
            CHECKS[capability]();
        }
    }
    return report();
}

/* The lines the tools read; the program's exit code. */
static int report(void)
{
    bool has_failed = false;
    for (int capability = 0; capability < CAPABILITY_COUNT; capability++) {
        if (!results[capability].is_asked) {
            continue;
        }
        bool is_whole = results[capability].problem_count == 0;
        has_failed = has_failed || !is_whole;
        printf("check %s %s %d\n", CAPABILITY_NAMES[capability], is_whole ? "ok" : "fail", results[capability].checks);
        for (int index = 0; index < results[capability].problem_count; index++) {
            printf("problem %s %s\n", CAPABILITY_NAMES[capability], results[capability].problems[index]);
        }
        if (results[capability].problems_unsaid > 0) {
            printf("problem %s and %d more\n", CAPABILITY_NAMES[capability], results[capability].problems_unsaid);
        }
        for (int index = 0; index < results[capability].note_count; index++) {
            printf("note %s %s\n", CAPABILITY_NAMES[capability], results[capability].notes[index]);
        }
    }
    return has_failed ? 1 : 0;
}
