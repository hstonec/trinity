#ifndef _HTTP_H
#define _HTTP_H

#include <arpa/inet.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <bsd/stdlib.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define OK						200
#define Created					201
#define Accepted				202
#define No_Content				204
#define Moved_Permanently		301
#define Moved_Temporarily		302
#define Not_Modified			304
#define Bad_Request				400
#define Unauthorized			401
#define Forbidden				403
#define Not_Found				404
#define Internal_Server_Error	500
#define Not_Implemented			501
#define Bad_Gateway				502
#define Service_Unavailable		503

#define METHOD
#define GET				001
#define HEAD			002
#define POST			004
#define METHOD_MASK		007

#define DATETYPE
#define rfc1123_DATE		1
#define rfc850_DATE			2
#define asctime_DATE		3

/* the number of header field */
#define HEADER_FIELD	6
#define WAITLOCK		60

static char *wkday[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", NULL };
static char *weekday[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", NULL };
static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };

struct http_request
{
	int method_type;
	char *request_URL;
	char *http_version;
	struct tm HTTP_date;
	struct tm if_modified_since;
	struct tm last_modified;
	char *absoluteURI; /* location */
	char *server;
	char *user_agent;
	/*
	 * err:
	 * 000: succeed
	 * 001: set_method
	 * 002: process_header
	 * 003: set_date
	 */
	int err;
	/* log */
	char *first_line;
	/* need to be set */
	struct tm receive_time;
	char *client_ip;
	char *state_code;
	int content_length; 
};


int process_header(char *Header_Field, struct http_request *request_info);
char *set_request(char *request_val);
int check_num(char *check_val);
int check_tm(struct tm *http_data);
int get_datenum(char *date_list[], char *sub);
struct http_request request(char *buf);	/* deal with http request */
int set_asctime(struct tm *http_date, char *request_val);
int set_method(char *method, struct http_request *request_info);
int set_rfc1123(struct tm *http_date, char *request_val);
int set_rfc850(struct tm *http_date, char *request_val);
int to_num(char *header);
struct tm set_date(char *request_val, struct http_request *request_info);

#endif
