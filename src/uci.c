#include <errno.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <string.h>

#include <uci.h>

#include <json-c/json.h>

#include "config.h"
#include "http.h"
#include "log.h"
#include "uci.h"

struct sections {
    const char *key;
    const char *section;
} section_map[] = {{"proto", "network.lan2"},
                   {"ipaddr", "network.lan2"},
                   {"netmask", "network.lan2"},
                   {"gateway", "network.lan2"},
                   {"dns", "network.lan2"},
                   {"ssid", "wireless.@wifi-iface[0]"},
                   {"key", "wireless.@wifi-iface[0]"},
                   {NULL, NULL}};

json_object *uci_section_jobj(const char *path) {
    /* Returns a json_object with all of the options from the given uci
       section pointers. Returns NULL if section not found */
    struct uci_context *uci_ctx = uci_alloc_context();
    json_object *jobj = NULL;

    /* uci_lookup_ptr requires mutable arg, so we copy so that we can
       use simpler string literals in the external interface */
    char *tuple = strdup(path);
    if (!tuple) {
        exit(errno);
    }
    struct uci_ptr ptr;
    if (uci_lookup_ptr(uci_ctx, &ptr, tuple, true) != UCI_OK) {
        free(tuple);
        return NULL;
    }
    free(tuple);
    if (ptr.flags & UCI_LOOKUP_COMPLETE) {
        jobj = json_object_new_object();
        struct uci_element *el, *list_el;
        uci_foreach_element(&ptr.s->options, el) {
            struct uci_option *option = uci_to_option(el);
            if (!strcmp(option->e.name, "key")) {
                /* Don't include the wireless key */
                continue;
            }
            if (option->type == UCI_TYPE_LIST) {
                json_object *jobj_a = json_object_new_array();
                uci_foreach_element(&option->v.list, list_el) {
                    json_object_array_add(
                        jobj_a, json_object_new_string(list_el->name));
                }
                json_object_object_add(jobj, option->e.name, jobj_a);
            } else {
                json_object_object_add(
                    jobj, option->e.name,
                    json_object_new_string(option->v.string));
            }
        }
    }
    uci_free_context(uci_ctx);
    return jobj;
}

int uci_simple_set(char *key, char *value) {
    struct uci_context *uci_ctx = uci_alloc_context();

    char const *section = NULL;
    for (struct sections *s = section_map; s->key; s++) {
        if (!strncmp(key, s->key, strlen(s->key))) {
            section = s->section;
        }
    }
    if (!section) {
        log_error("uci_simple_set: Couldn't find section for %s\n", key);
        return CONFIG_UCI_NO_SECTION;
    }
    /* Join the key with it's known path */
    char *tuple;
    if (asprintf(&tuple, "%s.%s", section, key) < 0) {
        log_error("Failed to allocate memory");
        exit(ENOMEM);
    }

    log_notice("uci_simple_set: Trying to set: %s\n", tuple);

    /* Lookup option by full path */
    struct uci_ptr ptr;
    if (uci_lookup_ptr(uci_ctx, &ptr, tuple, true) != UCI_OK) {
        log_error("uci_simple_set: Couldn't lookup path: %s\n", tuple);
        free(tuple);
        return CONFIG_UCI_LOOKUP_FAIL;
    }
    if (ptr.flags & UCI_LOOKUP_COMPLETE) {
        struct uci_element *el; //, *list_el;
        el = ptr.last;
        struct uci_option *option = uci_to_option(el);
        if (option->type == UCI_TYPE_LIST) {
            uci_delete(uci_ctx, &ptr);
            char *entry = strtok(value, ",");
            while (entry != NULL) {
                ptr.value = entry;
                uci_add_list(uci_ctx, &ptr);
                entry = strtok(NULL, ",");
            }
        } else {
            ptr.value = value;
            if (uci_set(uci_ctx, &ptr) != UCI_OK) {
                char *error = calloc(1, 256);
                uci_get_errorstr(uci_ctx, &error, NULL);
                log_error("Failed to set %s: %s\n", tuple, error);
                free(tuple);
                free(error);
                return CONFIG_UCI_SET_FAIL;
            }
        }
        if (uci_save(uci_ctx, ptr.p) != UCI_OK) {
            char *error = calloc(1, 256);
            uci_get_errorstr(uci_ctx, &error, NULL);
            log_error("Failed to save %s: %s\n", tuple, error);
            free(tuple);
            free(error);
            return CONFIG_UCI_SAVE_FAIL;
        }
        log_notice("Reset required");
        free(tuple);
        return CONFIG_OK;
    }
    free(tuple);
    log_error("key \"%s\" doesn't exist", key);
    return CONFIG_UCI_LOOKUP_INCOMPLETE;
}
