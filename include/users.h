#ifndef _POCKET_USERS_H_
#define _POCKET_USERS_H_

#include <cerver/handler.h>

#include <cerver/http/request.h>

#define DEFAULT_USERS_POOL_INIT			16

#pragma region main

extern unsigned int pocket_users_init (void);

extern void pocket_users_end (void);

// {
//   "email": "erick.salas@ermiry.com",
//   "iat": 1596532954
//   "id": "5eb2b13f0051f70011e9d3af",
//   "name": "Erick Salas",
//   "role": "god",
//   "username": "erick",
// }
extern void *pocket_user_parse_from_json (void *user_json_ptr);

extern void pocket_user_delete (void *user_ptr);

#pragma endregion

#pragma region routes

// GET api/users/
extern void users_handler (CerverReceive *cr, HttpRequest *request);

// POST api/users/login
extern void users_login_handler (CerverReceive *cr, HttpRequest *request);

// POST api/users/register
extern void users_register_handler (CerverReceive *cr, HttpRequest *request);

#pragma endregion

#endif