/*
 * This program implements a rudimentary server 
 * which will read one line of text from any 
 * connecting client and print out on stdout that 
 * line prefixed with the remote client's IP address.
 */
#include <bsd/stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jstring.h"
#include "macros.h"
#include "sws.h"
#include "net.h"

int main(int, char **);
static BOOL is_dir(char *);
static JSTRING *convert(char *, char *);
static void usage();
static void print_help();

/*
 * This function parses all command-line options 
 * and sets all flags, then calls start_server()
 * function to start the server.
 */
int
main(int argc, char *argv[])
{
	int opt, fd;
	char *logfile, *cgidir;
	char *cwd;
	struct swsopt so;
	
	/* By default, set all options to be FALSE */
	memset(so.opt, FALSE, sizeof(BOOL) * 256);
	
	setprogname(argv[0]);
	
	/* Get current wording directory */
	if ((cwd = getcwd(NULL, 0)) == NULL) {
		(void)fprintf(stderr,
		  "%s: get CWD error: %s\n",
		  getprogname(),
		  strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	while ((opt = getopt(argc, argv, 
					"c:dhi:l:p:")) != -1) {
		switch (opt) {
		case 'c':
			so.opt['c'] = TRUE;
			cgidir = optarg;
			break;
		case 'd':
			so.opt['d'] = TRUE;
			break;
		case 'h':
			print_help();
			/* NOTREACHED */
		case 'i':
			so.opt['i'] = TRUE;
			so.address = optarg;
			break;
		case 'l':
			so.opt['l'] = TRUE;
			logfile = optarg;
			break;
		case 'p':
			so.opt['p'] = TRUE;
			so.port = optarg;
			break;
		case '?':
			usage();
			/* NOTREACHED */
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;
	
	/* the content directory must be given through command line */
	if (argc != 1) {
		(void)fprintf(stderr,
		  "%s: miss operand or too many operands\n",
		  getprogname());
		usage();
		/* NOTREACHED */
	}
	
	/* verify whether dir is a valid directory */
	if (is_dir(argv[0]) == FALSE) {
		(void)fprintf(stderr,
		  "%s: '%s' is not a directory\n",
		  getprogname(),
		  argv[0]);
		exit(EXIT_FAILURE);
	}
	/* 
	 * It's necessary to convert the content directory to
	 * absolute path. Because if the server runs as a
	 * daemon process, the working directory will be set
	 * to '/'.
	 */
	so.content_dir = convert(cwd, argv[0]);
	
	/* verify whether cgi_dir is a valid directory*/
	if (so.opt['c']) {
		if (is_dir(cgidir) == FALSE) {
			(void)fprintf(stderr,
			  "%s: '%s' is not a directory\n",
			  getprogname(),
			  cgidir);
			exit(EXIT_FAILURE);
		}
		so.cgi_dir = convert(cwd, cgidir);
	}
	
	
	/*
	 * -d has higher priority than -l option. If -d
	 * was set, the log information must be printed
	 * out to stdout
	 */
	if (so.opt['d'] == TRUE) {
		so.opt['l'] = TRUE;
		so.fd_logfile = STDOUT_FILENO;
	} else if (so.opt['l'] == TRUE) {
		if ((fd = open(logfile, 
			O_WRONLY | O_APPEND)) == -1) {
			(void)fprintf(stderr,
			  "%s: open '%s' failed: %s\n",
			  getprogname(),
			  logfile,
			  strerror(errno));
			exit(EXIT_FAILURE);
		}
		so.fd_logfile = fd;
	}
	
	start_server(&so);
	
	if (so.opt['d'] == FALSE && so.opt['l'] == TRUE)
		(void)close(so.fd_logfile);
	
	if (so.opt['c'] == TRUE)
		jstr_free(so.cgi_dir);
	
	jstr_free(so.content_dir);
	
	free(cwd);
	return EXIT_SUCCESS;
}

static BOOL
is_dir(char *path)
{
	struct stat buf;
	
	if (stat(path, &buf) == -1) {
		(void)fprintf(stderr,
		  "%s: stat '%s' error: %s\n",
		  getprogname(),
		  path,
		  strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	if (S_ISDIR(buf.st_mode))
		return TRUE;
	else
		return FALSE;
}

/*
 * If dir is an relative path, this function transforms
 * it to absolute path.
 */
static JSTRING *
convert(char *cwd, char *dir) 
{
	JSTRING *pcwd, *pdir;
	
	if (strcmp(dir, "/") == 0)
		return jstr_create("");
	
	pdir = jstr_create(dir);
	if (jstr_charat(pdir, jstr_length(pdir) - 1) == '/')
		jstr_trunc(pdir, 0, jstr_length(pdir) - 1);
	
	if (jstr_charat(pdir, 0) == '/')
		return pdir;
	
	pcwd = jstr_create(cwd);
	if (jstr_charat(pcwd, jstr_length(pcwd) - 1) != '/')
		jstr_append(pcwd, '/');
	jstr_concat(pcwd, jstr_cstr(pdir));
	
	jstr_free(pdir);
	
	return pcwd;
}

static void
usage()
{
	(void)fprintf(stderr, 
	  "usage: %s [-dh] [-c dir] [-i address] [-l file] [-p port] dir\n", 
	  getprogname());
	exit(EXIT_FAILURE);
}

static void
print_help()
{
	(void)fprintf(stdout, "\nSYNOPSIS\n");
	(void)fprintf(stdout, "       %s [OPTION]... dir\n\n",
	              getprogname());
	
	(void)fprintf(stdout, "DESCRIPTION\n");
	
	(void)fprintf(stdout,
	  "       sws serves content from the given directory.\n\n");
	(void)fprintf(stdout,
	  "       sws supports the following options:\n\n");
	
	(void)fprintf(stdout,
	  "       -c dir\n");
	(void)fprintf(stdout,
	  "              Allow execution of CGIs from the given directory.\n\n");
	 
	(void)fprintf(stdout,
	  "       -d     Enter debugging mode. That is, do not daemonize, only " \
	                 "accept\n");
	(void)fprintf(stdout,
	  "              one connection at a time and enable logging to " \
	                 "stdout.\n\n");
	
	(void)fprintf(stdout,
	  "       -h     Print a short usage summary and exit.\n\n");
	  
	(void)fprintf(stdout,
	  "       -i address\n");
	(void)fprintf(stdout,
	  "              Bind to the given IPv4 or IPv6 address. If not " \
	                 "provided, sws\n");
	(void)fprintf(stdout,
	  "              will listen on all IPv4 and IPv6 addresses on " \
	                 "this host.\n\n");
	
	(void)fprintf(stdout,
	  "       -l file\n");
	(void)fprintf(stdout,
	  "              Log all requests to the given file.\n\n");
	
	(void)fprintf(stdout,
	  "       -p port\n");
	(void)fprintf(stdout,
	  "              Listen on the given port. If not provided, sws " \
	                 "will listen\n");
	(void)fprintf(stdout,
	  "              on port 8080.\n\n");
	exit(EXIT_SUCCESS);
}