#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/types/types.h>
#include <cerver/types/string.h>

#include <cerver/handler.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/json/json.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

#include "categories.h"
#include "errors.h"
#include "mongo.h"
#include "pocket.h"
#include "roles.h"
#include "transactions.h"
#include "users.h"
#include "version.h"

#include "models/action.h"
#include "models/category.h"
#include "models/role.h"
#include "models/user.h"

#pragma region main

const String *PORT = NULL;

static const String *MONGO_URI = NULL;
static const String *MONGO_APP_NAME = NULL;
static const String *MONGO_DB = NULL;

unsigned int CERVER_RECEIVE_BUFFER_SIZE = 4096;
unsigned int CERVER_TH_THREADS = 4;

HttpResponse *oki_doki = NULL;
HttpResponse *bad_request = NULL;
HttpResponse *server_error = NULL;
HttpResponse *bad_user = NULL;
HttpResponse *missing_values = NULL;

static HttpResponse *pocket_works = NULL;
static HttpResponse *current_version = NULL;

static HttpResponse *no_user_trans = NULL;

static HttpResponse *trans_created_success = NULL;
static HttpResponse *trans_created_bad = NULL;
static HttpResponse *trans_deleted_success = NULL;
static HttpResponse *trans_deleted_bad = NULL;

static HttpResponse *no_user_categories = NULL;
static HttpResponse *no_user_category = NULL;

static HttpResponse *category_created_success = NULL;
static HttpResponse *category_created_bad = NULL;
static HttpResponse *category_deleted_success = NULL;
static HttpResponse *category_deleted_bad = NULL;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static unsigned int pocket_env_get_port (void) {
	
	unsigned int retval = 1;

	char *port_env = getenv ("PORT");
	if (port_env) {
		PORT = str_new (port_env);

		retval = 0;
	}

	else {
		cerver_log_error ("Failed to get PORT from env!");
	}

	return retval;

}

static unsigned int pocket_env_get_mongo_app_name (void) {

	unsigned int retval = 1;

	char *mongo_app_name_env = getenv ("MONGO_APP_NAME");
	if (mongo_app_name_env) {
		MONGO_APP_NAME = str_new (mongo_app_name_env);

		retval = 0;
	}

	else {
		cerver_log_error ("Failed to get MONGO_APP_NAME from env!");
	}

	return retval;

}

static unsigned int pocket_env_get_mongo_db (void) {

	unsigned int retval = 1;

	char *mongo_db_env = getenv ("MONGO_DB");
	if (mongo_db_env) {
		MONGO_DB = str_new (mongo_db_env);

		retval = 0;
	}

	else {
		cerver_log_error ("Failed to get MONGO_DB from env!");
	}

	return retval;

}

static unsigned int pocket_env_get_mongo_uri (void) {

	unsigned int retval = 1;

	char *mongo_uri_env = getenv ("MONGO_URI");
	if (mongo_uri_env) {
		MONGO_URI = str_new (mongo_uri_env);

		retval = 0;
	}

	else {
		cerver_log_error ("Failed to get MONGO_URI from env!");
	}

	return retval;

}

static void gepp_env_get_cerver_receive_buffer_size (void) {

	char *buffer_size = getenv ("CERVER_RECEIVE_BUFFER_SIZE");
	if (buffer_size) {
		CERVER_RECEIVE_BUFFER_SIZE = (unsigned int) atoi (buffer_size);
		cerver_log_success (
			"CERVER_RECEIVE_BUFFER_SIZE -> %d\n", CERVER_RECEIVE_BUFFER_SIZE
		);
	}

	else {
		cerver_log_warning (
			"Failed to get CERVER_RECEIVE_BUFFER_SIZE from env - using default %d!",
			CERVER_RECEIVE_BUFFER_SIZE
		);
	}
}

static void gepp_env_get_cerver_th_threads (void) {

	char *th_threads = getenv ("CERVER_TH_THREADS");
	if (th_threads) {
		CERVER_TH_THREADS = (unsigned int) atoi (th_threads);
		cerver_log_success ("CERVER_TH_THREADS -> %d\n", CERVER_TH_THREADS);
	}

	else {
		cerver_log_warning (
			"Failed to get CERVER_TH_THREADS from env - using default %d!",
			CERVER_TH_THREADS
		);
	}

}

