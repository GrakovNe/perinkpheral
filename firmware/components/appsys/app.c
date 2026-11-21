/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The runtime of one application: its module checked and loaded, the
 * calls of the contract linked into it, its handlers found and called
 * on a budget, its strings put in place. The calls themselves are
 * app_api_<area>.c.
 */
#include <assert.h>
#include <stdio.h>

#include "app_internal.h"
#include "app_abi.h"

/* The interpreter's own stack of values, apart from the application's
 * memory. */
#define INTERPRETER_STACK_BYTES 4096

#define HOST_MODULE "host"

/* How long one handler may run. Drawing a full screen takes an
 * interpreted application tens of milliseconds; one that is still at it
 * after this long is stuck. */
#define HANDLER_BUDGET_MS 3000

/* The clock is looked at once in this many yields of the interpreter,
 * which yields on every call and every turn of a loop; and after every
 * call of the contract (call_host below), which may take milliseconds. */
#define YIELDS_PER_CLOCK_CHECK 1024

static const char STOPPED_TOO_LONG[] = "it ran too long";

static const struct {
    const char *name;
    uint32_t    arguments;
    bool        has_result;
} EXPORTS[EXPORT_COUNT] = {
#define APP_EXPORT_ROW(name, arguments, has_result) [EXPORT_##name] = { #name, arguments, has_result },
#include "host_exports.inc"
#undef APP_EXPORT_ROW
};

/* An export added to the contract stops the build here until the host
 * calls it: then the number is moved. */
_Static_assert(EXPORT_COUNT == 17, "every export of host_api.h is called by the host");

/* The application whose handler is running, for m3_Yield() below: the
 * interpreter calls it without saying for whom. There is one application
 * and one host task, so one is enough. */
static struct {
    const app_services_t *services;
    int64_t               deadline_ms;
    uint32_t              yields;
} s_running;

/* The interpreter's hook: called on every call and every turn of a loop
 * of the application; a result that is not m3Err_none traps it. */
M3Result m3_Yield(void)
{
    if (s_running.services == NULL || ++s_running.yields % YIELDS_PER_CLOCK_CHECK != 0) {
        return m3Err_none;
    }
    return s_running.services->uptime_ms() > s_running.deadline_ms ? STOPPED_TOO_LONG
                                                                   : m3Err_none;
}

static void start_budget(const app_services_t *services)
{
    s_running.services = services;
    s_running.deadline_ms = services->uptime_ms() + HANDLER_BUDGET_MS;
    s_running.yields = 0;
}

/* A handler this long is felt: the device does nothing else meanwhile. */
#define HANDLER_SLOW_MS 400

static int64_t stop_budget(void)
{
    int64_t took_ms = s_running.services != NULL
                          ? s_running.services->uptime_ms() - (s_running.deadline_ms - HANDLER_BUDGET_MS)
                          : 0;
    s_running.services = NULL;
    return took_ms;
}

/* wasm3 signatures: the result, then the arguments. v nothing, i i32,
 * I i64, * a pointer into the application's memory. */
static const struct {
    const char *name;
    const char *signature;
    M3RawCall   function;
} HOST_API[] = {
#define HOST_IMPORT_ROW(name, signature) { #name, signature, app_api_##name },
#include "host_imports.inc" /* made from sdk/host_api.h by tools/perinkpheral imports */
};
#define HOST_API_COUNT (sizeof HOST_API / sizeof HOST_API[0])

/* The system API: for an application whose manifest asked for it. */
static const struct {
    const char *name;
    const char *signature;
    M3RawCall   function;
} SYSTEM_API[] = {
#include "system_imports.inc" /* made from sdk/host_api.h by tools/perinkpheral imports */
#undef HOST_IMPORT_ROW
};
#define SYSTEM_API_COUNT (sizeof SYSTEM_API / sizeof SYSTEM_API[0])

/* Writing files: for an application whose manifest said it writes. */
#define FILE_WRITE_CALL "file_write"

