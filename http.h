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

struct http_response
{
        time_t last_modified;
        char *file_path;
        size_t content_length;
        int http_status;
        int body_flag;
};

struct set_logging
{
	int fd; 
	char *first_line;	/* already set up in request */
	char *client_ip;
	int state_code;
	size_t content_length;
	time_t receive_time;/* already set up in request */
};

int request(char *buf, struct http_request *request_info, struct set_logging *logging_info);	/* deal with http request */
void clean_request(struct http_request *request_info);

int response(struct http_response *response_info, char *resp_buf, int capacity, int *size);    /* deal with http response */

/* logging will return the length function written
 * return 0 if error 
 * This function will check set_logging structure to 
 * make sure there is no empty or illegal values.
 */
int logging(struct set_logging *logging_info);
void clean_logging(struct set_logging *logging_info);
#endif
