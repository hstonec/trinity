/*
 * This program contains the code to deal with
 * cgi request.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "jstring.h"
#include "macros.h"
#include "http.h"

#include "cgi.h"

static void alarm_handler(int);
static int separate_pathinfo(JSTRING *, JSTRING **, JSTRING **);
static BOOL is_regular_file(char *);
static JSTRING *get_parent(JSTRING *);

/* used by alarm_handler() to kill cgi process */
static pid_t cgi_pid;
static void convert_request_method(int , char *);
static void write_socket(int, char *, size_t);

/*
 * Ths main function to be used to handle CGI
 * request. It checks the argument of CGI request
 * and invoke the specific CGI program and send
 * part of the http response header. 
 */
int
call_cgi(struct cgi_request *cgi_req,
         struct http_response *h_res)
{
	extern pid_t cgi_pid;
	pid_t pid;
	char *env_list[11];
	char request_method[5];
    char resp_buf[HTTP_RESPONSE_MAX_LENGTH];
    size_t size;
	int sep_result;
    JSTRING *abs_path, *path_info;
	
	JSTRING *cwd;
	JSTRING *mv_GATEWAY_INTERFACE;
	JSTRING *mv_PATH_INFO;
	JSTRING *mv_QUERY_STRING;
	JSTRING *mv_REMOTE_ADDR;
	JSTRING *mv_REQUEST_METHOD;
	JSTRING *mv_SCRIPT_NAME;
	JSTRING *mv_SERVER_NAME;
	JSTRING *mv_SERVER_PORT;
	JSTRING *mv_SERVER_PROTOCOL;
	JSTRING *mv_SERVER_SOFTWARE;
	/* 
	 * remove /cgi-bin from the uri, then separate
	 * PATH_INFO and the original uri, and
	 * convert it to absolute path according
	 * to cgi_dir.
	 */
	jstr_trunc(cgi_req->uri, 8, jstr_length(cgi_req->uri) - 8);
	
	sep_result = separate_pathinfo(cgi_req->uri, &abs_path, &path_info);
	if (sep_result != 0)
		return sep_result;
	
	jstr_insert(abs_path, 0, jstr_cstr(cgi_req->cgi_dir));
	
	/* check if file is a regular file */
	if (is_regular_file(jstr_cstr(abs_path)) == FALSE)
		return Not_Found;
	
	/* check if file is executable */
	if (access(jstr_cstr(abs_path), X_OK)) {
		if (errno == ENOENT)
			return Not_Found;
		else
			return Internal_Server_Error;
	}
		
	if ((pid = fork()) == -1)
		return Internal_Server_Error;
	
	if (pid > 0) {
		cgi_pid = pid;
		if (signal(SIGALRM, &alarm_handler) == SIG_ERR)
			perror("signal error: ");
		/* set an alarm to avoid cgi program executing too long */
		(void)alarm(MAX_CGI_EXEC_TIME);
		(void)wait(NULL);
	} else {
		mv_GATEWAY_INTERFACE = 
			jstr_create("GATEWAY_INTERFACE=CGI/1.1");
		env_list[0] = jstr_cstr(mv_GATEWAY_INTERFACE);
		
		mv_QUERY_STRING = jstr_create("QUERY_STRING=");
		jstr_concat(mv_QUERY_STRING, jstr_cstr(cgi_req->query));
		env_list[1] = jstr_cstr(mv_QUERY_STRING);
		
		mv_REMOTE_ADDR = jstr_create("REMOTE_ADDR=");
		jstr_concat(mv_REMOTE_ADDR, cgi_req->client_ip);
		env_list[2] = jstr_cstr(mv_REMOTE_ADDR);
		
		mv_REQUEST_METHOD = jstr_create("REQUEST_METHOD=");
		convert_request_method(cgi_req->request_method, request_method);
		jstr_concat(mv_REQUEST_METHOD, request_method);
		env_list[3] = jstr_cstr(mv_REQUEST_METHOD);
		
		mv_SCRIPT_NAME = jstr_create("SCRIPT_NAME=");
		jstr_concat(mv_SCRIPT_NAME, jstr_cstr(abs_path));
		env_list[4] = jstr_cstr(mv_SCRIPT_NAME);
		
		mv_SERVER_NAME = jstr_create("SERVER_NAME=");
		jstr_concat(mv_SERVER_NAME, cgi_req->server_ip);
		env_list[5] = jstr_cstr(mv_SERVER_NAME);
		
		mv_SERVER_PORT = jstr_create("SERVER_PORT=");
		jstr_concat(mv_SERVER_PORT, cgi_req->server_port);
		env_list[6] = jstr_cstr(mv_SERVER_PORT);
		
		mv_SERVER_PROTOCOL = jstr_create("SERVER_PROTOCOL=" HTTP_VERSION);
		env_list[7] = jstr_cstr(mv_SERVER_PROTOCOL);
		
		mv_SERVER_SOFTWARE = jstr_create("SERVER_SOFTWARE=" HTTP_SERVER_NAME);
		env_list[8] = jstr_cstr(mv_SERVER_SOFTWARE);
		
		if (jstr_length(path_info) == 0)
			env_list[9] = NULL;
		else {
			mv_PATH_INFO = jstr_create("PATH_INFO=");
			jstr_concat(mv_PATH_INFO, jstr_cstr(path_info));
			env_list[9] = jstr_cstr(mv_PATH_INFO);
		}
		
		env_list[10] = NULL;
		
		/* send http response header here */
        h_res->last_modified = time(NULL);
        h_res->http_status = OK;
        h_res->body_flag = 0;

        size = 0;
        (void)cgi_response(h_res, resp_buf, 
                       HTTP_RESPONSE_MAX_LENGTH, &size);
        write_socket(cgi_req->cfd, resp_buf, size);
        
        
		if (dup2(cgi_req->cfd, STDOUT_FILENO) == -1)
			perror("dup2 error: ");
	
		(void)close(cgi_req->cfd);
		
		/* 
		 * explicitly set the current working directory to
		 * make sure exec will get correct PWD
		 */
		cwd = get_parent(abs_path);
		if (chdir(jstr_cstr(cwd)) == -1)
			perror("chdir error: ");
		
		if (execle(jstr_cstr(abs_path), 
		    jstr_cstr(abs_path), NULL,
			env_list) == -1)
			perror("exec error: ");
		
		_exit(EXIT_SUCCESS);
	}
	
