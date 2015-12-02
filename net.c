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

#ifdef LINUX
	#include <bsd/stdlib.h>
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


#include "jstring.h"
#include "arraylist.h"
#include "macros.h"

#include "sws.h"
#include "net.h"
#include "http.h"
#include "cgi.h"

#define DEFAULT_BACKLOG 10
#define DEFAULT_BUFFSIZE 512

static void do_http(struct swsopt *, int, struct sockaddr *,
					struct sockaddr *, char *);
static void read_http_header(int, struct http_request *,
                             struct set_logging *);
static void get_ip(char *, struct sockaddr *);
static void send_file(int, struct http_request *, JSTRING *);
static void send_dirindex(int, JSTRING *, char *uri);
static void send_err_and_exit(int, int);

static int trim_uri(JSTRING *);
static void verify_port(char *);
static BOOL replace_userdir(JSTRING *);
static void separate_query(char *, JSTRING **, JSTRING **);
static BOOL is_dir(char *);
static BOOL contains_indexfile(JSTRING *);
static void write_socket(int, char *, size_t);
static int lexicographical_compare(const void *, const void *);
static void perror_exit(char *);

static struct set_logging logger;
static struct http_response h_res;

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
	int sfd, cfd, errcode;
	pid_t pid;
	char *server_port;
	struct addrinfo hint, *res;
	struct sockaddr_in ipv4_client;
	struct sockaddr_in6 ipv6_any, ipv6_client;
	struct sockaddr *server, *client;
	socklen_t server_len, client_len;
	
	
	/* set server socket port and verify */
	if (so->opt['p'] == TRUE) {
		verify_port(so->port);
		server_port = so->port;
	} else
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
		
		errcode = getaddrinfo(so->address, server_port, &hint, &res);
		if (errcode != 0) {
			fprintf(stderr, 
				"%s: get address information error: %s\n", 
				getprogname(),
				gai_strerror(errcode));
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
	if (sfd == -1)
		perror_exit("create socket error");
		
	if (bind(sfd, server, server_len) == -1)
		perror_exit("bind socket error");
	
	if (listen(sfd, DEFAULT_BACKLOG) == -1)
		perror_exit("listen socket error");
		
	
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
	
	/* If -d isn't set, run this server as a daemon process. */
	if (so->opt['d'] == FALSE)
		if (daemon(0, 0) != 0)
			perror_exit("daemonize error: ");
		
	/*
	 * This infinite loop makes server accept next connection
	 * after closing the last socket.
	 */
	for (;;) {
		if ((cfd = accept(sfd, client, &client_len)) == -1)
			perror_exit("accept socket error");
		
		
		/*
		 * To avoid zombie process, fork(2) will be called twice
		 * to create two children processes. The first child will
		 * exit immediately after the creation of second child.
		 * The second child will become the child of init process.
		 */
		if (so->opt['d'] == FALSE) {
			if ((pid = fork()) == -1)
				perror_exit("fork first child error: ");
			
			if (pid > 0) {
				close(cfd);
				(void)wait(NULL);
			} else {
				if ((pid = fork()) == -1)
					perror_exit("fork second child error: ");
				
				if (pid > 0) 
					_exit(EXIT_SUCCESS);
				else {
					do_http(so, cfd, client, server, server_port);
					close(cfd);
					_exit(EXIT_SUCCESS);
				}	
			}
		} else { // should be modified to use child process
			do_http(so, cfd, client, server, server_port);
			close(cfd);
		}
			
		
		
	}
	
	close(sfd);
	if (so->opt['i'] == TRUE)
		freeaddrinfo(res);

}