#pragma GCC diagnostic pop

static unsigned int pocket_init_env (void) {

	unsigned int errors = 0;

	errors |= pocket_env_get_port ();

	errors |= pocket_env_get_mongo_uri ();

	errors |= pocket_env_get_mongo_app_name ();

	errors |= pocket_env_get_mongo_db ();

	return errors;

}

static unsigned int pocket_mongo_connect (void) {

	unsigned int errors = 0;

	bool connected_to_mongo = false;

	mongo_set_uri (MONGO_URI->str);
	mongo_set_app_name (MONGO_APP_NAME->str);
	mongo_set_db_name (MONGO_DB->str);

	if (!mongo_connect ()) {
		// test mongo connection
		if (!mongo_ping_db ()) {
			cerver_log_success ("Connected to Mongo DB!");

			// open handle to actions collection
			errors |= actions_collection_get ();

			// open handle to categories collection
			errors |= categories_collection_get ();

			// open handle to roles collection
			errors |= roles_collection_get ();

			// open handle to transactions collection
			errors |= transactions_collection_get ();

			// open handle to user collection
			errors |= users_collection_get ();

			connected_to_mongo = true;
		}
	}

	if (!connected_to_mongo) {
		cerver_log_error ("Failed to connect to mongo!");
		errors |= 1;
	}

	return errors;

}

static unsigned int pocket_mongo_init (void) {

	unsigned int retval = 1;

	if (!pocket_mongo_connect ()) {
		if (!pocket_roles_init ()) {
			retval = 0;
		}

		else {
			cerver_log_error ("Failed to get roles from db!");
		}
	}

	return retval;

}

static unsigned int pocket_init_responses (void) {

	unsigned int retval = 1;

	oki_doki = http_response_json_key_value (
		(http_status) 200, "oki", "doki"
	);

	bad_request = http_response_json_key_value (
		(http_status) 400, "error", "Bad request!"
	);

	server_error = http_response_json_key_value (
		(http_status) 500, "error", "Internal server error!"
	);

	bad_user = http_response_json_key_value (
		(http_status) 400, "error", "Bad user!"
	);

	missing_values = http_response_json_key_value (
		(http_status) 400, "error", "Missing values!"
	);

	pocket_works = http_response_json_key_value (
		(http_status) 200, "msg", "Pocket works!"
	);

	char *status = c_string_create ("%s - %s", POCKET_VERSION_NAME, POCKET_VERSION_DATE);
	if (status) {
		current_version = http_response_json_key_value (
			(http_status) 200, "version", status
		);

		free (status);
	}

	/*** transactions ***/
	no_user_trans = http_response_json_key_value (
		(http_status) 404, "msg", "Failed to get user's transaction(s)"
	);

	trans_created_success = http_response_json_key_value (
		(http_status) 200, "oki", "doki"
	);

	trans_created_bad = http_response_json_key_value (
		(http_status) 400, "error", "Failed to create transaction!"
	);

	trans_deleted_success = http_response_json_key_value (
		(http_status) 200, "oki", "doki"
	);

	trans_deleted_bad = http_response_json_key_value (
		(http_status) 400, "error", "Failed to delete transaction!"
	);

	/*** categories ****/

	no_user_categories = http_response_json_key_value (
		(http_status) 404, "msg", "Failed to get user's categories"
	);

	no_user_category = http_response_json_key_value (
		(http_status) 404, "msg", "User's category was not found"
	);

	category_created_success = http_response_json_key_value (
		(http_status) 200, "oki", "doki"
	);

	category_created_bad = http_response_json_key_value (
		(http_status) 400, "error", "Failed to create category!"
	);

	category_deleted_success = http_response_json_key_value (
		(http_status) 200, "oki", "doki"
	);

	category_deleted_bad = http_response_json_key_value (
		(http_status) 400, "error", "Failed to delete category!"
	);

	if (
		oki_doki && bad_request && server_error && bad_user && missing_values
		&& pocket_works && current_version
		&& no_user_trans
		&& trans_created_success && trans_created_bad
		&& trans_deleted_success && trans_deleted_bad
		&& no_user_categories && no_user_category
		&& category_created_success && category_created_bad
		&& category_deleted_success && category_deleted_bad
	) retval = 0;

	return retval;

}

