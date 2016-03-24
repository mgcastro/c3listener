#include <errno.h>
#include <string.h>

#include <uci.h>

#include <json-c/json.h>

#include "log.h"
#include "uci_json.h"

struct uci_context *uci_ctx = NULL;

json_object *uci_section_jobj(const char *path) {
    /* Returns a json_object with all of the options from the given uci
       section pointers. Returns NULL if section not found */
    json_object *jobj = NULL;
    if (!uci_ctx) {
        uci_ctx = uci_alloc_context();
    }
    char *tuple = strdupa(path);
    if (!tuple) {
        exit(errno);
    }
    struct uci_ptr ptr;
    if (uci_lookup_ptr(uci_ctx, &ptr, tuple, true) != UCI_OK) {
        return NULL;
    }
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
    return jobj;
}
