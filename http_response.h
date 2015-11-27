#ifndef _HTTP_RESPONSE_H
#define _HTTP_RESPONSE_H

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

#define rfc1123_DATE "%a, %d %b %Y %T GMT"

struct http_response
{
	time_t last_modified;
	//char *file_path;
	char *content_type;
	unsigned long content_length;
	int http_status;
	int body_flag;
	int keep_alive;
};

/* deal with http response */
int response(struct http_response *response_info, char *resp_buf, int capacity, int *size);
char* status_phrase(int code);
//void clean_response(struct http_response *response_info);
#endif