// inits pocket main values
unsigned int pocket_init (void) {

	unsigned int errors = 0;

	if (!pocket_init_env ()) {
		errors |= pocket_mongo_init ();

		errors |= pocket_users_init ();

		errors |= pocket_categories_init ();

		errors |= pocket_trans_init ();

		errors |= pocket_init_responses ();
	}

	return errors;  

}

static unsigned int pocket_mongo_end (void) {

	if (mongo_get_status () == MONGO_STATUS_CONNECTED) {
		actions_collection_close ();

		categories_collection_close ();

		roles_collection_close ();

		transactions_collection_close ();

		users_collection_close ();

		mongo_disconnect ();
	}

	return 0;

}

// ends pocket main values
unsigned int pocket_end (void) {

	unsigned int errors = 0;

	errors |= pocket_mongo_end ();

	pocket_roles_end ();

	pocket_users_end ();

	pocket_categories_end ();

	pocket_trans_end ();

	http_respponse_delete (oki_doki);
	http_respponse_delete (bad_request);
	http_respponse_delete (server_error);
	http_respponse_delete (bad_user);
	http_respponse_delete (missing_values);

	http_respponse_delete (pocket_works);
	http_respponse_delete (current_version);

	http_respponse_delete (no_user_trans);

	http_respponse_delete (trans_created_success);
	http_respponse_delete (trans_created_bad);
	http_respponse_delete (trans_deleted_success);
	http_respponse_delete (trans_deleted_bad);

	http_respponse_delete (no_user_categories);
	http_respponse_delete (no_user_category);

	http_respponse_delete (category_created_success);
	http_respponse_delete (category_created_bad);
	http_respponse_delete (category_deleted_success);
	http_respponse_delete (category_deleted_bad);

	str_delete ((String *) MONGO_URI);
	str_delete ((String *) MONGO_APP_NAME);
	str_delete ((String *) MONGO_DB);

	return errors;

}

#pragma endregion

#pragma region routes

// GET api/pocket
void pocket_handler (CerverReceive *cr, HttpRequest *request) {

	(void) http_response_send (pocket_works, cr->cerver, cr->connection);

}

// GET api/pocket/version
void pocket_version_handler (CerverReceive *cr, HttpRequest *request) {

	(void) http_response_send (current_version, cr->cerver, cr->connection);

}

