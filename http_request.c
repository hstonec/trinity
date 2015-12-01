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
#define LOGGING_BUF		4096

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
char *split_str(char *source, char **rest);
int check_version(char *http_version);
int check_format(char *method);

static char *wkday[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", NULL };
static char *weekday[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday", NULL };
static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", NULL };

int q_err;

/* This function processes http request header fields.
 * This function will set values for http_request structure
 * and set_logging structure to communicate with other functions
 * This function will return 0 if succeed, larger than 0 if error.
 */
int request(char *buf, struct http_request *request_info, struct set_logging *logging_info)
{
	char *request_head = buf;
	char *Header_Field;
	char *method;
	int ret;
	q_err = 0;
	/* process the first line of http request */
	method = split_str(NULL, &request_head);
	//method = strtok_r(NULL, "\r\n", &request_head);
	if (method == NULL)
		return 1;
	/* set logging information */
	logging_info->first_line = set_request(method);
	time(&logging_info->receive_time);

	ret = set_method(method, request_info);
	if (ret)
		return q_err;
	if (strcmp(request_head, "") == 0)
		return 1;
	/* process the following header fields */
	while(1){
		Header_Field = split_str(NULL, &request_head);
		//Header_Field = strtok_r(NULL, "\r\n", &request_head);
		if (Header_Field == NULL)
			return 1;
		if (strcmp(Header_Field, "") == 0)
			break;/* end of http request */

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
	int len = strlen(http_url);
	int i = 0;
	int j = 0;
	char hex1;
	char hex2;
	int decimal_str;

	decoded_url = (char *)malloc(sizeof(char)*(len + 1));
	if (decoded_url == NULL){
		q_err = 7;
		return NULL;
	}
	decoded_url[len] = '\0';

	for (i = 0, j = 0; i < len; i++, j++)
	{
		if (http_url[j] == '%'){
			hex1 = http_url[j + 1];
			hex2 = http_url[j + 2];
			decimal_str = htod(hex1, hex2);
			if (decimal_str < 0 || !isascii(decimal_str)){
				q_err = 7;
				return NULL;
			}
			decoded_url[i] = (char)decimal_str;
			j += 2;
		}
		else{
			decoded_url[i] = http_url[j];
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
		else if (isalpha(hex1)){
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
		else if (isalpha(hex2)){
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
	char *http_str;
	char *rest;
	if (check_format(method)){
		q_err = 1;
		return 1;
	}
	method_type = strtok_r(method, " ", &rest);
	method_val = strtok_r(NULL, " ", &rest);
	http_str = strtok_r(rest, "/", &http_version);
	if (method_type&&method_val&&http_version&&http_str)
	{
		if (strcmp(method_type, "GET") == 0)
			request_info->method_type = GET;
		else if (strcmp(method_type, "HEAD") == 0)
			request_info->method_type = HEAD;
		else if (strcmp(method_type, "POST") == 0)
			request_info->method_type = POST;
		else{
			q_err = 1;
			return 1;
		}
		if (strcmp(http_str, "HTTP") != 0 || method_val[0] != '/') {
			q_err = 1;
			return 1;
		}
		request_info->request_URL = http_decoding(request_info, method_val);
		if (check_version(http_version))
			request_info->http_version = atof(http_version);
		else{
			q_err = 1;
			return 1;
		}
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
time_t set_date(char *request_val, struct http_request *request_info)
{
	putenv("TZ=GMT");
	tzset();
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
	//free(request_info->http_version);
	//request_info->http_version = NULL;
}

/* Logging writes logging information to logging file.
 * If there is an error, logging will return 0. 
 * If succeed, logging will return the length written to 
 * logging file.
 */
int logging(struct set_logging *logging_info)
{
	char output_buf[LOGGING_BUF];
	char receive_time[30];
	int ret, len, total;
	int fd = logging_info->fd;
	if (logging_info->logging_flag == 0)
		return 0;
	ret = 0;
	if (logging_info->client_ip == NULL || 
		logging_info->content_length < 0 || 
		logging_info->first_line == NULL || 
		logging_info->receive_time < 0 || 
		logging_info->state_code < 0)
		return -1;
	if (!strftime(receive_time, 30, "%a, %d %b %Y %H:%M:%S GMT", gmtime(&logging_info->receive_time)))
		return 0;
	len = snprintf(output_buf, LOGGING_BUF, "%s %s \"%s\" %d %ld\n",
		logging_info->client_ip,
		receive_time,
		logging_info->first_line,
		logging_info->state_code,
		logging_info->content_length);
	total = len;
	if (len){
		while (len)
		{
			if (len>LOGGING_BUF)
				ret = write(fd, output_buf, LOGGING_BUF);
			else
				ret = write(fd, output_buf, len);
			len -= ret;
		}
		return total;
	}
	else
		return 0;
}

void clean_logging(struct set_logging *logging_info)
{
	free(logging_info->first_line);
	logging_info->first_line = NULL;
}

char *split_str(char *source, char **rest)
{
	char *ret;
	if (source == NULL)
		source = *rest;
	ret = source;
	int len = strlen(source);
	int i = 0;
	for (i = 0; i < len; i++)
	{
		if (source[i] == '\r'&&source[i + 1] == '\n')
		{
			source[i] = '\0';
			source[i + 1] = '\0';
			*rest = source + i + 2;
			return ret;
		}
		else if (source[i] == '\r'&&source[i + 1] != '\n')
			return NULL;
		else if (source[i] == '\n')
			return NULL;
	}
	return "";
}

/* check if the http version is a proper formatted string 
 * return 1 if correct 
 */
int check_version(char *http_version)
{
	int len = strlen(http_version);
	int i, dot_flag, digit_flag;
	dot_flag = 0;
	digit_flag = 0;
	for (i = 0; i < len; i++)
	{
		if (dot_flag)
		{
			if (!isdigit(http_version[i]))
				return 0;
			else
				digit_flag = 2;
		}
		else
		{
			if (http_version[i] == '.'&&digit_flag == 1)
				dot_flag = 1;
			if (!isdigit(http_version[i]) && http_version[i] != '.')
				return 0;
			else
				digit_flag = 1;
		}
	}
	if (digit_flag == 2)
		return 1;
	else
		return 0;
}
/* check the format of Request-Line, return 1 if error */
int check_format(char *method)
{
	int len = strlen(method);
	int i;
	int count_space = 0;
	for (i = 0; i < len; i++)
	{
		if (method[i] == ' ')
			count_space++;
	}
	if (count_space != 2)
		return 1;
	else
		return 0;
}