#pragma once
#include <stddef.h>
void *dt_get_prop(const char *device, const char *prop, size_t *size);
typedef struct dt_node dt_node_t;
dt_node_t *dt_get(const char *name);
void *dt_prop(dt_node_t *node, const char *key, size_t *lenp);