	jstr_free(abs_path);
	jstr_free(path_info);

	return OK;
}

/*
 * This function verify whether the url is a CGI call.
 */
BOOL
is_cgi_call(JSTRING *url)
{
	/* url length must be greater than the length of "/cgi-bin" */
	if (jstr_length(url) < 8)
		return FALSE;
	
	if (strncmp(jstr_cstr(url), "/cgi-bin", 8) != 0)
		return FALSE;

	if (jstr_length(url) > 8 && jstr_charat(url, 8) != '/')
		return FALSE;
	
	return TRUE;
}

/*
 * This function separate the CGI program path and PATH_INFO
 * used by that program. If the uri doesn't include a PATH_INFO
 * the path_info variable will be set "".
 */
static int
separate_pathinfo(JSTRING *uri, JSTRING **abs_path, JSTRING **path_info)
{
	size_t index;
	char *ext, *occur;
	
	ext = ".cgi";
	
	index = 0;
	for (;;) {
		occur = strstr(jstr_cstr(uri) + index, ext);
		if (occur == NULL)
			return Not_Found;
		index = occur - jstr_cstr(uri) + 4;
		if (index == jstr_length(uri) || jstr_charat(uri, index) == '/')
			break;
	}
	
	if (index == jstr_length(uri)) {
		*abs_path = jstr_create(jstr_cstr(uri));
		*path_info = jstr_create("");
	} else {
		*abs_path = jstr_substr(uri, 0, index);
		*path_info = jstr_substr(uri, index, jstr_length(uri) - index);
	}

	return 0;
}

static BOOL
is_regular_file(char *path)
{
	struct stat buf;
	
	if (stat(path, &buf) != -1 && 
	    S_ISREG(buf.st_mode))
		return TRUE;
	else
		return FALSE;
}

static void 
alarm_handler(int signum)
{
	extern pid_t cgi_pid;

	if (kill(cgi_pid, SIGKILL) == -1)
		perror("kill error: ");
}

static JSTRING *
get_parent(JSTRING *path)
{
	size_t i;
	
	i = jstr_length(path) - 1;
	if (jstr_charat(path, jstr_length(path) - 1) == '/')
		i = jstr_length(path) - 2;
	
	for (; i >= 0; i--)
		if (jstr_charat(path, i) == '/')
			break;
	
	if (i == 0)
		return jstr_create("/");
	else
		return jstr_substr(path, 0, i);
}

static void
convert_request_method(int method, char *request_method)
{
	if (method == GET)
		strcpy(request_method, "GET");
	else if (method == POST)
		strcpy(request_method, "POST");
	else if (method == HEAD)
		strcpy(request_method, "HEAD");
	else
		strcpy(request_method, "");
}

static void
write_socket(int sfd, char *buf, size_t len)
{
	ssize_t count;
		
	count = 0;
	while ((count = write(sfd, buf, len)) > 0) {
		if (count < len) {
			len -= count;
			buf += count;
		} else
			break;
	}
	if (count == -1)
		perror("write socket error: ");
}
