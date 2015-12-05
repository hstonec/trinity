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

#define HTTP_REQUEST_MAX_LENGTH 8192
#define HTTP_RESPONSE_MAX_LENGTH 8192

#define HTTP_IMPL_VERSION 1.0
#define HTTP_VERSION "HTTP/1.0"
#define HTTP_SERVER_NAME "Trinity/1.0"

/*
 * http_request
 * This structure provides a interface between main program and http
 * request processing part. The request(1) function will fill this
 * structure after processing the http request message.
 * clean_request(1) needs to be called to release the memory.
 */
struct http_request
{
	int method_type;
	char *request_URL;
	float http_version;
	int if_modified_flag;  /* 1 for yes */
	time_t if_modified_since;
};
/*
 * http_response
 * This structure provides a interface between main program and http
 * response processing part. The response(4) function generates http
 * response header according to this structure.
 */
struct http_response
{
        time_t last_modified;
        char *file_path;
        size_t content_length;
        int http_status;
        int body_flag;
};
/*
 * set_logging
 * This structure contains all messages that need to be logged to
 * file. The logging(1) function will do logging according to this
 * structure. clean_logging(1) needs to be called to release memory.
 */
struct set_logging
{
	int fd;
	char *first_line;	/* already set up in request */
	char *client_ip;
	int state_code;
	int logging_flag;
	size_t content_length;
	time_t receive_time;/* already set up in request */
};
/* 
 * request(3) processes http request line and request headers,
 * if there is syntax problem in the http request message, it 
 * will return larger than 0. This function will fill http request
 * information to the http_request structure and the set_logging 
 * structure.
 */
int request(char *buf, struct http_request *request_info, 
		struct set_logging *logging_info);
/* release the memory of http_requst */ 
void clean_request(struct http_request *request_info);

/* 
 * response(4) processes http response status line and response
 * headers, in normal it will return 0. This function will generate
 * http response header buffer, according to http_response structure.
 */
int response(struct http_response *response_info, char *resp_buf, 
		size_t capacity, size_t *size);
/* deal with cgi response, the idea of this is same as response(4) */
int cgi_response(struct http_response *response_info, char *resp_buf, 
		size_t capacity, size_t *size);

/* logging will return the length function written
 * return 0 if error 
 * This function will check set_logging structure to 
 * make sure there is no empty or illegal values.
 */
int logging(struct set_logging *logging_info);
void clean_logging(struct set_logging *logging_info);

#endif
