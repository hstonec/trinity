/*
 * This program contains all network related 
 * functions.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <bsd/stdlib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jstring.h"
#include "macros.h"
#include "sws.h"
#include "net.h"

#define DEFAULT_BACKLOG 10
#define DEFAULT_BUFFSIZE 512

void do_http(int, struct sockaddr *);
void println(int, char *);

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
					do_http(cfd, client);
					_exit(EXIT_SUCCESS);
				}	
			}
		} else
			do_http(cfd, client);
		
		
	}
	
	close(sfd);
	if (so->opt['i'] == TRUE)
		freeaddrinfo(res);

}

void
do_http(int cfd, struct sockaddr *client)
{
	/* ipv6 length is enough for both type */
	char client_ip[INET6_ADDRSTRLEN];
	void *client_in_addr;
	
	if (client->sa_family == AF_INET)
		client_in_addr = 
			(void *)&((struct sockaddr_in *)client)->sin_addr;
	else if (client->sa_family == AF_INET6)
		client_in_addr = 
			(void *)&((struct sockaddr_in6 *)client)->sin6_addr;
	(void)inet_ntop(client->sa_family, client_in_addr,
				  client_ip, INET6_ADDRSTRLEN);
	
	println(cfd, client_ip);
	
	close(cfd);
}

/*
 * This function reads one line of text from passed file descriptor
 * and prints out on standard output with the passed ip address.
 */
void
println(int sfd, char *ip_addr)
{
	ssize_t i, count;
	char buf[DEFAULT_BUFFSIZE];
	BOOL eol; /* end of line flag */
	
	eol = FALSE;
	(void)fprintf(stdout, "%s: ", ip_addr);
	while ((count = read(sfd, buf, DEFAULT_BUFFSIZE)) > 0) {
		for (i = 0; i < count; i++) {
			(void)fprintf(stdout, "%c", buf[i]);
			if (buf[i] == '\n') {
				eol = TRUE;
				break;
			}
		}
		if (eol)
			break;
	}
	
	if (count == -1) {
		perror("socket read error");
		exit(EXIT_FAILURE);
	}
}