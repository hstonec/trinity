/*
 * This program contains all network related 
 * functions.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <bsd/stdlib.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "jstring.h"
#include "macros.h"
#include "sws.h"
#include "net.h"
#include "http.h"
#include "arraylist.h"

#define DEFAULT_BACKLOG 10
#define DEFAULT_BUFFSIZE 512
#define HTTP_REQUEST_MAX_LENGTH 8192

static void do_http(struct swsopt *, int, struct sockaddr *);
static void read_http_header(int, struct http_request *);
static void call_cgi(int, JSTRING *);
static void get_client_ip(char *, struct sockaddr *);
static void send_file(int, JSTRING *);
static void send_dirindex(int, JSTRING *, char *uri);
static void send_err_and_exit(int, int);

static BOOL is_cgi_call(JSTRING *);
static BOOL replace_userdir(JSTRING *);
static BOOL is_dir(char *);
static BOOL contains_indexfile(JSTRING *);
static void write_socket(int, char *, size_t);
static int lexicographical_compare(const void *, const void *);


/*
 * This function creates a server socket and binds
 * it to the address and port provided in struct
 * swsopt passed by the argument.
 *
 * The server serves one client each time. The server
 * reads one line of text from the client and prints
 * out to stdout, then close this connection and waits
 * for the next client.
 */
void
start_server(struct swsopt *so)
{
	int sfd, cfd, err;
	pid_t pid;
	char *server_port;
	struct addrinfo hint, *res;
	struct sockaddr_in ipv4_client;
	struct sockaddr_in6 ipv6_any, ipv6_client;
	struct sockaddr *server, *client;
	socklen_t server_len, client_len;
	
	
	/* set server socket port */
	if (so->opt['p'] == TRUE)
		server_port = so->port;
	else
		server_port = "8080";
	
	/* 
	 * If -i address is set, try to get address information 
	 * through getaddrinfo(3); or, the server will listen on
	 * all available ipv4 and ipv6 addresses.
	 */
	if (so->opt['i'] == TRUE) {
		memset(&hint, 0, sizeof(struct addrinfo));
		hint.ai_family = AF_UNSPEC;
		hint.ai_socktype = SOCK_STREAM;
		hint.ai_flags = AI_NUMERICHOST;
		
		err = getaddrinfo(so->address, server_port, &hint, &res);
		if (err != 0) {
			(void)fprintf(stderr, 
				"%s: get address information error: %s\n", 
				getprogname(),
				gai_strerror(err));
			exit(EXIT_FAILURE);
		}
		
		server = res->ai_addr;
		server_len = res->ai_addrlen;
	} else {
		memset(&ipv6_any, 0, sizeof(struct sockaddr_in6));
		ipv6_any.sin6_family = AF_INET6;
		ipv6_any.sin6_port = htons(atoi(server_port));
		ipv6_any.sin6_addr = in6addr_any;
		
		server = (struct sockaddr *)&ipv6_any;
		server_len = sizeof(struct sockaddr_in6);
	}
	
	
	/* 
	 * create TCP/IP socket, bind to proper address
	 * then start to listening
	 */
	sfd = socket(server->sa_family, SOCK_STREAM, 0);
	if (sfd == -1) {
		perror("create socket error");
		exit(EXIT_FAILURE);
	}
	if (bind(sfd, server, server_len) == -1) {
		perror("bind socket error");
		exit(EXIT_FAILURE);
	}
	if (listen(sfd, DEFAULT_BACKLOG) == -1) {
		perror("listen socket error");
		exit(EXIT_FAILURE);
	}
	
	/* 
	 * Since ipv4 and ipv6 have different structure,
	 * it's safe to select longer structure as the
	 * client socket address.
	 */
	if (sizeof(struct sockaddr_in6) > sizeof(struct sockaddr_in)) {
		client = (struct sockaddr *)&ipv6_client;
		client_len = sizeof(struct sockaddr_in6);
	} else {
		client = (struct sockaddr *)&ipv4_client;
		client_len = sizeof(struct sockaddr_in);
	}
	
	/* 
	 * If -d isn't set, run this server as a daemon process.
	 */
	
	
	if (so->opt['d'] == FALSE)
		if (daemon(0, 0) != 0) {
			(void)fprintf(stderr,
				"%s: daemonize error: %s",
				getprogname(),
				strerror(errno));
			exit(EXIT_FAILURE);
		}
	

	/*
	 * This infinite loop makes server accept next connection
	 * after closing the last socket.
	 */
	for (;;) {
		if ((cfd = accept(sfd, client, &client_len)) == -1) {
			perror("accept socket error");
			exit(EXIT_FAILURE);
		}
		
		/*
		 * To avoid zombie process, fork(2) will be called twice
		 * to create two children processes. The first child will
		 * exit immediately after the creation of second child.
		 * The second child will become the child of init process.
		 */
		if (so->opt['d'] == FALSE) {
			if ((pid = fork()) == -1) {
				(void)fprintf(stderr,
					"%s: fork error: %s",
					getprogname(),
					strerror(errno));
				exit(EXIT_FAILURE);
			}
			
			if (pid > 0) {
				close(cfd);
				(void)wait(NULL);
			} else {
				if ((pid = fork()) == -1) {
					(void)fprintf(stderr,
						"%s: fork error: %s",
						getprogname(),
						strerror(errno));
					_exit(EXIT_FAILURE);
				}
				if (pid > 0) 
					_exit(EXIT_SUCCESS);
				else {
					do_http(so, cfd, client);
					_exit(EXIT_SUCCESS);
				}	
			}
		} else // should be modified to use child process
			do_http(so, cfd, client);
		
		
	}
	
	close(sfd);
	if (so->opt['i'] == TRUE)
		freeaddrinfo(res);

}

