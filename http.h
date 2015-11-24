#ifndef _HTTP_H
#define _HTTP_H

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


struct http_request
{
	int method_type;
	char *request_URL;
	char *http_version;
	int if_modified_flag;  /* 1 for yes */
	time_t if_modified_since;
};

int request(char *buf, struct http_request *request_info);	/* deal with http request */
void clean_request(struct http_request *request_info);
#endif
