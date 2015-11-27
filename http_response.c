#include <arpa/inet.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "http_response.h"

int
response(struct http_response *response_info, char *resp_buf, int capacity, int *size)
{
	char buf[capacity];
	char timestr[64];
	char lastmodstr[64];
	char entity_html[capacity];
	char len[32];
	time_t present;

	memset(entity_html, 0, sizeof(entity_html));
	present = time(NULL);
	strftime(timestr, sizeof(timestr), rfc1123_DATE, gmtime(&present));

	strftime(lastmodstr, sizeof(lastmodstr), rfc1123_DATE, gmtime(&response_info->last_modified));

	if (response_info->http_status == OK ||
		response_info->http_status == Not_Modified) {
		sprintf(buf,
			"HTTP/%s %d %s\r\n"
			"Date: %s\r\n"
			"Server: SWS\r\n"
			"Last-Modified: %s\r\n"
			"Content-Type: %s\r\n"
			"Content-Length: %lu\r\n",
			"1.0", response_info->http_status, status_phrase(response_info->http_status),
			timestr,
			lastmodstr,
			"text/html",
			//response_info->content_type,
			response_info->content_length);
		if (response_info->keep_alive == 1) {
			sprintf(len, "Connection: %s\r\n\r\n", "keep_alive");
			strncat(buf, len, strlen(len));
		} else if (response_info->keep_alive == 0) {
			sprintf(len, "Connection: %s\r\n\r\n", "closed");
			strncat(buf, len, strlen(len));
		}
	} else {
		sprintf(entity_html, "<html><h1>%d %s</h1></html>", response_info->http_status, status_phrase(response_info->http_status));
		sprintf(buf,
			"HTTP/%s %d %s\r\n"
			"Date: %s\r\n"
			"Server: SWS\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: %lu\r\n"
			"Connection: closed\r\n\r\n",
			"1.0", response_info->http_status, status_phrase(response_info->http_status),
			timestr,
			(unsigned long)strlen(entity_html));
		response_info->content_length = (unsigned long)strlen(entity_html);
	}
	for (int i = 0; i < strlen(buf); i ++) {
		resp_buf[i] = buf[i];
	}
	*size = strlen(buf);
	return 0;
}

char*
status_phrase(int code) {
	switch(code) {
		case 200:
			return "OK";
			break;
		case 201:
			return "Created";
			break;
		case 202:
			return "Accepted";
			break;
		case 204:
			return "No Content";
			break;
		case 301:
			return "Moved Permanently";
			break;
		case 302:
			return "Found";
			break;
		case 304:
			return "Not Modified";
			break;
		case 400:
			return "Bad Request";
			break;
		case 401:
			return "Unauthorized";
			break;
		case 403:
			return "Forbidden";
			break;
		case 404:
			return "Not Found";
			break;
		case 500:
			return "Internal Server Error";
			break;
		case 501:
			return "Not Implemented";
			break;
		case 502:
			return "Bad Gateway";
			break;
		case 503:
			return "Service Unavailable";
			break;
		default:
			return "UNRECOGNIZED CODE";
			break;
	}
}