static void
do_http(struct swsopt *so, int cfd, struct sockaddr *client)
{
	/* ipv6 length is enough for both type */
	char client_ip[INET6_ADDRSTRLEN];
	struct http_request hr;
	JSTRING *url;
	
	get_client_ip(client_ip, client);
	
	read_http_header(cfd, &hr);
	
	/* verify http version */
	
	/* should verify path like ../../../.. ? */
	url = jstr_create(hr.request_URL);
	
	/* If -c is set and URL starts with /cgi-bin */
	if (so->opt['c'] == TRUE && is_cgi_call(url) == TRUE)
		call_cgi(cfd, url);
	else {
		/* 
		 * If url doesn't start with /~<user> and is a 
		 * relative path, it should be concatenated with
		 * current working directory.
		 */
		if (replace_userdir(url) == FALSE)
			jstr_insert(url, 0, jstr_cstr(so->content_dir));
		
		/*
		 * If url denotes a directory and contains index.html,
		 * the index.html file will be sent as response; If it
		 * is a directory but doesn't contain index.html, the
		 * directory index will be generated and sent to client.
		 */
		if (is_dir(jstr_cstr(url)) == TRUE &&
			contains_indexfile(url) == TRUE) {
			if (jstr_charat(url, jstr_length(url) - 1) != '/')
				jstr_append(url, '/');
			jstr_concat(url, "index.html");
			send_file(cfd, url);
		} else if (is_dir(jstr_cstr(url)) == TRUE)
			send_dirindex(cfd, url, hr.request_URL);
		else
			send_file(cfd, url);
	}
	
	/* If If-Modified-Since is set, detect */
	
	close(cfd);
}


static void
read_http_header(int cfd, struct http_request *phr)
{
	ssize_t i, count;
	char buf[DEFAULT_BUFFSIZE];
	char request_head[HTTP_REQUEST_MAX_LENGTH];
	ssize_t request_len;
	BOOL end_of_request;
	int offset;
	char *end_flag;
	
	offset = 0;
	request_len = 0;
	end_flag = "\r\n\r\n";
	end_of_request = FALSE;
	
	while ((count = read(cfd, buf, DEFAULT_BUFFSIZE)) > 0) {
		for (i = 0; i < count; i++, request_len++) {
			request_head[request_len] = buf[i];
			
			/* verify if it's the end of request */
			offset = buf[i] == end_flag[offset] ? offset + 1 : 0;
			if (offset == 4) {
				end_of_request = TRUE;
				break;
			}
			
			/* verify length */
			if (request_len == HTTP_REQUEST_MAX_LENGTH - 1)
				send_err_and_exit(cfd, Bad_Request);
				
		}
		if (end_of_request == TRUE)
			break;
	}
	if (count == -1)
		send_err_and_exit(cfd, Internal_Server_Error);
	
	if (end_of_request == FALSE)
		send_err_and_exit(cfd, Bad_Request);
	
	/* request() return 0 means success */
	if (request(request_head, phr) != 0)
		send_err_and_exit(cfd, Bad_Request);
	
}


