/* if_curl.c */
int curl_enabled(int verbose);
void f_curl_request(typval_T *argvars, typval_T *rettv);
int curl_async_active(void);
void curl_check_all(void);
void curl_lib_cleanup(void);
