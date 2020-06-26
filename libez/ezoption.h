#ifndef EZOPTION_H
#define EZOPTION_H

#define XPS_MAX_OPTIONS 64

typedef struct options {
    char *key, *value;
} XPS_Option_t;

// XPS_StatusCode_t XPS_set_option(char *key, char *value);
XPS_StatusCode_t XPS_get_option(char *key, char **value);

#endif