static void
call_cgi(int cfd, JSTRING *path)
{
	ssize_t count, total;
	char *buf;
	
	jstr_insert(path, 0, "call cgi at: ");
	buf = jstr_cstr(path);
	total = jstr_length(path);
	
	count = 0;
	while ((count = write(cfd, buf, total)) > 0) {
		total -= count;
		buf += count;
	}
}

static void
send_file(int cfd, JSTRING *path)
{
	ssize_t count, total;
	char *buf;
	
	jstr_insert(path, 0, "should send file: ");
	buf = jstr_cstr(path);
	total = jstr_length(path);
	
	count = 0;
	while ((count = write(cfd, buf, total)) > 0) {
		total -= count;
		buf += count;
	}
}

static void
send_dirindex(int cfd, JSTRING *path, char *uri)
{
	DIR *dp;
	struct dirent *dirp;
	size_t i, bodylen;
	ARRAYLIST *list;
	JSTRING *filename;
	char *tag_before_title;
	char *tag_before_h1;
	char *tag_before_li;
	char *tag_left_li;
	char *tag_middle_li;
	char *tag_right_li;
	char *tag_after_li;
	
	size_t tag_before_title_len;
	size_t tag_before_h1_len;
	size_t tag_before_li_len;
	size_t tag_left_li_len;
	size_t tag_middle_li_len;
	size_t tag_right_li_len;
	size_t tag_after_li_len;
	
	size_t uri_len;
	
	
	list = arrlist_create();
	
	/* 
	 * Denotes the http response body length to this 
	 * request.
	 */
	bodylen = 0;
	
	if ((dp = opendir(jstr_cstr(path))) == NULL )
		send_err_and_exit(cfd, Internal_Server_Error);
	
	while ((dirp = readdir(dp)) != NULL ) {
		/*  Files starting with a '.' are ignored. */
		if (dirp->d_name[0] == '.')
			continue;
		
		filename = jstr_create(dirp->d_name);
		bodylen += jstr_length(filename);
		arrlist_add(list, filename);
	}
	(void)closedir(dp);
	
	/* sort the list in alphanumeric order */
	arrlist_sort(list, &lexicographical_compare);
	
	tag_before_title = \
		"<!DOCTYPE html>\n" \
		"<html>\n" \
		"\t<head><title>Index of ";
	tag_before_h1 = \
		"</title></head>\n" \
		"\t<body>\n" \
		"\t\t<h1>Index of ";
	tag_before_li = \
		"</h1>\n" \
		"\t\t<ul>\n";
	tag_left_li = "\t\t\t<li><a href=\"";
	tag_middle_li = "\"> ";
	tag_right_li = "</a></li>\n";
	tag_after_li = "\t\t</ul>\n\t</body>\n</html>";
	
	tag_before_title_len = strlen(tag_before_title);
	tag_before_h1_len = strlen(tag_before_h1);
	tag_before_li_len = strlen(tag_before_li);
	tag_left_li_len = strlen(tag_left_li);
	tag_middle_li_len = strlen(tag_middle_li);
	tag_right_li_len = strlen(tag_right_li);
	tag_after_li_len = strlen(tag_after_li);
	
	uri_len = strlen(uri);
	
	/* each uri will be wrote twice */
	bodylen *= 2;
	
	bodylen += uri_len * 2;
	
	bodylen += tag_before_title_len +
			   tag_before_h1_len +
			   tag_before_li_len +
			   tag_left_li_len * arrlist_size(list) +
			   tag_middle_li_len * arrlist_size(list) +
			   tag_right_li_len * arrlist_size(list) +
			   tag_after_li_len;
	
	// send response head here
	
	write_socket(cfd, tag_before_title, tag_before_title_len);
	write_socket(cfd, uri, uri_len);
	write_socket(cfd, tag_before_h1, tag_before_h1_len);
	write_socket(cfd, uri, uri_len);
	write_socket(cfd, tag_before_li, tag_before_li_len);
	for (i = 0; i < arrlist_size(list); i++) {
		filename = (JSTRING *)arrlist_get(list, i);
		
		write_socket(cfd, tag_left_li, tag_left_li_len);
		write_socket(cfd, jstr_cstr(filename), jstr_length(filename));
		write_socket(cfd, tag_middle_li, tag_middle_li_len);
		write_socket(cfd, jstr_cstr(filename), jstr_length(filename));
		write_socket(cfd, tag_right_li, tag_right_li_len);
		
		jstr_free(filename);
	}
	write_socket(cfd, tag_after_li, tag_after_li_len);
	
	arrlist_free(list);
}


