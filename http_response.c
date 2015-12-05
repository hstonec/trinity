#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "http.h"

#define rfc1123_DATE_STR "%a, %d %b %Y %T GMT"

char* status_phrase(int code);
char* get_content_type(char* file_path);

/* This function processes http response header fields.
 * This function will process http_response structure from net.c
 * and generate string buffer, return back to net.c as a header.
 */
int
response(struct http_response *response_info, char *resp_buf, size_t capacity, size_t *size)
{
	char buf[capacity];
	char timestr[64], lastmodstr[64];
	time_t present;
	char len[32];
	int i = 0;

	/* process the current time and last modified time */
	time(&present);
	strftime(timestr, sizeof(timestr), rfc1123_DATE_STR, gmtime(&present));
	strftime(lastmodstr, sizeof(lastmodstr), rfc1123_DATE_STR, gmtime(&response_info->last_modified));

	if (response_info->http_status == Not_Modified ||
		response_info->http_status == No_Content) {
		/* 304 and 204 header, without content type and length */
		sprintf(buf,
			"%s %d %s\r\n"
			"Date: %s\r\n"
			"Server: %s\r\n",
			HTTP_VERSION, response_info->http_status, status_phrase(response_info->http_status),
			timestr,
			HTTP_SERVER_NAME);
	} else if (response_info->http_status == OK) {
		/* 200 OK */
		sprintf(buf,
			"%s %d %s\r\n"
			"Date: %s\r\n"
			"Server: %s\r\n"
			"Last-Modified: %s\r\n"
			"Content-Type: %s\r\n",
			HTTP_VERSION, response_info->http_status, status_phrase(response_info->http_status),
			timestr,
			HTTP_SERVER_NAME,
			lastmodstr,
			get_content_type(response_info->file_path));
	} else {
		/* return type as text/html */
		sprintf(buf,
			"%s %d %s\r\n"
			"Date: %s\r\n"
			"Server: %s\r\n"
			"Last-Modified: %s\r\n"
			"Content-Type: text/html\r\n",
			HTTP_VERSION, response_info->http_status, status_phrase(response_info->http_status),
			timestr,
			HTTP_SERVER_NAME,
			lastmodstr);
	}
	if (response_info->body_flag == 1 && response_info->http_status != Not_Modified) {
		/* append a content length and a ending CRLF */
		sprintf(len, "Content-Length: %zu\r\n\r\n", response_info->content_length);
		strncat(buf, len, strlen(len));
	} else {
		/* append a ending CRLF */
		sprintf(len, "\r\n");
		strncat(buf, len, strlen(len));
	}
	for (i = 0; i < strlen(buf); i ++) {
		/* copy buf */
		resp_buf[i] = buf[i];
	}
	/* return the size of buf*/
	*size = strlen(buf);
	return 0;
}

int
cgi_response(struct http_response *response_info, char *resp_buf, size_t capacity, size_t *size)
{
	char buf[capacity];
	char timestr[64], lastmodstr[64];
	time_t present;
	int i = 0;

	/* process the current time and last modified time */
	time(&present);
	strftime(timestr, sizeof(timestr), rfc1123_DATE_STR, gmtime(&present));
	strftime(lastmodstr, sizeof(lastmodstr), rfc1123_DATE_STR, gmtime(&response_info->last_modified));

	sprintf(buf,
		"%s %d %s\r\n"
		"Date: %s\r\n"
		"Server: %s\r\n"
		"Last-Modified: %s\r\n",
		HTTP_VERSION, response_info->http_status, status_phrase(response_info->http_status),
		timestr,
		HTTP_SERVER_NAME,
		lastmodstr);
	for (i = 0; i < strlen(buf); i ++) {
		/* copy buf */
		resp_buf[i] = buf[i];
	}
	/* return the size of buf*/
	*size = strlen(buf);
	return 0;
}

char*
status_phrase(int code)
{
	/* get the status phrase thru status code */
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

/*
 * simple routine to check the extension of file path
 * and get the content type
 */
char*
get_content_type(char* file_path)
{
	char* ext = strrchr(file_path, '.');
	if (!ext) {
		return "text/html";
	} else if (strncmp (ext, ".png", strlen(".png")) == 0 ) {
		return "image/png";
	} else if (strncmp (ext, ".jpg", strlen(".jpg")) == 0 ) {
		return "image/jpg";
	 } else if (strncmp (ext, ".bmp", strlen(".bmp")) == 0 ) {
		return "image/bmp";
	} else if (strncmp (ext, ".gif", strlen(".gif")) == 0 ) {
		return "image/gif";
	} else if (strncmp (ext, ".doc", strlen(".doc")) == 0 ) {
		return "document/doc";
	} else if (strncmp (ext, ".docx", strlen(".docx")) == 0 ) {
		return "document/docx";
	} else if (strncmp (ext, ".xls", strlen(".xls")) == 0 ) {
		return "document/xls";
	} else if (strncmp (ext, ".xlsx", strlen(".xlsx")) == 0 ) {
		return "document/xlsx";
	} else if (strncmp (ext, ".pdf", strlen(".pdf")) == 0 ) {
		return "document/pdf";
	} else if (strncmp (ext, ".tar", strlen(".tar")) == 0 ) {
		return "archive/tar";
	} else if (strncmp (ext, ".html", strlen(".html")) == 0 ) {
		return "text/html";
	} else {
    		return "text/plain";
    	}
}