static void
do_http(struct swsopt *so, int cfd, 
		struct sockaddr *client,
		struct sockaddr *server,
		char *server_port)
{
	int cgi_result, trim_result;
	struct cgi_request cgi_req;
	/* ipv6 length is enough for both type */
	char server_ip[INET6_ADDRSTRLEN];
	char client_ip[INET6_ADDRSTRLEN];
	struct http_request hr;
    extern struct http_response h_res;
    extern struct set_logging logger;
	JSTRING *url, *query;
	
	get_ip(server_ip, server);
	get_ip(client_ip, client);
	
    logger.client_ip = client_ip;
    logger.fd = so->fd_logfile;
    logger.logging_flag = so->opt['l'];
	
	read_http_header(cfd, &hr, &logger);
    
	/* verify if http version is supported */
	if (hr.http_version > HTTP_IMPL_VERSION)
		send_err_and_exit(cfd, Not_Implemented);
	
	/* Check if the request method is not HEAD or GET */
	if (hr.method_type != HEAD &&
	    hr.method_type != GET)
		send_err_and_exit(cfd, Not_Implemented);
	
	/* separate url and query string */
	separate_query(hr.request_URL, &url, &query);
	
	/* trim uri and verify if the actual file exceeds CWD */
	trim_result = trim_uri(url);
	if (trim_result != 0)
		send_err_and_exit(cfd, trim_result);
	
	
    h_res.file_path = jstr_cstr(url);
    
	/* If -c is set and URL starts with /cgi-bin */
	if (so->opt['c'] == TRUE && is_cgi_call(url) == TRUE) {
		/* Initialize struct cgi_request */
		cgi_req.cfd = cfd;
		cgi_req.request_method = hr.method_type;
		cgi_req.cgi_dir = so->cgi_dir;
		cgi_req.server_ip = server_ip;
		cgi_req.server_port = server_port;
		cgi_req.client_ip = client_ip;
		cgi_req.path = url;
		cgi_req.query = query;
		
		cgi_result = call_cgi(&cgi_req, &h_res);
		
        logger.state_code = cgi_result;
        /* can't get Content-Length from cgi call */
        logger.content_length = 0;
        
        (void)logging(&logger);
	} else {
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
			send_file(cfd, &hr, url);
		} else if (is_dir(jstr_cstr(url)) == TRUE)
			send_dirindex(cfd, url, hr.request_URL);
		else
			send_file(cfd, &hr, url);
	}
	
	/* If If-Modified-Since is set, detect */
	jstr_free(url);
	jstr_free(query);
    
    clean_request(&hr);
    clean_logging(&logger);
}


static void
read_http_header(int cfd, struct http_request *phr, 
                 struct set_logging *logger)
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
	if (request(request_head, phr, logger) != 0)
		send_err_and_exit(cfd, Bad_Request);
	
}

static void
send_file(int cfd, struct http_request *hr, JSTRING *path)
{
	extern struct set_logging logger;
	extern struct http_response h_res;
    BOOL need_send;
	struct stat stat_buf;
	int fd;
	ssize_t read_count;
	char buf[DEFAULT_BUFFSIZE];
	char resp_buf[HTTP_RESPONSE_MAX_LENGTH];
    size_t size;
	
	if (stat(jstr_cstr(path), &stat_buf) == -1) {
		if (errno == ENOENT)
			send_err_and_exit(cfd, Not_Found);
		else
			send_err_and_exit(cfd, Internal_Server_Error);
	}
	
	if (!S_ISREG(stat_buf.st_mode))
		send_err_and_exit(cfd, Not_Found);
	
	/* 1 means has If-Modified-Since header */
	need_send = TRUE;
	if (hr->if_modified_flag == 1 &&
	    difftime(stat_buf.st_mtime, hr->if_modified_since) >= 0)
		need_send = FALSE;
	
	if (hr->method_type == HEAD) {
		/* send head response, without Content-Length */
        h_res.last_modified = time(NULL);
        h_res.content_length = 0;
        h_res.http_status = OK;
        h_res.body_flag = 0;
        
        size = 0;
        (void)response(&h_res, resp_buf, 
                       HTTP_RESPONSE_MAX_LENGTH, &size);
        
        
        write_socket(cfd, resp_buf, size);
		return;
	}
	
	if ((fd = open(jstr_cstr(path), O_RDONLY)) == -1)
		send_err_and_exit(cfd, Internal_Server_Error);
	
	/* prepare response head data */
	h_res.last_modified = stat_buf.st_mtime;
    h_res.body_flag = need_send;
    /* 
     * check if it needs to add Content-Length
     * header and sends message body 
     */
    if (need_send) {
		h_res.http_status = OK;
		h_res.content_length = stat_buf.st_size;
	} else {
		h_res.http_status = Not_Modified;
		h_res.content_length = 0;
	}
        
    
    /* send http response head */
    size = 0;
    (void)response(&h_res, resp_buf, 
                   HTTP_RESPONSE_MAX_LENGTH, &size);
    write_socket(cfd, resp_buf, size);
    
    
    /* send message body when needed */
	if (need_send) {
        while ((read_count = read(fd, buf, DEFAULT_BUFFSIZE)) > 0)
            write_socket(cfd, buf, read_count);
        if (read_count == -1)
            perror_exit("read file error: ");
    }
	(void)close(fd);
	
	/* log the response */
    logger.state_code = h_res.http_status;
    logger.content_length = h_res.content_length;
    
    (void)logging(&logger);
}

static void
send_dirindex(int cfd, JSTRING *path, char *uri)
{
	extern struct http_response h_res;
    extern struct set_logging logger;
    DIR *dp;
	struct dirent *dirp;
	size_t i, bodylen;
	ARRAYLIST *list;
	JSTRING *filename;
	char resp_buf[HTTP_RESPONSE_MAX_LENGTH];
    size_t size;
    
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
	
	/* send response head */
    h_res.last_modified = time(NULL);
    h_res.content_length = bodylen;
    h_res.http_status = OK;
    h_res.body_flag = 1;
    
    size = 0;
    (void)response(&h_res, resp_buf, 
                   HTTP_RESPONSE_MAX_LENGTH, &size);
    write_socket(cfd, resp_buf, size);
	
    /* send message body */
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
    
    /* log the response */
    logger.state_code = OK;
    logger.content_length = bodylen;
    
    (void)logging(&logger);
}


