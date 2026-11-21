/* SPDX-License-Identifier: MIT */
#include "web_settings_json.h"

#include <string.h>

json_t *web_settings_json(const appconf_t *settings, char *order)
{
    json_t *json = json_new_object();
    if (json == NULL) {
        return NULL;
    }
    /* Every setting there is, by its key, as the store has it; a secret is
     * never read back. */
    int count;
    const appconf_field_t *fields = appconf_fields(&count);
    for (int i = 0; i < count; i++) {
        const appconf_field_t *field = &fields[i];
        if ((field->flags & APPCONF_SECRET) != 0) {
            continue;
        }
        json_t *member;
        if (field->kind == APPCONF_TEXT) {
            member = json_add_string(json, field->key, appconf_text(settings, field));
        } else if (field->kind == APPCONF_FLAG) {
            member = json_add_bool(json, field->key, appconf_number(settings, field) != 0);
        } else {
            member = json_add_number(json, field->key, appconf_number(settings, field));
        }
        if (member == NULL) {
            goto failed;
        }
    }
    if (json_add_string(json, "hostname", settings->hostname) == NULL) {
        goto failed;
    }
    {
        json_t *languages = json_add_item(json, "languages", json_new_array());
        if (languages == NULL) {
            goto failed;
        }
        const appconf_language_t *known = appconf_languages(&count);
        for (int i = 0; i < count; i++) {
            json_t *language = json_new_object();
            if (language == NULL) {
                goto failed;
            }
            json_array_add(languages, language);
            if (json_add_string(language, "code", known[i].code) == NULL ||
                json_add_string(language, "name", known[i].name) == NULL) {
                goto failed;
            }
        }
    }
    {
        json_t *names = json_add_item(json, "menu_order", json_new_array());
        if (names == NULL) {
            goto failed;
        }
        char *rest = NULL;
        for (char *name = strtok_r(order, "\n", &rest); name != NULL;
             name = strtok_r(NULL, "\n", &rest)) {
            json_t *item = json_new_string(name);
            if (item == NULL) {
                goto failed;
            }
            json_array_add(names, item);
        }
    }
    return json;

failed:
    json_free(json);
    return NULL;
}