// GET api/pocket/auth
void pocket_auth_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;

	if (user) {
		#ifdef POCKET_DEBUG
		user_print (user);
		#endif

		(void) http_response_send (oki_doki, cr->cerver, cr->connection);
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

#pragma endregion

#pragma region transactions

static char *pocket_transactions_handler_generate_json (
	User *user,
	mongoc_cursor_t *trans_cursor,
	size_t *json_len
) {

	char *retval = NULL;

	bson_t *doc = bson_new ();
	if (doc) {
		(void) bson_append_int32 (doc, "count", -1, user->trans_count);

		bson_t trans_array = { 0 };
		(void) bson_append_array_begin (doc, "transactions", -1, &trans_array);
		char buf[16] = { 0 };
		const char *key = NULL;
		size_t keylen = 0;

		int i = 0;
		const bson_t *trans_doc = NULL;
		while (mongoc_cursor_next (trans_cursor, &trans_doc)) {
			keylen = bson_uint32_to_string (i, &key, buf, sizeof (buf));
			(void) bson_append_document (&trans_array, key, (int) keylen, trans_doc);

			bson_destroy ((bson_t *) trans_doc);

			i++;
		}
		(void) bson_append_array_end (doc, &trans_array);

		retval = bson_as_relaxed_extended_json (doc, json_len);
	}

	return retval;

}

// GET api/pocket/transactions
// get all the authenticated user's transactions
void pocket_transactions_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;
	if (user) {
		// get user's transactions from the db
		if (!user_get_by_id (user, user->id, user_transactions_query_opts)) {
			mongoc_cursor_t *trans_cursor = transactions_get_all_by_user (
				&user->oid, trans_no_user_query_opts
			);

			if (trans_cursor) {
				// convert them to json and send them back
				size_t json_len = 0;
				char *json = pocket_transactions_handler_generate_json (
					user, trans_cursor, &json_len
				);

				if (json) {
					(void) http_response_json_custom_reference_send (
						cr,
						200,
						json, json_len
					);

					free (json);
				}

				else {
					(void) http_response_send (server_error, cr->cerver, cr->connection);
				}

				mongoc_cursor_destroy (trans_cursor);
			}

			else {
				(void) http_response_send (no_user_trans, cr->cerver, cr->connection);
			}
		}

		else {
			(void) http_response_send (bad_user, cr->cerver, cr->connection);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

static void pocket_trans_parse_json (
	json_t *json_body,
	const char **title,
	double *amount,
	const char **category,
	const char **date
) {

	// get values from json to create a new transaction
	const char *key = NULL;
	json_t *value = NULL;
	if (json_typeof (json_body) == JSON_OBJECT) {
		json_object_foreach (json_body, key, value) {
			if (!strcmp (key, "title")) {
				*title = json_string_value (value);
				(void) printf ("title: \"%s\"\n", *title);
			}

			else if (!strcmp (key, "amount")) {
				*amount = json_real_value (value);
				(void) printf ("amount: %f\n", *amount);
			}

			else if (!strcmp (key, "category")) {
				*category = json_string_value (value);
				(void) printf ("category: \"%s\"\n", *category);
			}

			else if (!strcmp (key, "date")) {
				*date = json_string_value (value);
				(void) printf ("date: \"%s\"\n", *date);
			}
		}
	}

}

static PocketError pocket_transaction_create_handler_internal (
	Transaction **trans,
	const char *user_id, const String *request_body
) {

	PocketError error = POCKET_ERROR_NONE;

	if (request_body) {
		const char *title = NULL;
		double amount = 0;
		const char *category_id = NULL;
		const char *date = NULL;

		json_error_t json_error =  { 0 };
		json_t *json_body = json_loads (request_body->str, 0, &json_error);
		if (json_body) {
			pocket_trans_parse_json (
				json_body,
				&title, &amount,
				&category_id, &date
			);

			if (title && (amount != 0)) {
				*trans = pocket_trans_create (
					user_id,
					title, amount
				);

				if (*trans == NULL) error = POCKET_ERROR_SERVER_ERROR;
			}

			else {
				error = POCKET_ERROR_MISSING_VALUES;
			}

			json_decref (json_body);
		}

		else {
			cerver_log_error (
				"json_loads () - json error on line %d: %s\n", 
				json_error.line, json_error.text
			);

			error = POCKET_ERROR_BAD_REQUEST;
		}
	}

	return error;

}

// POST api/pocket/transactions
// a user has requested to create a new transaction
void pocket_transaction_create_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;
	if (user) {
		Transaction *trans = NULL;

		PocketError error = pocket_transaction_create_handler_internal (
			&trans,
			user->id, request->body
		);

		if (error == POCKET_ERROR_NONE) {
			#ifdef POCKET_DEBUG
			transaction_print (trans);
			#endif

			if (!mongo_insert_one (
				transactions_collection,
				transaction_to_bson (trans)
			)) {
				// update users values
				(void) mongo_update_one (
					users_collection,
					user_query_id (user->id),
					user_create_update_pocket_transactions ()
				);

				// return success to user
				(void) http_response_send (
					trans_created_success,
					cr->cerver, cr->connection
				);
			}

			else {
				(void) http_response_send (
					trans_created_bad,
					cr->cerver, cr->connection
				);
			}
			
			pocket_trans_delete (trans);
		}

		else {
			pocket_error_send_response (error, cr);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

// GET api/pocket/transactions/:id
// returns information about an existing transaction that belongs to a user
void pocket_transaction_get_handler (CerverReceive *cr, HttpRequest *request) {

	const String *trans_id = request->params[0];

	User *user = (User *) request->decoded_data;
	if (user) {
		Transaction *trans = (Transaction *) pool_pop (trans_pool);
		if (trans) {
			bson_oid_init_from_string (&trans->oid, trans_id->str);
			bson_oid_init_from_string (&trans->user_oid, user->id);

			const bson_t *trans_bson = transaction_find_by_oid_and_user (
				&trans->oid, &trans->user_oid,
				trans_no_user_query_opts
			);

			if (trans_bson) {
				size_t json_len = 0;
				char *json = bson_as_relaxed_extended_json (trans_bson, &json_len);
				if (json) {
					(void) http_response_json_custom_reference_send (
						cr, 200, json, json_len
					);

					free (json);
				}

				bson_destroy ((bson_t *) trans_bson);
			}

			else {
				(void) http_response_send (no_user_trans, cr->cerver, cr->connection);
			}

			pocket_trans_delete (trans);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

static u8 pocket_transaction_update_handler_internal (
	Transaction *trans, const String *request_body
) {

	u8 retval = 1;

	if (request_body) {
		const char *title = NULL;
		double amount = 0;
		const char *category_id = NULL;
		const char *date = NULL;

		json_error_t error =  { 0 };
		json_t *json_body = json_loads (request_body->str, 0, &error);
		if (json_body) {
			pocket_trans_parse_json (
				json_body,
				&title, &amount,
				&category_id, &date
			);

			if (title) (void) strncpy (trans->title, title, TRANSACTION_TITLE_LEN);
			trans->amount = amount;

			json_decref (json_body);

			retval = 0;
		}

		else {
			cerver_log_error (
				"json_loads () - json error on line %d: %s\n", 
				error.line, error.text
			);
		}
	}

	return retval;

}

// POST api/pocket/transactions/:id
// a user wants to update an existing transaction
void pocket_transaction_update_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;
	if (user) {
		bson_oid_init_from_string (&user->oid, user->id);

		Transaction *trans = pocket_trans_get_by_id_and_user (
			request->params[0], &user->oid
		);

		if (trans) {
			// get update values
			if (!pocket_transaction_update_handler_internal (
				trans, request->body
			)) {
				// update the transaction in the db
				if (!mongo_update_one (
					transactions_collection,
					transaction_query_oid (&trans->oid),
					transaction_update_bson (trans)
				)) {
					(void) http_response_send (oki_doki, cr->cerver, cr->connection);
				}

				else {
					(void) http_response_send (server_error, cr->cerver, cr->connection);
				}
			}

			pocket_trans_delete (trans);
		}

		else {
			(void) http_response_send (bad_request, cr->cerver, cr->connection);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

// DELETE api/pocket/transactions/:id
// deletes an existing user's transaction
void pocket_transaction_delete_handler (CerverReceive *cr, HttpRequest *request) {

	const String *trans_id = request->params[0];

	User *user = (User *) request->decoded_data;
	if (user) {
		bson_t *trans_query = bson_new ();
		if (trans_query) {
			bson_oid_t oid = { 0 };

			bson_oid_init_from_string (&oid, trans_id->str);
			(void) bson_append_oid (trans_query, "_id", -1, &oid);

			bson_oid_init_from_string (&oid, user->id);
			(void) bson_append_oid (trans_query, "user", -1, &oid);

			if (!mongo_delete_one (transactions_collection, trans_query)) {
				#ifdef POCKET_DEBUG
				cerver_log_debug ("Deleted transaction %s", trans_id->str);
				#endif

				(void) http_response_send (trans_deleted_success, cr->cerver, cr->connection);
			}

			else {
				(void) http_response_send (trans_deleted_bad, cr->cerver, cr->connection);
			}
		}

		else {
			(void) http_response_send (server_error, cr->cerver, cr->connection);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

#pragma endregion

#pragma region categories

static char *pocket_categories_handler_generate_json (
	User *user,
	mongoc_cursor_t *categories_cursor,
	size_t *json_len
) {

	char *retval = NULL;

	bson_t *doc = bson_new ();
	if (doc) {
		(void) bson_append_int32 (doc, "count", -1, user->categories_count);

		bson_t categories_array = { 0 };
		(void) bson_append_array_begin (doc, "categories", -1, &categories_array);
		char buf[16] = { 0 };
		const char *key = NULL;
		size_t keylen = 0;

		int i = 0;
		const bson_t *category_doc = NULL;
		while (mongoc_cursor_next (categories_cursor, &category_doc)) {
			keylen = bson_uint32_to_string (i, &key, buf, sizeof (buf));
			(void) bson_append_document (&categories_array, key, (int) keylen, category_doc);

			bson_destroy ((bson_t *) category_doc);

			i++;
		}
		(void) bson_append_array_end (doc, &categories_array);

		retval = bson_as_relaxed_extended_json (doc, json_len);
	}

	return retval;

}

// GET api/pocket/categories
// get all the authenticated user's categories
void pocket_categories_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;
	if (user) {
		// get user's categories from the db
		if (!user_get_by_id (user, user->id, user_categories_query_opts)) {
			mongoc_cursor_t *categories_cursor = categories_get_all_by_user (
				&user->oid, category_no_user_query_opts
			);

			if (categories_cursor) {
				// convert them to json and send them back
				size_t json_len = 0;
				char *json = pocket_categories_handler_generate_json (
					user, categories_cursor, &json_len
				);

				if (json) {
					(void) http_response_json_custom_reference_send (
						cr,
						200,
						json, json_len
					);

					free (json);
				}

				else {
					(void) http_response_send (server_error, cr->cerver, cr->connection);
				}

				mongoc_cursor_destroy (categories_cursor);
			}

			else {
				(void) http_response_send (no_user_categories, cr->cerver, cr->connection);
			}
		}

		else {
			(void) http_response_send (bad_user, cr->cerver, cr->connection);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

static void pocket_category_parse_json (
	json_t *json_body,
	const char **title,
	const char **description,
	const char **color
) {

	// get values from json to create a new category
	const char *key = NULL;
	json_t *value = NULL;
	if (json_typeof (json_body) == JSON_OBJECT) {
		json_object_foreach (json_body, key, value) {
			if (!strcmp (key, "title")) {
				*title = json_string_value (value);
				(void) printf ("title: \"%s\"\n", *title);
			}

			else if (!strcmp (key, "description")) {
				*description = json_string_value (value);
				(void) printf ("description: \"%s\"\n", *description);
			}

			else if (!strcmp (key, "color")) {
				*color = json_string_value (value);
				(void) printf ("color: \"%s\"\n", *color);
			}
		}
	}

}

static Category *pocket_category_create_handler_internal (
	const char *user_id, const String *request_body
) {

	Category *category = NULL;

	if (request_body) {
		const char *title = NULL;
		const char *description = NULL;
		const char *color = NULL;

		json_error_t error =  { 0 };
		json_t *json_body = json_loads (request_body->str, 0, &error);
		if (json_body) {
			pocket_category_parse_json (
				json_body,
				&title,
				&description,
				&color
			);

			category = pocket_category_create (
				user_id,
				title, description,
				color
			);

			json_decref (json_body);
		}

		else {
			cerver_log_error (
				"json_loads () - json error on line %d: %s\n", 
				error.line, error.text
			);
		}
	}

	return category;

}

// POST api/pocket/categories
// a user has requested to create a new category
void pocket_category_create_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;
	if (user) {
		Category *category = pocket_category_create_handler_internal (
			user->id, request->body
		);

		if (category) {
			#ifdef POCKET_DEBUG
			category_print (category);
			#endif

			if (!mongo_insert_one (
				categories_collection,
				category_to_bson (category)
			)) {
				// update users values
				(void) mongo_update_one (
					users_collection,
					user_query_id (user->id),
					user_create_update_pocket_categories ()
				);

				// return success to user
				(void) http_response_send (
					category_created_success,
					cr->cerver, cr->connection
				);
			}

			else {
				(void) http_response_send (
					category_created_bad,
					cr->cerver, cr->connection
				);
			}
			
			pocket_category_delete (category);
		}

		else {
			(void) http_response_send (
				category_created_bad,
				cr->cerver, cr->connection
			);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

// GET api/pocket/categories/:id
// returns information about an existing category that belongs to a user
void pocket_category_get_handler (CerverReceive *cr, HttpRequest *request) {

	const String *category_id = request->params[0];

	User *user = (User *) request->decoded_data;
	if (user) {
		Category *category = (Category *) pool_pop (categories_pool);
		if (category) {
			bson_oid_init_from_string (&category->oid, category_id->str);
			bson_oid_init_from_string (&category->user_oid, user->id);

			const bson_t *category_bson = category_find_by_oid_and_user (
				&category->oid, &category->user_oid,
				category_no_user_query_opts
			);

			if (category_bson) {
				size_t json_len = 0;
				char *json = bson_as_relaxed_extended_json (category_bson, &json_len);
				if (json) {
					(void) http_response_json_custom_reference_send (
						cr, 200, json, json_len
					);

					free (json);
				}

				bson_destroy ((bson_t *) category_bson);
			}

			else {
				(void) http_response_send (no_user_category, cr->cerver, cr->connection);
			}

			pocket_category_delete (category);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

static u8 pocket_category_update_handler_internal (
	Category *category, const String *request_body
) {

	u8 retval = 1;

	if (request_body) {
		const char *title = NULL;
		const char *description = NULL;
		const char *color = NULL;

		json_error_t error =  { 0 };
		json_t *json_body = json_loads (request_body->str, 0, &error);
		if (json_body) {
			pocket_category_parse_json (
				json_body,
				&title,
				&description,
				&color
			);

			if (title) (void) strncpy (category->title, title, CATEGORY_TITLE_LEN);
			if (description) (void) strncpy (category->description, description, CATEGORY_DESCRIPTION_LEN);
			if (color) (void) strncpy (category->color, color, CATEGORY_COLOR_LEN);

			json_decref (json_body);

			retval = 0;
		}

		else {
			cerver_log_error (
				"json_loads () - json error on line %d: %s\n", 
				error.line, error.text
			);
		}
	}

	return retval;

}

// POST api/pocket/categories/:id
// a user wants to update an existing category
void pocket_category_update_handler (CerverReceive *cr, HttpRequest *request) {

	User *user = (User *) request->decoded_data;
	if (user) {
		bson_oid_init_from_string (&user->oid, user->id);

		Category *category = pocket_category_get_by_id_and_user (
			request->params[0], &user->oid
		);

		if (category) {
			// get update values
			if (!pocket_category_update_handler_internal (
				category, request->body
			)) {
				// update the category in the db
				if (!mongo_update_one (
					categories_collection,
					category_query_oid (&category->oid),
					category_update_bson (category)
				)) {
					(void) http_response_send (oki_doki, cr->cerver, cr->connection);
				}

				else {
					(void) http_response_send (server_error, cr->cerver, cr->connection);
				}
			}

			pocket_category_delete (category);
		}

		else {
			(void) http_response_send (bad_request, cr->cerver, cr->connection);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

// DELETE api/pocket/categories/:id
// deletes an existing user's category
void pocket_category_delete_handler (CerverReceive *cr, HttpRequest *request) {

	const String *category_id = request->params[0];

	User *user = (User *) request->decoded_data;
	if (user) {
		bson_t *category_query = bson_new ();
		if (category_query) {
			bson_oid_t oid = { 0 };

			bson_oid_init_from_string (&oid, category_id->str);
			(void) bson_append_oid (category_query, "_id", -1, &oid);

			bson_oid_init_from_string (&oid, user->id);
			(void) bson_append_oid (category_query, "user", -1, &oid);

			if (!mongo_delete_one (categories_collection, category_query)) {
				#ifdef POCKET_DEBUG
				cerver_log_debug ("Deleted category %s", category_id->str);
				#endif

				(void) http_response_send (category_deleted_success, cr->cerver, cr->connection);
			}

			else {
				(void) http_response_send (category_deleted_bad, cr->cerver, cr->connection);
			}
		}

		else {
			(void) http_response_send (server_error, cr->cerver, cr->connection);
		}
	}

	else {
		(void) http_response_send (bad_user, cr->cerver, cr->connection);
	}

}

#pragma endregion