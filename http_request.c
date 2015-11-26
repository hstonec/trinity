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
#include <unistd.h>
#include <time.h>

#include "http.h"

#define HEADER_FIELD	1

int process_header(char *Header_Field, struct http_request *request_info);
char *set_request(char *request_val);
int check_num(char *check_val);
int check_tm(struct tm *http_data);
int get_datenum(char *date_list[], char *sub);
int set_asctime(struct tm *http_date, char *request_val);
int set_method(char *method, struct http_request *request_info);
int set_rfc1123(struct tm *http_date, char *request_val);
int set_rfc850(struct tm *http_date, char *request_val);
char *http_decoding(struct http_request *request_info, char *http_url);
int to_num(char *header);
int htod(char hex1, char hex2);
time_t set_date(char *request_val, struct http_request *request_info);

static char *wkday[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", NULL };
static char *weekday[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", NULL };
static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };

int q_err;

int request(char *buf, struct http_request *request_info)
{
	char *request = buf;
	char *Header_Field;
	char *method;
	int ret;
	q_err = 0;
	/* process the first line of http request */
	method = strtok_r(NULL, "\r\n", &request);
// 	strncpy(request_info->first_line, method, strlen(method) + 1);
// 	request_info->first_line[strlen(method)] = '\0';
	ret = set_method(method, request_info);
	if (ret)
		return q_err;
	/* http decoding */
	
	/* process the following header fields */
	while(1){
		Header_Field = strtok_r(NULL, "\r\n", &request);
		if (Header_Field == NULL){
			/* end of http request */
			break;
		}
		ret = process_header(Header_Field, request_info);
		if (ret > 0)
			return q_err;
	}
	return q_err;
}

/* http decoding */
char *http_decoding(struct http_request *request_info, char *http_url)
{
	/* if error, set q_err to 6*/
	char *decoded_url;
	int len = strlen(http_url) + 1;
	int i = 0;
	char hex1;
	char hex2;
	int decimal_str;

	decoded_url = (char *)malloc(sizeof(char)*len);
	if (decoded_url == NULL){
		q_err = 7;
		return NULL;
	}
	decoded_url[len] = '\0';

	for (i = 0; i < len; i++)
	{
		if (http_url[i] == '%'){
			hex1 = http_url[i + 1];
			hex2 = http_url[i + 2];
			decimal_str = htod(hex1, hex2);
			if (decimal_str < 0 || !isascii(decimal_str)){
				q_err = 7;
				return NULL;
			}
			decoded_url[i] = (char)decimal_str;
		}
		else{
			decoded_url[i] = http_url[i];
		}
	}
	return decoded_url;
}

/* hex to decimal */
int htod(char hex1, char hex2)
{
	int ret = 0;
	char upper_c;
	if (isalnum(hex1) && isalnum(hex2)){
		if (isdigit(hex1))
			ret += atoi(&hex1) * 16;
		if (isalpha(hex1)){
			upper_c = toupper(hex1);
			if (upper_c >= 'A'&&upper_c <= 'F')
				ret += (upper_c - 'A' + 10) * 16;
			else
				return -1;
		}
		else
			return -1;
		if (isdigit(hex2))
			ret += atoi(&hex2);
		if (isalpha(hex2)){
			upper_c = toupper(hex2);
			if (upper_c >= 'A'&&upper_c <= 'F')
				ret += upper_c - 'A' + 10;
			else
				return -1;
		}
		else
			return -1;
		return ret;
	}
	return -1;
}

/* set http method to http_request, return 1 if error */
int set_method(char *method, struct http_request *request_info)
{
	char *method_type;
	char *method_val;
	char *http_version;
	char *rest;
	method_type = strtok_r(method, " ", &rest);
	method_val = strtok_r(NULL, " ", &rest);
	(void)strtok_r(rest, "/", &http_version);
	if (method_type&&method_val&&http_version)
	{
		if (strcmp(method_type, "GET") == 0)
			request_info->method_type = GET;
		if (strcmp(method_type, "HEAD") == 0)
			request_info->method_type = HEAD;
		if (strcmp(method_type, "POST") == 0)
			request_info->method_type = POST;
		request_info->request_URL = http_decoding(request_info, method_val);
		//request_info->request_URL = set_request(method_val);
		request_info->http_version = set_request(http_version);
		return 0;
	}
	q_err = 1;
	return 1;
}

/* process http request header field 
 * split header and value
 * return 2 if error 
 */
int process_header(char *Header_Field, struct http_request *request_info)
{
	char *header;
	char *header_value;
	header = strtok_r(Header_Field, ":", &header_value);
	if (header == NULL&&header_value == NULL){
		q_err = 2;
		return 2;
	}
	switch (to_num(header))
	{
	case 0:		/*If-Modified-Since*/
		request_info->if_modified_since = set_date(header_value, request_info);
		if (request_info->if_modified_since < 0)
			request_info->if_modified_flag = 0;
		else
			request_info->if_modified_flag = 1;
		break;
	default:
		break;
	}
	return 0;
}

/* set string request value, return null if error */
char *set_request(char *request_val)
{
	char *request_type;
	request_type = (char *)malloc((strlen(request_val) + 1)*sizeof(char));
	if (request_type == NULL)
		return NULL;
	strncpy(request_type, request_val, strlen(request_val) + 1);
	request_type[strlen(request_val)] = '\0';
	return request_type;
}

/* set tm from rfc850 format date, return 1 if error */
int set_rfc850(struct tm *http_date, char *request_val)
{
	time_t t_now = time(NULL);
	struct tm *gmt_now = gmtime(&t_now);
	char *rest;
	char *week;
	char *hour;
	char *minute;
	char *second;
	char *year;
	char *month;
	char *day;
	int n_week;
	int n_month;
	week = strtok_r(request_val, ",", &rest);
	day = strtok_r(NULL, "-", &rest);
	/* delete " " in day */
	strtok_r(NULL, " ", &day);
	month = strtok_r(NULL, "-", &rest);
	year = strtok_r(NULL, " ", &rest);
	hour = strtok_r(NULL, ":", &rest);
	minute = strtok_r(NULL, ":", &rest);
	second = strtok_r(NULL, " ", &rest);
	if (week&&day&&month&&year&&hour&&minute&&second)
	{
		if ((n_week = get_datenum(weekday, week)) == -1)
			return 3;
		if ((n_month = get_datenum(months, month)) == -1)
			return 3;
		if (check_num(day) && check_num(year) && check_num(hour) && check_num(minute) && check_num(second))
		{
			http_date->tm_hour = atoi(hour);
			http_date->tm_min = atoi(minute);
			http_date->tm_sec = atoi(second);
			if (atoi(year) + 100 <= gmt_now->tm_year)
				http_date->tm_year = atoi(year) + 100;
			else
				http_date->tm_year = atoi(year);
			http_date->tm_mon = n_month;
			http_date->tm_mday = atoi(day);
			http_date->tm_wday = n_week;
			http_date->tm_isdst = 0;
			if (check_tm(http_date))
				return 3;
			return 0;
		}
	}
	return 3;
}

/* set tm from rfc1123 format date, return 1 if error */
int set_rfc1123(struct tm *http_date, char *request_val)
{
	char *rest;
	char *week;
	char *hour;
	char *minute;
	char *second;
	char *year;
	char *month;
	char *day;
	int n_week;
	int n_month;
	//Date:Mon, 02 Jun 1982 23:59:59 GMT
	week = strtok_r(request_val, ",", &rest);
	day = strtok_r(NULL, " ", &rest);
	month = strtok_r(NULL, " ", &rest);
	year = strtok_r(NULL, " ", &rest);
	hour = strtok_r(NULL, ":", &rest);
	minute = strtok_r(NULL, ":", &rest);
	second = strtok_r(NULL, " ", &rest);
	if (week&&day&&month&&year&&hour&&minute&&second)
	{
		if ((n_week = get_datenum(wkday, week)) == -1)
			return 4;
		if ((n_month = get_datenum(months, month)) == -1)
			return 4;
		if (check_num(day) && check_num(year) && check_num(hour) && check_num(minute) && check_num(second))
		{
			http_date->tm_hour = atoi(hour);
			http_date->tm_min = atoi(minute);
			http_date->tm_sec = atoi(second);
			http_date->tm_year = atoi(year) - 1900;
			http_date->tm_mon = n_month;
			http_date->tm_mday = atoi(day);
			http_date->tm_wday = n_week;
			http_date->tm_isdst = 0;
			if (check_tm(http_date))
				return 4;
			return 0;
		}
	}
	return 4;
}

/* set tm from asctime format date, return 1 if error */
int set_asctime(struct tm *http_date, char *request_val)
{
	char *rest;
	char *week;
	char *hour;
	char *minute;
	char *second;
	char *year;
	char *month;
	char *day;
	int n_week;
	int n_month;
	//Date:Mon Jun  2 23:12:26 2010
	week = strtok_r(request_val, " ", &rest);
	month = strtok_r(NULL, " ", &rest);
	day = strtok_r(NULL, " ", &rest);
	hour = strtok_r(NULL, ":", &rest);
	minute = strtok_r(NULL, ":", &rest);
	second = strtok_r(NULL, " ", &rest);
	year = strtok_r(NULL, " ", &rest);

	if (week&&day&&month&&year&&hour&&minute&&second)
	{
		if ((n_week = get_datenum(wkday, week)) == -1)
			return 5;
		if ((n_month = get_datenum(months, month)) == -1)
			return 5;
		if (check_num(day) && check_num(year) && check_num(hour) && check_num(minute) && check_num(second))
		{
			http_date->tm_hour = atoi(hour);
			http_date->tm_min = atoi(minute);
			http_date->tm_sec = atoi(second);
			http_date->tm_year = atoi(year) - 1900;
			http_date->tm_mon = n_month;
			http_date->tm_mday = atoi(day);
			http_date->tm_wday = n_week;
			http_date->tm_isdst = 0;
			if (check_tm(http_date))
				return 5;
			return 0;
		}
	}
	return 5;
}

/*
rfc1123-date   = wkday "," SP date1 SP time SP "GMT"
Mon, 02 Jun 1982 23:59:59 GMT

rfc850-date    = weekday "," SP date2 SP time SP "GMT"
Monday, 02-Jun-82 23:59:59 GMT

asctime-date   = wkday SP date3 SP time SP 4DIGIT
Mon Jun  2 23:59:59 1982
*/
time_t set_date(char *request_val,struct http_request *request_info)
{
	struct tm http_date;
	time_t t;
	if (strstr(request_val, "GMT")){
		if (strstr(request_val, "-")){
			//rfc850 
			//Date:Monday, 02-Jun-02 23:54:07 GMT
			q_err = set_rfc850(&http_date, request_val);
		}
		else{
			//rfc1123
			//Date:Mon, 02 Jun 1982 23:59:59 GMT
			q_err = set_rfc1123(&http_date, request_val);
		}
	}
	else{
		//asctime
		//Date:Mon Jun  2 23:12:26 2010
		q_err = set_asctime(&http_date, request_val);
	}
	t = mktime(&http_date);
	return t;
}

/* transform header to category number, return -1 if error */
int to_num(char *header)
{
	char *hf_list[] = {
		"If-Modified-Since"
	};
	int i = 0;
	for (i = 0; i < HEADER_FIELD; i++)
	{
		if (strcmp(header, hf_list[i]) == 0){
			return i;
		}
	}
	return -1;
}

/* if check_val is a number string, return 1 , else 0*/
int check_num(char *check_val)
{
	int i = 0;
	int ret = 1;
	for (i = 0; i < strlen(check_val); i++)
		isdigit(check_val[i]) > 0 ? (ret = ret * 1) : (ret = ret * 0);
	return ret;
}

/* get the index num of month and week, return -1 if error */
int get_datenum(char *date_list[], char *sub)
{
	int i = 0;
	for (i = 0; date_list[i] != NULL; i++)
		if (strcmp(date_list[i], sub) == 0)
			return i;
	return -1;
}

/* check tm value, return 1 if there is an error */
int check_tm(struct tm *http_data)
{
	if (http_data->tm_hour>23 && http_data->tm_hour < 0)
		return 1;
	if (http_data->tm_mday>31 && http_data->tm_mday < 1)
		return 1;
	if (http_data->tm_min>59 && http_data->tm_min < 0)
		return 1;
	if (http_data->tm_sec>59 && http_data->tm_sec < 0)
		return 1;
	if (http_data->tm_year < 0)
		return 1;
	return 0;
}

void clean_request(struct http_request *request_info)
{
	free(request_info->request_URL);
	request_info->request_URL = NULL;
	free(request_info->http_version);
	request_info->http_version = NULL;
}