static bool is_offered(const uint8_t *module_name, uint32_t module_length,
                       const uint8_t *field_name, uint32_t field_length, unsigned grants)
{
    if (module_length != strlen(HOST_MODULE) ||
        memcmp(module_name, HOST_MODULE, module_length) != 0) {
        return false;
    }
    if ((grants & APP_GRANT_FILES) == 0 && field_length == strlen(FILE_WRITE_CALL) &&
        memcmp(field_name, FILE_WRITE_CALL, field_length) == 0) {
        return false;
    }
    for (size_t i = 0; i < HOST_API_COUNT; i++) {
        if (field_length == strlen(HOST_API[i].name) &&
            memcmp(field_name, HOST_API[i].name, field_length) == 0) {
            return true;
        }
    }
    for (size_t i = 0; (grants & APP_GRANT_SYSTEM) != 0 && i < SYSTEM_API_COUNT; i++) {
        if (field_length == strlen(SYSTEM_API[i].name) &&
            memcmp(field_name, SYSTEM_API[i].name, field_length) == 0) {
            return true;
        }
    }
    return false;
}

/* A reader over the module's bytes that cannot run off their end: past
 * it everything reads as zero and `failed` is set. */
typedef struct {
    const uint8_t *next;
    const uint8_t *end;
    bool           failed;
} wasm_reader_t;

static uint8_t read_byte(wasm_reader_t *reader)
{
    if (reader->next >= reader->end) {
        reader->failed = true;
        return 0;
    }
    return *reader->next++;
}

#define LEB_PAYLOAD_BITS 7
#define LEB_MORE_FLAG    0x80
#define LEB_U32_MAX_BYTES 5

static uint32_t read_leb_u32(wasm_reader_t *reader)
{
    uint32_t value = 0;
    for (int index = 0; index < LEB_U32_MAX_BYTES; index++) {
        uint8_t byte = read_byte(reader);
        value |= (uint32_t)(byte & ~LEB_MORE_FLAG) << (index * LEB_PAYLOAD_BITS);
        if ((byte & LEB_MORE_FLAG) == 0) {
            return value;
        }
    }
    reader->failed = true;
    return 0;
}

/* A length-prefixed name; NULL when it does not fit in what is left. */
static const uint8_t *read_name(wasm_reader_t *reader, uint32_t *length)
{
    *length = read_leb_u32(reader);
    if (reader->failed || *length > (size_t)(reader->end - reader->next)) {
        reader->failed = true;
        return NULL;
    }
    const uint8_t *name = reader->next;
    reader->next += *length;
    return name;
}

#define WASM_HEADER_BYTES    8
#define WASM_SECTION_IMPORT  2
#define WASM_IMPORT_FUNCTION 0

/* Whether everything the module imports is a function of the host API.
 * The interpreter would load a module that imports something else and
 * fail only when it is called. APP_OK, or why not: APP_ERR_OTHER_API for a
 * call of the host's own that this host does not have - a module made
 * for another version of it - and APP_ERR_BAD_MODULE for anything else. */
static app_load_result_t check_imports(const uint8_t *wasm, size_t wasm_size, unsigned grants)
{
    if (wasm_size < WASM_HEADER_BYTES) {
        return APP_ERR_BAD_MODULE;
    }
    wasm_reader_t reader = { wasm + WASM_HEADER_BYTES, wasm + wasm_size, false };
    while (reader.next < reader.end) {
        uint8_t section = read_byte(&reader);
        uint32_t section_size = read_leb_u32(&reader);
        if (reader.failed || section_size > (size_t)(reader.end - reader.next)) {
            return APP_ERR_BAD_MODULE;
        }
        if (section != WASM_SECTION_IMPORT) {
            reader.next += section_size;
            continue;
        }
        uint32_t import_count = read_leb_u32(&reader);
        for (uint32_t i = 0; i < import_count && !reader.failed; i++) {
            uint32_t module_length, field_length;
            const uint8_t *module_name = read_name(&reader, &module_length);
            const uint8_t *field_name = read_name(&reader, &field_length);
            uint8_t kind = read_byte(&reader);
            read_leb_u32(&reader); /* the function's type */
            if (reader.failed || kind != WASM_IMPORT_FUNCTION) {
                return APP_ERR_BAD_MODULE;
            }
            if (is_offered(module_name, module_length, field_name, field_length, grants)) {
                continue;
            }
            /* A call it did not ask for - the system API, writing files -
             * is not a matter of versions; neither is another module's
             * function. */
            bool is_host_module = module_length == sizeof HOST_MODULE - 1 &&
                                memcmp(module_name, HOST_MODULE, module_length) == 0;
            bool is_granted = is_offered(module_name, module_length, field_name, field_length,
                                         APP_GRANT_SYSTEM | APP_GRANT_FILES);
            return is_host_module && !is_granted ? APP_ERR_OTHER_API : APP_ERR_BAD_MODULE;
        }
        return reader.failed ? APP_ERR_BAD_MODULE : APP_OK;
    }
    return APP_OK;
}

