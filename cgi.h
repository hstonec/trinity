#ifndef _CGI_H_
#define _CGI_H_

#define MAX_CGI_EXEC_TIME 60

struct cgi_request {
	int cfd;
	int request_method;
	char *client_ip;
	char *server_ip;
	char *server_port;
	JSTRING *cgi_dir;
	JSTRING *uri;
	JSTRING *query;
};

/* return 0 when succeed, or return http error status code */
int call_cgi(struct cgi_request *, struct http_response *);
BOOL is_cgi_call(JSTRING *);

#endif /* !_CGI_H_ */