static void
send_err_and_exit(int cfd, int err_code)
{
	// get response message according to error code
	JSTRING *path;
	ssize_t count, total;
	char *buf;
	
	path = jstr_create("");
	
	if (err_code == Bad_Request)
		jstr_insert(path, 0, "400 ");
	else if (err_code == Not_Found)
		jstr_insert(path, 0, "404 ");
	else if (err_code == Internal_Server_Error)
		jstr_insert(path, 0, "500 ");
	else
		jstr_insert(path, 0, "Unknown ");
	
	jstr_insert(path, 0, "err num: ");
	buf = jstr_cstr(path);
	total = jstr_length(path);
	
	count = 0;
	while ((count = write(cfd, buf, total)) > 0) {
		total -= count;
		buf += count;
	}
	
	close(cfd);
	_exit(EXIT_FAILURE);
}


static void
get_client_ip(char *client_ip, struct sockaddr *client) 
{
	void *client_in_addr;
	
	if (client->sa_family == AF_INET)
		client_in_addr = 
			(void *)&((struct sockaddr_in *)client)->sin_addr;
	else if (client->sa_family == AF_INET6)
		client_in_addr = 
			(void *)&((struct sockaddr_in6 *)client)->sin6_addr;
	
	(void)inet_ntop(client->sa_family, client_in_addr,
				  client_ip, INET6_ADDRSTRLEN);
}

/*
 * This function verify whether the url is a CGI call.
 */
static BOOL
is_cgi_call(JSTRING *url)
{
	/* url length must be greater than the length of "/cgi-bin" */
	if (jstr_length(url) < 8)
		return FALSE;
	
	if (strncmp(jstr_cstr(url), "/cgi-bin", 8) != 0)
		return FALSE;

	return TRUE;
}

/*
 * This function replace /~<user> with /home/<user>/sws/,
 * and return TRUE if replacement successes, or return 
 * FALSE if the path doesn't need to be replaced.
 */
static BOOL
replace_userdir(JSTRING *path)
{
	size_t i;
	JSTRING *user;
	
	if (jstr_length(path) <= 2)
		return FALSE;
	
	if (strncmp(jstr_cstr(path), "/~/", 3) == 0)
		return FALSE;
	
	if (strncmp(jstr_cstr(path), "/~", 2) != 0)
		return FALSE;
	
	user = jstr_create("/home/");
	for (i = 2; 
		 jstr_charat(path, i) != '/' && i < jstr_length(path);
		 i++)
		jstr_append(user, jstr_charat(path, i));
	jstr_concat(user, "/sws");
	if (i >= jstr_length(path) || jstr_charat(path, i) != '/')
		jstr_append(user, '/');
	
	jstr_trunc(path, i, jstr_length(path) - i);
	jstr_insert(path, 0, jstr_cstr(user));
	
	jstr_free(user);
	return TRUE;
}

static BOOL
is_dir(char *path)
{
	struct stat buf;
	
	if (stat(path, &buf) != -1 && 
	    S_ISDIR(buf.st_mode))
		return TRUE;
	else
		return FALSE;
}

static BOOL
contains_indexfile(JSTRING *path)
{
	JSTRING *temp;
	struct stat buf;
	BOOL flag;
	
	flag = FALSE;
	temp = jstr_create(jstr_cstr(path));
	if (jstr_charat(temp, jstr_length(temp) - 1) != '/')
		jstr_append(temp, '/');
	jstr_concat(temp, "index.html");
	
	if (stat(jstr_cstr(temp), &buf) != -1 &&
		S_ISREG(buf.st_mode))
		flag = TRUE;	
	
	jstr_free(temp);
	
	return flag;
}

static void
write_socket(int sfd, char *buf, size_t len)
{
	ssize_t count;
		
	count = 0;
	while ((count = write(sfd, buf, len)) > 0) {
		len -= count;
		buf += count;
	}
	if (count == -1)
		send_err_and_exit(sfd, Internal_Server_Error);
}

static int 
lexicographical_compare(const void *p1, const void *p2)
{
	JSTRING *fp1, *fp2; 
	
	fp1 = *(JSTRING * const *)p1;
	fp2 = *(JSTRING * const *)p2;
	
	return strcmp(jstr_cstr(fp1), jstr_cstr(fp2));
}