static void
send_err_and_exit(int cfd, int err_code)
{
    extern struct http_response h_res;
	extern struct set_logging logger;
    char resp_buf[HTTP_RESPONSE_MAX_LENGTH];
    size_t size;
    
    h_res.last_modified = time(NULL);
    h_res.http_status = err_code;
    /* 
	 * 4xx and 5xx should include Content-Length, but
	 * the value could be 0.
	 */
	h_res.content_length = 0;
	h_res.body_flag = 1; // 1 means including Content-Length
    
    size = 0;
    (void)response(&h_res, resp_buf, 
                   HTTP_RESPONSE_MAX_LENGTH, &size);
    
    write_socket(cfd, resp_buf, size);
    
	close(cfd);
    
    /* log to file */
    logger.state_code = err_code;
    logger.content_length = 0;
    
    /* return 0 when error happened */
    (void)logging(&logger);
        
    
	_exit(EXIT_FAILURE);
}


static void
get_ip(char *ip, struct sockaddr *addr) 
{
	void *in_addr;
	
	if (addr->sa_family == AF_INET)
		in_addr = 
			(void *)&((struct sockaddr_in *)addr)->sin_addr;
	else if (addr->sa_family == AF_INET6)
		in_addr = 
			(void *)&((struct sockaddr_in6 *)addr)->sin6_addr;
	
	(void)inet_ntop(addr->sa_family, in_addr,
					ip, INET6_ADDRSTRLEN);
}

static int 
trim_uri(JSTRING *uri)
{
	size_t i;
	JSTRING *temp, *last;
	ARRAYLIST *list;
	
	list = arrlist_create();
	
	/* uri must start with '/' */
	arrlist_add(list, jstr_create("/"));
	
	temp = jstr_create("");
	for (i = 1; i < jstr_length(uri); i++) {
		jstr_append(temp, jstr_charat(uri, i));
		if (jstr_charat(uri, i) == '/' || 
			i == jstr_length(uri) - 1) {
			if (jstr_equals(temp, "..") == 0 ||
				jstr_equals(temp, "../") == 0) {
				
				if (arrlist_size(list) > 1) {
					last = (JSTRING *)arrlist_remove(list, 
							arrlist_size(list) - 1);
					jstr_free(last);
				} else
					return Forbidden;
				
				jstr_free(temp);
			} else if (jstr_equals(temp, ".") == 0 ||
					   jstr_equals(temp, "./") == 0 ||
					   jstr_equals(temp, "/") == 0) {
				jstr_free(temp);
			} else {
				arrlist_add(list, temp);
			}
			temp = jstr_create("");
		}
	}
	jstr_free(temp);
	
	jstr_trunc(uri, jstr_length(uri), 0);
	for (i = 0; i < arrlist_size(list); i++) {
		temp = (JSTRING *)arrlist_get(list, i);
		jstr_concat(uri, jstr_cstr(temp));
		jstr_free(temp);
	}
		
	arrlist_free(list);
	
	return 0;
}

static void 
verify_port(char *port)
{
	size_t i;
	unsigned long int num;
	
	for (i = 0; i < strlen(port); i++)
		if (!isdigit((int)port[i])) {
			fprintf(stderr, 
				"%s: port must be a positive integer\n", 
				getprogname());
			exit(EXIT_FAILURE);
		}
	
	num = strtoul(port, NULL, 10);
	if (num == ULONG_MAX) {
		fprintf(stderr, 
			"%s: port is too large\n", 
			getprogname());
		exit(EXIT_FAILURE);
	}
	
	if (num <= 1023) {
		fprintf(stderr, 
			"%s: port must be greater or equal than 1024\n", 
			getprogname());
		exit(EXIT_FAILURE);
	}
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

/*
 * This function separate query string from the original
 * uri.
 */
static void
separate_query(char *uri, JSTRING **url, JSTRING **query)
{
	size_t i;
	JSTRING *juri;
	
	juri = jstr_create(uri);
	
	for (i = 0; i < jstr_length(juri); i++)
		if (jstr_charat(juri, i) == '?')
			break;
	if (i != jstr_length(juri)) {
		*url = jstr_substr(juri, 0, i);
		*query = jstr_substr(juri, i + 1, jstr_length(juri) - i - 1);
		jstr_free(juri);
	} else {
		*url = juri;
		*query = jstr_create("");
	}
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

static void
perror_exit(char *message)
{
	fprintf(stderr, "%s: ", getprogname());
	perror(message);
	exit(EXIT_FAILURE);
}