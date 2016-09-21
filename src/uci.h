#pragma once

json_object *uci_section_jobj(const char *);
int uci_simple_set(char *, char *);

enum {
    CONFIG_UCI_NOT_FOUND = 64,
    CONFIG_UCI_NO_SECTION,
    CONFIG_UCI_LOOKUP_FAIL,
    CONFIG_UCI_SET_FAIL,
    CONFIG_UCI_SAVE_FAIL,
    CONFIG_UCI_LOOKUP_INCOMPLETE
};
