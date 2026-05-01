/* vi:set ts=8 sts=4 sw=4 noet:
 *
 * VIM - Vi IMproved	by Bram Moolenaar
 *
 * Do ":help uganda"  in Vim to read copying and usage conditions.
 * Do ":help credits" in Vim to see a list of people who contributed.
 * See README.txt for an overview of the Vim source code.
 */

/*
 * if_curl.c: cURL interface by Yasuhiro Matsumoto
 */

#include "vim.h"

#if defined(FEAT_CURL) || defined(PROTO)

#include <curl/curl.h>

typedef struct
{
    char_u	*data;
    size_t	len;
    size_t	alloc;
} curl_buffer_T;

/*
 * Async HTTP request structure.
 */
typedef struct curl_async_S curl_async_T;
struct curl_async_S
{
    curl_async_T    *ca_next;
    curl_async_T    *ca_prev;
    CURL	    *ca_curl;
    callback_T	    ca_callback;	// completion callback
    curl_buffer_T   ca_body;		// response body buffer
    curl_buffer_T   ca_headers;		// response header buffer
    struct curl_slist *ca_req_headers;	// request headers (to free later)
};

static curl_async_T	*first_curl_async = NULL;
static CURLM		*curl_multi_handle = NULL;

/*
 * Callback for curl to write received data into a buffer.
 */
    static size_t
curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    curl_buffer_T   *buf = (curl_buffer_T *)userdata;
    size_t	    realsize = size * nmemb;

    if (buf->len + realsize + 1 > buf->alloc)
    {
	size_t	newalloc = (buf->len + realsize + 1) * 2;
	char_u	*newdata = vim_realloc(buf->data, newalloc);

	if (newdata == NULL)
	    return 0;
	buf->data = newdata;
	buf->alloc = newalloc;
    }
    mch_memmove(buf->data + buf->len, ptr, realsize);
    buf->len += realsize;
    buf->data[buf->len] = NUL;
    return realsize;
}

/*
 * Callback for curl to collect response headers.
 * Each header line is appended to the buffer including the trailing \r\n.
 */
    static size_t
curl_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    curl_buffer_T   *buf = (curl_buffer_T *)userdata;
    size_t	    realsize = size * nmemb;

    if (buf->len + realsize + 1 > buf->alloc)
    {
	size_t	newalloc = (buf->len + realsize + 1) * 2;
	char_u	*newdata = vim_realloc(buf->data, newalloc);

	if (newdata == NULL)
	    return 0;
	buf->data = newdata;
	buf->alloc = newalloc;
    }
    mch_memmove(buf->data + buf->len, ptr, realsize);
    buf->len += realsize;
    buf->data[buf->len] = NUL;
    return realsize;
}

/*
 * Parse raw header buffer into a Vim dict.
 * Header lines are "Name: Value\r\n".  The status line and empty lines are
 * skipped.
 */
    static dict_T *
curl_parse_headers(curl_buffer_T *hdrbuf)
{
    dict_T  *dict;
    char_u  *p;
    char_u  *end;

    dict = dict_alloc();
    if (dict == NULL)
	return NULL;

    p = hdrbuf->data;
    end = hdrbuf->data + hdrbuf->len;
    while (p < end)
    {
	char_u	*line_end;
	char_u	*colon;

	line_end = p;
	while (line_end < end && *line_end != '\r' && *line_end != '\n')
	    ++line_end;

	colon = vim_strchr(p, ':');
	if (colon != NULL && colon < line_end)
	{
	    char_u  *name;
	    char_u  *value;
	    char_u  *vp;

	    // Extract header name (lowercase for consistency).
	    name = vim_strnsave(p, colon - p);
	    if (name != NULL)
	    {
		char_u	*np;
		for (np = name; *np != NUL; ++np)
		    *np = TOLOWER_ASC(*np);

		// Skip ": " and trim leading whitespace from value.
		vp = colon + 1;
		while (vp < line_end && VIM_ISWHITE(*vp))
		    ++vp;
		value = vim_strnsave(vp, line_end - vp);
		if (value != NULL)
		{
		    dict_add_string(dict, (char *)name, value);
		    vim_free(value);
		}
		vim_free(name);
	    }
	}

	// Skip past \r\n.
	p = line_end;
	if (p < end && *p == '\r')
	    ++p;
	if (p < end && *p == '\n')
	    ++p;
    }
    return dict;
}

