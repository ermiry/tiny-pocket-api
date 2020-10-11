#ifndef _POCKET_ROLES_H_
#define _POCKET_ROLES_H_

#include "cerver/types/string.h"

#include <bson/bson.h>

extern unsigned int pocket_roles_init (void);

extern void pocket_roles_end (void);

extern const String *pocket_roles_get_by_oid (const bson_oid_t *role_oid);

#endif