#ifndef _SWS_H_
#define _SWS_H_

struct swsopt {
	BOOL opt[256];
	JSTRING *content_dir;
	JSTRING *cgi_dir;
	char *address;
	int fd_logfile;
	char *port;
};

#endif /* !_SWS_H_ */