/* Every import is linked through here, the call itself in its userdata.
 * A call of the contract is one yield however long it takes, so a
 * thousand full-screen fills would pass before m3_Yield looked at the
 * clock: it is looked at after each, and the deadline is passed by one
 * call at most. One reading of the clock is about a microsecond, less
 * than the call itself. */
static const void *call_host(IM3Runtime runtime, IM3ImportContext context, uint64_t *sp, void *memory)
{
    M3RawCall function = *(const M3RawCall *)context->userdata;
    const void *trap = function(runtime, context, sp, memory);
    if (trap == m3Err_none && s_running.services != NULL &&
        s_running.services->uptime_ms() > s_running.deadline_ms) {
        return STOPPED_TOO_LONG;
    }
    return trap;
}

/* False when the module declares one of these with another signature. A
 * function the module does not import is simply not linked. */
static bool link_host_api(IM3Module module)
{
    for (size_t i = 0; i < HOST_API_COUNT; i++) {
        M3Result result = m3_LinkRawFunctionEx(module, HOST_MODULE, HOST_API[i].name,
                                               HOST_API[i].signature, call_host, &HOST_API[i].function);
        if (result != m3Err_none && result != m3Err_functionLookupFailed) {
            return false;
        }
    }
    /* Who may import these was settled before the module was parsed. */
    for (size_t i = 0; i < SYSTEM_API_COUNT; i++) {
        M3Result result = m3_LinkRawFunctionEx(module, HOST_MODULE, SYSTEM_API[i].name,
                                               SYSTEM_API[i].signature, call_host,
                                               &SYSTEM_API[i].function);
        if (result != m3Err_none && result != m3Err_functionLookupFailed) {
            return false;
        }
    }
    return true;
}

/* ---- lifecycle ---- */

/* An export the host calls, when the module has it with the signature the
 * contract gives it: so many i32 arguments and, with `has_result`, one i32
 * result. The interpreter passes arguments and writes results by the
 * module's own declaration, so an export declared otherwise would read
 * arguments that are not there or write a result past the host's
 * variable: such a module is not run at all (`has_wrong_export`). */
static IM3Function find_export(app_t *app, const char *name, uint32_t arguments, bool has_result)
{
    IM3Function function = NULL;
    if (m3_FindFunction(&function, app->runtime, name) != m3Err_none) {
        return NULL;
    }
    bool is_as_agreed = m3_GetArgCount(function) == arguments &&
                        m3_GetRetCount(function) == (has_result ? 1u : 0u) &&
                        (!has_result || m3_GetRetType(function, 0) == c_m3Type_i32);
    for (uint32_t i = 0; is_as_agreed && i < arguments; i++) {
        is_as_agreed = m3_GetArgType(function, i) == c_m3Type_i32;
    }
    if (!is_as_agreed) {
        app->has_wrong_export = true;
        return NULL;
    }
    return function;
}

/* The one place a handler's result is looked at: a trap stops the
 * application. */
static void note_result(app_t *app, M3Result result)
{
    if (result != m3Err_none) {
        app->stop_reason = result;
        app->redraw_requested = true;
    }
}

/* A slow handler says itself in the log, once an instance: the developer
 * sees it beside the application's own lines. */