static int curl_initialized = FALSE;

/*
 * Ensure libcurl global state is initialized.
 */
    static void
curl_ensure_init(void)
{
    if (!curl_initialized)
    {
	curl_global_init(CURL_GLOBAL_DEFAULT);
	curl_initialized = TRUE;
    }
}

/*
 * Build a response dict from curl result.
 */
    static dict_T *
curl_build_response(CURL *curl, curl_buffer_T *body_buf,
						    curl_buffer_T *hdr_buf)
{
    dict_T	*result;
    dict_T	*hdr_dict;
    long	status_code = 0;

    result = dict_alloc();
    if (result == NULL)
	return NULL;

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    dict_add_number(result, "status", (varnumber_T)status_code);

    if (body_buf->data != NULL)
	dict_add_string(result, "body", body_buf->data);
    else
	dict_add_string(result, "body", (char_u *)"");

    hdr_dict = curl_parse_headers(hdr_buf);
    if (hdr_dict != NULL)
	dict_add_dict(result, "headers", hdr_dict);

    return result;
}

/*
 * Set up a CURL easy handle with common options from the options dict.
 * Returns the request headers slist (caller must free), or NULL.
 */
    static struct curl_slist *
curl_setup_easy(CURL *curl, char_u *url, dict_T *opts,
		curl_buffer_T *body_buf, curl_buffer_T *hdr_buf)
{
    char_u	    *method = (char_u *)"GET";
    long	    timeout = 30;
    struct curl_slist *req_headers = NULL;

    if (opts != NULL)
    {
	dictitem_T  *di;

	di = dict_find(opts, (char_u *)"method", -1);
	if (di != NULL)
	    method = tv_get_string(&di->di_tv);

	di = dict_find(opts, (char_u *)"timeout", -1);
	if (di != NULL)
	    timeout = (long)tv_get_number(&di->di_tv);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body_buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, hdr_buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Vim");

    // Set HTTP method.
    if (STRICMP(method, "POST") == 0)
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
    else if (STRICMP(method, "PUT") == 0)
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    else if (STRICMP(method, "DELETE") == 0)
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    else if (STRICMP(method, "PATCH") == 0)
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    else if (STRICMP(method, "HEAD") == 0)
    {
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "HEAD");
    }
    else if (STRICMP(method, "GET") != 0)
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);

    // Set request headers.
    if (opts != NULL)
    {
	dictitem_T  *di;

	di = dict_find(opts, (char_u *)"headers", -1);
	if (di != NULL && di->di_tv.v_type == VAR_DICT)
	{
	    dict_T	*hdr = di->di_tv.vval.v_dict;
	    hashitem_T	*hi;
	    int		todo;

	    if (hdr != NULL)
	    {
		todo = (int)hdr->dv_hashtab.ht_used;
		for (hi = hdr->dv_hashtab.ht_array; todo > 0; ++hi)
		{
		    if (!HASHITEM_EMPTY(hi))
		    {
			dictitem_T  *item = HI2DI(hi);
			char_u	    *val = tv_get_string(&item->di_tv);
			char_u	    *hdrline;
			size_t	    len;

			len = STRLEN(item->di_key) + STRLEN(val) + 3;
			hdrline = alloc(len);
			if (hdrline != NULL)
			{
			    vim_snprintf((char *)hdrline, len, "%s: %s",
					    item->di_key, val);
			    req_headers = curl_slist_append(req_headers,
						    (char *)hdrline);
			    vim_free(hdrline);
			}
			--todo;
		    }
		}
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_headers);
	    }
	}

	// Set request body.  Copy it so the caller's string can be freed.
	di = dict_find(opts, (char_u *)"body", -1);
	if (di != NULL)
	{
	    char_u  *body = tv_get_string(&di->di_tv);

	    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body);
	}
    }

    return req_headers;
}

