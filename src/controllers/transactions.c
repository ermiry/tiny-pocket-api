#include <stdlib.h>

#include <time.h>

#include <cerver/types/string.h>

#include <cerver/collections/pool.h>

#include <cerver/http/response.h>
#include <cerver/http/json/json.h>

#include <cerver/utils/log.h>

#include <cmongo/crud.h>
#include <cmongo/select.h>

#include "models/transaction.h"

#include "controllers/transactions.h"

Pool *trans_pool = NULL;

const bson_t *trans_no_user_query_opts = NULL;
static CMongoSelect *trans_no_user_select = NULL;

HttpResponse *no_user_trans = NULL;

HttpResponse *trans_created_success = NULL;
HttpResponse *trans_created_bad = NULL;
HttpResponse *trans_deleted_success = NULL;
HttpResponse *trans_deleted_bad = NULL;

void pocket_trans_delete (void *trans_ptr);

static unsigned int pocket_trans_init_pool (void) {

	unsigned int retval = 1;

	trans_pool = pool_create (transaction_delete);
	if (trans_pool) {
		pool_set_create (trans_pool, transaction_new);
		pool_set_produce_if_empty (trans_pool, true);
		if (!pool_init (trans_pool, transaction_new, DEFAULT_TRANS_POOL_INIT)) {
			retval = 0;
		}

		else {
			cerver_log_error ("Failed to init trans pool!");
		}
	}

	else {
		cerver_log_error ("Failed to create trans pool!");
	}

	return retval;

}

static unsigned int pocket_trans_init_query_opts (void) {

	unsigned int retval = 1;

	trans_no_user_select = cmongo_select_new ();
	(void) cmongo_select_insert_field (trans_no_user_select, "title");
	(void) cmongo_select_insert_field (trans_no_user_select, "amount");
	(void) cmongo_select_insert_field (trans_no_user_select, "date");
	(void) cmongo_select_insert_field (trans_no_user_select, "category");

	trans_no_user_query_opts = mongo_find_generate_opts (trans_no_user_select);

	if (trans_no_user_query_opts) retval = 0;

	return retval;

}

static unsigned int pocket_trans_init_responses (void) {

	unsigned int retval = 1;

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

	if (
		no_user_trans
		&& trans_created_success && trans_created_bad
		&& trans_deleted_success && trans_deleted_bad
	) retval = 0;

	return retval;

}

unsigned int pocket_trans_init (void) {

	unsigned int errors = 0;

	errors |= pocket_trans_init_pool ();

	errors |= pocket_trans_init_query_opts ();

	errors |= pocket_trans_init_responses ();

	return errors;

}

void pocket_trans_end (void) {

	cmongo_select_delete (trans_no_user_select);
	bson_destroy ((bson_t *) trans_no_user_query_opts);

	pool_delete (trans_pool);
	trans_pool = NULL;

	http_response_delete (no_user_trans);

	http_response_delete (trans_created_success);
	http_response_delete (trans_created_bad);
	http_response_delete (trans_deleted_success);
	http_response_delete (trans_deleted_bad);

}

Transaction *pocket_trans_get_by_id_and_user (
	const String *trans_id, const bson_oid_t *user_oid
) {

	Transaction *trans = NULL;

	if (trans_id) {
		trans = (Transaction *) pool_pop (trans_pool);
		if (trans) {
			bson_oid_init_from_string (&trans->oid, trans_id->str);

			if (transaction_get_by_oid_and_user (
				trans,
				&trans->oid, user_oid,
				NULL
			)) {
				pocket_trans_delete (trans);
				trans = NULL;
			}
		}
	}

	return trans;

}

u8 pocket_trans_get_by_id_and_user_to_json (
	const char *trans_id, const bson_oid_t *user_oid,
	const bson_t *query_opts,
	char **json, size_t *json_len
) {

	u8 retval = 1;

	if (trans_id) {
		bson_oid_t trans_oid = { 0 };
		bson_oid_init_from_string (&trans_oid, trans_id);

		retval = transaction_get_by_oid_and_user_to_json (
			&trans_oid, user_oid,
			query_opts,
			json, json_len
		);
	}

	return retval;

}

Transaction *pocket_trans_create (
	const char *user_id,
	const char *title,
	const double amount,
	const char *category_id,
	const char *date
) {

	Transaction *trans = (Transaction *) pool_pop (trans_pool);
	if (trans) {
		bson_oid_init (&trans->oid, NULL);

		bson_oid_init_from_string (&trans->user_oid, user_id);

		if (title) (void) strncpy (trans->title, title, TRANSACTION_TITLE_LEN - 1);
		trans->amount = amount;

		if (category_id) {
			bson_oid_init_from_string (&trans->category_oid, category_id);
		}

		if (date) {
			int y = 0, M = 0, d = 0, h = 0, m = 0;
			float s = 0;
			(void) sscanf (date, "%d-%d-%dT%d:%d:%f", &y, &M, &d, &h, &m, &s);

			struct tm date;
			date.tm_year = y - 1900;	// Year since 1900
			date.tm_mon = M - 1;		// 0-11
			date.tm_mday = d;			// 1-31
			date.tm_hour = h;			// 0-23
			date.tm_min = m;			// 0-59
			date.tm_sec = (int) s;		// 0-61 (0-60 in C++11)

			trans->date = mktime (&date);
		}

		else {
			trans->date = time (NULL);
		}
	}

	return trans;

}

void pocket_trans_delete (void *trans_ptr) {

	(void) memset (trans_ptr, 0, sizeof (Transaction));
	(void) pool_push (trans_pool, trans_ptr);

}