static void note_duration(app_t *app, const char *handler, int64_t took_ms)
{
    if (took_ms < HANDLER_SLOW_MS || app->slow_logged || app->services->log_write == NULL) {
        return;
    }
    app->slow_logged = true;
    char told[HOST_LOG_TEXT_MAX + 1];
    int  length = snprintf(told, sizeof told, "slow handler: %s took %d ms; the device waits for it",
                           handler, (int)took_ms);
    app->services->log_write(HOST_LOG_WARN, told, length < (int)sizeof told ? length : (int)sizeof told - 1);
}

/* Calls the export `name` - app_on_draw - if the application has it; the
 * log names it without its "app_": on_draw. */
#define CALL_HANDLER(app, name, ...)                                                   \
    do {                                                                               \
        IM3Function handler_ = HANDLER(app, name);                                     \
        if ((app)->stop_reason == NULL && handler_ != NULL) {                          \
            start_budget((app)->services);                                             \
            note_result((app), m3_CallV(handler_, ##__VA_ARGS__));                     \
            note_duration((app), &#name[sizeof "app_" - 1], stop_budget());            \
        }                                                                              \
    } while (0)

/* Builds the runtime and loads the module into it; the application is
 * left for app_unload() whatever happens. */
static app_load_result_t load_module(app_t *app, const uint8_t *wasm, size_t wasm_size)
{
    app->environment = m3_NewEnvironment();
    app->runtime = app->environment != NULL
                       ? m3_NewRuntime(app->environment, INTERPRETER_STACK_BYTES, app)
                       : NULL;
    if (app->runtime == NULL) {
        return APP_ERR_NO_MEMORY;
    }

    IM3Module module = NULL;
    if (m3_ParseModule(app->environment, &module, wasm, (uint32_t)wasm_size) != m3Err_none) {
        return APP_ERR_BAD_MODULE;
    }
    M3Result load_result = m3_LoadModule(app->runtime, module);
    if (load_result != m3Err_none) {
        /* Until it is loaded the module is ours to free. */
        m3_FreeModule(module);
        return load_result == m3Err_mallocFailed ? APP_ERR_NO_MEMORY : APP_ERR_BAD_MODULE;
    }
    return link_host_api(module) ? APP_OK : APP_ERR_OTHER_API;
}

/* Asks the application where it takes data. Without a sound answer it
 * simply gets none. */
static void find_data_buffer(app_t *app)
{
    IM3Function data_buffer = HANDLER(app, app_data_buffer);
    uint32_t offset = 0;
    if (data_buffer == NULL) {
        return;
    }
    /* It is the application's code like any handler: on the same budget. */
    start_budget(app->services);
    M3Result result = m3_CallV(data_buffer);
    stop_budget();
    if (result != m3Err_none) {
        note_result(app, result);
        return;
    }
    if (m3_GetResultsV(data_buffer, &offset) != m3Err_none) {
        return;
    }
    uint32_t memory_size = m3_GetMemorySize(app->runtime);
    app->has_data_buffer = offset <= memory_size && HOST_DATA_CHUNK_MAX <= memory_size - offset;
    app->data_buffer_offset = offset;
}

/* The table of the device's language, in place of the English one the
 * module came with - when the application has strings, has a table for
 * that language among its files, and the table is sound and fits. Else
 * the application speaks English. */
#define STRINGS_HEADER_BYTES 4
#define STRINGS_MAX_BYTES    MANIFEST_STRINGS_MAX_BYTES
/* The table: its capacity and the number of strings, two bytes each, low
 * first; then an offset of two bytes for every string; then the strings. */
enum { STRINGS_AT_CAPACITY = 0, STRINGS_AT_COUNT = 2, STRINGS_OFFSET_BYTES = 2 };

static uint32_t read_u16(const uint8_t *bytes)
{
    return (uint32_t)(bytes[0] | bytes[1] << 8);
}

static bool is_strings_table(const uint8_t *table, size_t bytes, uint32_t count)
{
    if (bytes < STRINGS_HEADER_BYTES + STRINGS_OFFSET_BYTES * (size_t)count || table[bytes - 1] != '\0' ||
        read_u16(table + STRINGS_AT_COUNT) != count) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        size_t offset = read_u16(table + STRINGS_HEADER_BYTES + STRINGS_OFFSET_BYTES * i);
        if (offset < STRINGS_HEADER_BYTES + STRINGS_OFFSET_BYTES * (size_t)count || offset >= bytes) {
            return false;
        }
    }
    return true;
}

static void load_strings(app_t *app)
{
    IM3Function strings_buffer = HANDLER(app, app_strings_buffer);
    const char *language = app->services->language != NULL ? app->services->language() : "en";
    uint32_t    offset = 0;
    /* After a trap in app_data_buffer or the start function, a call would
     * run the start function again on a budget of its own. */
    if (app->stop_reason != NULL || strings_buffer == NULL || strcmp(language, "en") == 0 ||
        app->services->file_size == NULL || app->services->file_read == NULL) {
        return;
    }
    start_budget(app->services);
    M3Result result = m3_CallV(strings_buffer);
    stop_budget();
    if (result != m3Err_none) {
        note_result(app, result);
        return;
    }
    uint32_t memory_size = m3_GetMemorySize(app->runtime);
    uint8_t *memory = m3_GetMemory(app->runtime, NULL, 0);
    if (m3_GetResultsV(strings_buffer, &offset) != m3Err_none || memory == NULL ||
        offset > memory_size || memory_size - offset < STRINGS_HEADER_BYTES) {
        return;
    }
    uint8_t *english = memory + offset;
    uint32_t capacity = read_u16(english + STRINGS_AT_CAPACITY);
    uint32_t count = read_u16(english + STRINGS_AT_COUNT);
    if (capacity > memory_size - offset) {
        return;
    }
    char file[HOST_FILE_NAME_MAX + 1];
    snprintf(file, sizeof file, "strings.%.7s", language);
    int32_t bytes = app->services->file_size(app->name, file);
    if (bytes < STRINGS_HEADER_BYTES || (uint32_t)bytes > capacity || bytes > STRINGS_MAX_BYTES) {
        return; /* no such language, or a table that would not fit */
    }
    uint8_t *table = malloc((size_t)bytes);
    if (table == NULL) {
        return;
    }
    if (app->services->file_read(app->name, file, 0, table, bytes) == bytes &&
        is_strings_table(table, (size_t)bytes, count)) {
        /* The buffer's own size stays what the module said it is. */
        memcpy(english + 2, table + 2, (size_t)bytes - 2);
    }
    free(table);
}

/* The one instance there is room for. The area under it is reset when it is
 * unloaded, so a second one started beside it would be handed the first's
 * memory: it is refused instead, whoever forgot to unload the first. */
static app_t *s_loaded;

const char *app_loaded_name(void)
{
    return s_loaded != NULL ? s_loaded->name : NULL;
}

app_load_result_t app_load(const char *name, uint32_t abi, const uint8_t *wasm, size_t wasm_size,
                           unsigned grants, const app_services_t *services, app_t **loaded)
{
    *loaded = NULL;
    if (!app_abi_supported(abi)) {
        return APP_ERR_OTHER_API;
    }
    if (s_loaded != NULL) {
        /* The host's mistake, not the application's: said so, since the
         * screen can only say there is no room. */
        if (services->log_write != NULL) {
            char told[HOST_LOG_TEXT_MAX + 1];
            int  length = snprintf(told, sizeof told, "%s is not loaded: there is room for one", name);
            services->log_write(HOST_LOG_ERROR, told,
                          length < (int)sizeof told ? length : (int)sizeof told - 1);
        }
        return APP_ERR_NO_MEMORY;
    }
    app_load_result_t imports = check_imports(wasm, wasm_size, grants);
    if (imports != APP_OK) {
        return imports;
    }
    app_t *app = calloc(1, sizeof *app);
    if (app == NULL) {
        return APP_ERR_NO_MEMORY;
    }
    app->services = services;
    app->name = name;
    s_loaded = app;
    app_load_result_t result = load_module(app, wasm, wasm_size);
    if (result != APP_OK) {
        app_unload(app);
        return result;
    }

    for (int i = 0; i < EXPORT_COUNT; i++) {
        app->exports[i] = find_export(app, EXPORTS[i].name, EXPORTS[i].arguments, EXPORTS[i].has_result);
    }
    if (!app->has_wrong_export) {
        find_data_buffer(app);
        load_strings(app);
    }
    if (app->has_wrong_export) {
        app_unload(app);
        return APP_ERR_BAD_MODULE;
    }
    CALL_HANDLER(app, app_on_load);
    app->exit_requested = false;

    *loaded = app;
    return APP_OK;
}

void app_unload(app_t *app)
{
    if (app == NULL) {
        return;
    }
    if (app->runtime != NULL) {
        m3_FreeRuntime(app->runtime);
    }
    if (app->environment != NULL) {
        m3_FreeEnvironment(app->environment);
    }
    /* Every instance is the one app_load() noted. */
    assert(app == s_loaded);
    free(app);
    s_loaded = NULL;
#ifdef d_m3ApplicationArea
    /* The next application starts in an area nobody has been in. */
    m3_area_reset();
#endif
}

void app_attach_session(app_t *app, app_session_t *session, bool is_writable)
{
    if (app != NULL) {
        app->session = session;
        app->session_is_writable = is_writable;
        if (session != NULL) {
            if (app->screen_inversion_set && is_writable && !session->is_closed) {
                session->screen_inverted = app->screen_inverted;
            } else {
                app->screen_inverted = session->screen_inverted;
            }
        }
    }
}

void app_focus(app_t *app)
{
    CALL_HANDLER(app, app_on_focus);
    app->exit_requested = false; /* only a button or a timer may ask to leave */
}

void app_blur(app_t *app)
{
    CALL_HANDLER(app, app_on_blur);
}

void app_terminate(app_t *app)
{
    CALL_HANDLER(app, app_on_terminate);
}

bool app_handles_back(const app_t *app)
{
    return HANDLER(app, app_on_back) != NULL;
}

void app_back(app_t *app)
{
    CALL_HANDLER(app, app_on_back);
}

void app_button(app_t *app, int button, int press)
{
    CALL_HANDLER(app, app_on_button, (int32_t)BTN_EVENT(button, press));
}

void app_timer(app_t *app, int id)
{
    CALL_HANDLER(app, app_on_timer, (int32_t)id);
}

void app_draw(app_t *app, gfx_t *surface)
{
    app->surface = surface;
    surface->shade = GFX_SHADE_SOLID;
    CALL_HANDLER(app, app_on_draw);
    surface->shade = GFX_SHADE_SOLID;
    app->surface = NULL;
    app->exit_requested = false;
}

void app_answer_begins(app_t *app, const host_answer_info_t *response,
                       const struct answer_headers *headers, int http_status, int item)
{
    app->is_in_answer = true;
    app->response = response;
    app->headers = headers;
    app->response_status = http_status;
    app->answer_item = item;
}

void app_answer_ends(app_t *app)
{
    app->is_in_answer = false;
    app->response = NULL;
    app->headers = NULL;
}

void app_data(app_t *app, int request, const uint8_t *data, size_t length, int flags,
              int http_status)
{
    if (HANDLER(app, app_on_data) == NULL || !app->has_data_buffer || length > HOST_DATA_CHUNK_MAX) {
        return;
    }
    /* Looked up again every time: the interpreter may move the memory. */
    uint8_t *memory = m3_GetMemory(app->runtime, NULL, 0);
    if (memory == NULL || app->stop_reason != NULL) {
        return;
    }
    if (length > 0) { /* a request that failed comes with no data at all */
        memcpy(memory + app->data_buffer_offset, data, length);
    }
    CALL_HANDLER(app, app_on_data, (int32_t)request, (int32_t)app->data_buffer_offset,
                 (int32_t)length, (int32_t)flags, (int32_t)http_status);
}

void app_values(app_t *app, int request, int http_status, json_pick_t *picked)
{
    app->picked = picked;
    CALL_HANDLER(app, app_on_picked, (int32_t)request, (int32_t)http_status);
    app->picked = NULL;
}

void app_alarm(app_t *app, int id)
{
    CALL_HANDLER(app, app_on_alarm, (int32_t)id);
}

void app_settings_changed(app_t *app)
{
    CALL_HANDLER(app, app_on_settings);
}

void app_intent(app_t *app, int intent)
{
    CALL_HANDLER(app, app_on_intent, (int32_t)intent);
}

void app_installed(app_t *app)
{
    CALL_HANDLER(app, app_on_installed);
}

void app_updated(app_t *app)
{
    CALL_HANDLER(app, app_on_updated);
}

bool app_screen_inverted(const app_t *app)
{
    return app->screen_inverted;
}

const char *app_header_text(const app_t *app, int side)
{
    return app->header[side];
}

bool app_take_focus_request(app_t *app)
{
    bool requested = app->focus_requested;
    app->focus_requested = false;
    return requested;
}

bool app_take_redraw_request(app_t *app)
{
    bool requested = app->redraw_requested;
    app->redraw_requested = false;
    return requested;
}

int app_take_redraw_flags(app_t *app)
{
    int flags = app->redraw_flags;
    app->redraw_flags = 0;
    return flags;
}

bool app_take_exit_request(app_t *app)
{
    bool requested = app->exit_requested;
    app->exit_requested = false;
    return requested;
}

bool app_has_stopped(const app_t *app)
{
    return app->stop_reason != NULL;
}

const char *app_stop_reason(const app_t *app)
{
    return app->stop_reason != NULL ? app->stop_reason : "";
}

/* ---- what the calls of the contract share: declared in app_internal.h ---- */

const uint8_t *app_memory_at(IM3Runtime runtime, void *memory, uint32_t offset,
                                    int32_t length)
{
    uint32_t memory_size = m3_GetMemorySize(runtime);
    bool inside = length >= 0 && offset <= memory_size &&
                  (uint32_t)length <= memory_size - offset;
    return inside ? (const uint8_t *)memory + offset : NULL;
}

uint8_t *app_memory_to_fill(IM3Runtime runtime, void *memory, uint32_t offset,
                                      int32_t capacity)
{
    return app_memory_at(runtime, memory, offset, capacity) != NULL ? (uint8_t *)memory + offset
                                                                    : NULL;
}

bool app_text_copy(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                      char terminated[HOST_TEXT_MAX + 1])
{
    const uint8_t *text = app_memory_at(runtime, memory, offset, length);
    if (text == NULL) {
        return false;
    }
    if (length > HOST_TEXT_MAX) {
        /* Cut between characters, not inside one of several bytes. */
        length = HOST_TEXT_MAX;
        while (length > 0 && gfx_is_utf8_tail(text[length])) {
            length--;
        }
    }
    memcpy(terminated, text, (size_t)length);
    terminated[length] = '\0';
    return true;
}

int32_t app_not_drawing(app_t *app)
{
    static const char TOLD[] = "a drawing call outside app_on_draw() draws nothing: "
                               "change the state and call screen_redraw()";
    if (!app->not_drawing_logged && app->services != NULL && app->services->log_write != NULL) {
        app->not_drawing_logged = true;
        app->services->log_write(HOST_LOG_WARN, TOLD, (int)sizeof TOLD - 1);
    }
    return HOST_ERR_NOT_DRAWING;
}

bool app_file_name_copy(IM3Runtime runtime, void *memory, uint32_t offset, int32_t length,
                           char name[HOST_FILE_NAME_MAX + 1])
{
    const uint8_t *text = length >= 1 && length <= HOST_FILE_NAME_MAX
                              ? app_memory_at(runtime, memory, offset, length)
                              : NULL;
    if (text == NULL) {
        return false;
    }
    for (int32_t i = 0; i < length; i++) {
        char character = (char)text[i];
        bool allowed = (character >= 'a' && character <= 'z') ||
                       (character >= '0' && character <= '9') || character == '-' ||
                       character == '_' || (character == '.' && i > 0);
        if (!allowed) {
            return false;
        }
        name[i] = character;
    }
    name[length] = '\0';
    return true;
}

const uint8_t *app_picture_at(IM3Runtime runtime, void *memory, uint32_t offset)
{
    const uint8_t *header = app_memory_at(runtime, memory, offset, GFX_PICTURE_HEADER);
    if (header == NULL || header[0] == 0 || header[1] == 0) {
        return NULL;
    }
    return app_memory_at(runtime, memory, offset, (uint32_t)gfx_picture_bytes(header));
}