/*
 * Free an async request and remove from the linked list.
 */
    static void
curl_async_free(curl_async_T *ca)
{
    if (ca->ca_prev != NULL)
	ca->ca_prev->ca_next = ca->ca_next;
    else
	first_curl_async = ca->ca_next;
    if (ca->ca_next != NULL)
	ca->ca_next->ca_prev = ca->ca_prev;

    if (curl_multi_handle != NULL)
	curl_multi_remove_handle(curl_multi_handle, ca->ca_curl);
    curl_easy_cleanup(ca->ca_curl);
    free_callback(&ca->ca_callback);
    if (ca->ca_req_headers != NULL)
	curl_slist_free_all(ca->ca_req_headers);
    vim_free(ca->ca_body.data);
    vim_free(ca->ca_headers.data);
    vim_free(ca);
}

/*
 * Process completed async HTTP requests.
 */
    static void
curl_async_process(void)
{
    CURLMsg	    *msg;
    int		    msgs_left;

    if (curl_multi_handle == NULL)
	return;

    while ((msg = curl_multi_info_read(curl_multi_handle, &msgs_left))
								    != NULL)
    {
	if (msg->msg == CURLMSG_DONE)
	{
	    CURL		*easy = msg->easy_handle;
	    curl_async_T	*ca;

	    // Find the async request.
	    for (ca = first_curl_async; ca != NULL; ca = ca->ca_next)
		if (ca->ca_curl == easy)
		    break;
	    if (ca == NULL)
		continue;

	    // Build response dict and invoke callback.
	    if (msg->data.result == CURLE_OK)
	    {
		dict_T	    *result;
		typval_T    argv[2];

		result = curl_build_response(easy, &ca->ca_body,
							    &ca->ca_headers);
		if (result != NULL)
		{
		    typval_T    rettv;

		    argv[0].v_type = VAR_DICT;
		    argv[0].vval.v_dict = result;
		    ++result->dv_refcount;
		    argv[1].v_type = VAR_UNKNOWN;

		    call_callback(&ca->ca_callback, -1, &rettv, 1, argv);
		    clear_tv(&rettv);
		    dict_unref(result);
		}
	    }
	    else
	    {
		// Error: invoke callback with error dict.
		dict_T	    *result = dict_alloc();

		if (result != NULL)
		{
		    typval_T    argv[2];
		    typval_T    rettv;

		    dict_add_number(result, "status", 0);
		    dict_add_string(result, "body", (char_u *)"");
		    dict_add_string(result, "error",
			(char_u *)curl_easy_strerror(msg->data.result));

		    argv[0].v_type = VAR_DICT;
		    argv[0].vval.v_dict = result;
		    ++result->dv_refcount;
		    argv[1].v_type = VAR_UNKNOWN;

		    call_callback(&ca->ca_callback, -1, &rettv, 1, argv);
		    clear_tv(&rettv);
		    dict_unref(result);
		}
	    }

	    curl_async_free(ca);
	}
    }
}

/*
 * "curl_request({url} [, {options}])" function
 *
 * Options dict:
 *   "method"	- HTTP method string (default: "GET")
 *   "headers"	- dict of request headers
 *   "body"	- request body string
 *   "timeout"	- timeout in seconds (default: 30)
 *   "callback" - if set, run async and invoke callback(response) on done
 *
 * Sync mode returns a dict: {"status": N, "headers": {...}, "body": "..."}
 * Async mode returns 0 (request submitted) or -1 (error).
 */
    void
f_curl_request(typval_T *argvars, typval_T *rettv)
{
    char_u	    *url;
    dict_T	    *opts = NULL;
    CURL	    *curl;
    struct curl_slist *req_headers;
    callback_T	    callback;
    int		    is_async = FALSE;

    CLEAR_FIELD(callback);

    if (in_vim9script()
	    && (check_for_string_arg(argvars, 0) == FAIL
		|| check_for_opt_dict_arg(argvars, 1) == FAIL))
	return;

    url = tv_get_string_chk(&argvars[0]);
    if (url == NULL)
	return;

    if (argvars[1].v_type == VAR_DICT)
	opts = argvars[1].vval.v_dict;
    else if (argvars[1].v_type != VAR_UNKNOWN)
    {
	semsg(_(e_invalid_argument_str), "opts");
	return;
    }

    // Check for async callback.
    if (opts != NULL)
    {
	dictitem_T  *di = dict_find(opts, (char_u *)"callback", -1);
	if (di != NULL)
	{
	    callback = get_callback(&di->di_tv);
	    if (callback.cb_name != NULL)
		is_async = TRUE;
	}
    }

    curl_ensure_init();

    curl = curl_easy_init();
    if (curl == NULL)
    {
	emsg(_(e_out_of_memory));
	return;
    }

    if (is_async)
    {
	// Async mode: add to curl_multi.
	curl_async_T	*ca;

	ca = ALLOC_CLEAR_ONE(curl_async_T);
	if (ca == NULL)
	{
	    curl_easy_cleanup(curl);
	    free_callback(&callback);
	    return;
	}

	ca->ca_curl = curl;
	set_callback(&ca->ca_callback, &callback);
	req_headers = curl_setup_easy(curl, url, opts,
						&ca->ca_body, &ca->ca_headers);
	ca->ca_req_headers = req_headers;

	// Initialize curl_multi if needed.
	if (curl_multi_handle == NULL)
	{
	    curl_multi_handle = curl_multi_init();
	    if (curl_multi_handle == NULL)
	    {
		curl_async_free(ca);
		emsg(_(e_out_of_memory));
		return;
	    }
	}

	curl_multi_add_handle(curl_multi_handle, curl);

	// Add to linked list.
	ca->ca_next = first_curl_async;
	ca->ca_prev = NULL;
	if (first_curl_async != NULL)
	    first_curl_async->ca_prev = ca;
	first_curl_async = ca;

	rettv->v_type = VAR_NUMBER;
	rettv->vval.v_number = 0;
    }
    else
    {
	// Sync mode: perform immediately.
	curl_buffer_T	body_buf;
	curl_buffer_T	hdr_buf;
	CURLcode	res;
	dict_T		*result;

	CLEAR_FIELD(body_buf);
	CLEAR_FIELD(hdr_buf);

	rettv->v_type = VAR_DICT;
	rettv->vval.v_dict = NULL;

	req_headers = curl_setup_easy(curl, url, opts, &body_buf, &hdr_buf);

	res = curl_easy_perform(curl);

	if (res != CURLE_OK)
	{
	    semsg(_(e_curl_request_failed_str),
					    curl_easy_strerror(res));
	}
	else
	{
	    result = curl_build_response(curl, &body_buf, &hdr_buf);
	    if (result != NULL)
	    {
		rettv->vval.v_dict = result;
		++result->dv_refcount;
	    }
	}

	if (req_headers != NULL)
	    curl_slist_free_all(req_headers);
	vim_free(body_buf.data);
	vim_free(hdr_buf.data);
	curl_easy_cleanup(curl);
    }
}

///////////////////////////////////////////////////////////////////////////
// Public helpers for the main event loop.
///////////////////////////////////////////////////////////////////////////

/*
 * Return TRUE if there are any active async HTTP connections.
 */
    int
curl_async_active(void)
{
    return first_curl_async != NULL;
}

/*
 * Drive curl_multi and process completed async HTTP requests.
 * Called from do_sleep() and other places that need to poll
 * outside the main event loop.
 */
    void
curl_check_all(void)
{
    if (curl_multi_handle != NULL && first_curl_async != NULL)
    {
	int	running;

	curl_multi_perform(curl_multi_handle, &running);
	curl_async_process();
    }
}

/*
 * Cleanup libcurl.  Called during Vim exit.
 */
    void
curl_lib_cleanup(void)
{
    // Free pending async requests.
    while (first_curl_async != NULL)
	curl_async_free(first_curl_async);

    if (curl_multi_handle != NULL)
    {
	curl_multi_cleanup(curl_multi_handle);
	curl_multi_handle = NULL;
    }

    if (curl_initialized)
    {
	curl_global_cleanup();
	curl_initialized = FALSE;
    }
}

#endif // FEAT_CURL
