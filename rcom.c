/*
 *  - rcom.c
 *  vers. 1.2 (rev. date 2003.09.28)
 *
 *  Copyright (c) 2000 - pirus, Zapek
 *  All rights reserved worldwide and beyond :)
 *
 *  @(#)$Id: rcom.c,v 1.1.1.1 2002/11/29 08:55:15 zapek Exp $
 */


#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
#include<signal.h>
#include<errno.h>
#include<string.h>
#include<ctype.h>
#if defined(__linux__)
#include<getopt.h>
#endif  /* __linux__ */
#include<termios.h>
#include<fcntl.h>

#include<sys/types.h>
#include<sys/time.h>
#include <time.h>
 

static const char rcsid[] =
	"$Id: rcom.c,v 1.1.1.1 2002/11/29 08:55:15 zapek Exp $";

#define VERSTR		"1.0"


#define PAR_NONE	CS8
#define PAR_EVEN	(CS7 | PARENB)
#define PAR_ODD		(CS7 | PARENB | PARODD)

#define DEF_PAR		PAR_NONE
#define DEF_BAUD	B9600
/* #define DEF_LINE	"/dev/modem" */
#define DEF_CBRK	''

#define BUF_SIZ		256


static int  loop(int, int);
static int  com_open(char *, int, int);
static int  tty_raw(int, int);
static int  tty_rst(int);
static int  s2b(char *);

static void sig_handle(int);
static void copyright(char *);
static void usage(char *);


static struct termios  sio;
static int    sigc;

extern char *optarg;
extern int   optind;

static int timestamps;


int
main(int argc, char *argv[])
{
	char   *name, *line;
	int     cbrk, par, baud, cflg, hdx;
	int     c, fd;
	char	buf[20];


	name = argv[0];
	/* line = DEF_LINE; */
	baud = DEF_BAUD;
	cbrk = DEF_CBRK;
	par  = DEF_PAR;
	hdx  = 0;
	cflg = 0;

	while((c = getopt(argc, argv, "cehlovwtE:s:")) != EOF ) {
		switch(c) {
			case 'c':
				cflg |= HUPCL;
				break;

			case 'e':
				if( par == DEF_PAR )
					par = PAR_EVEN;
				else
					fprintf(stderr, "option -%c ignored.\n", c);
				break;

			case 'h':
				hdx = 1;
				break;

			case 'l':
				cflg |= CLOCAL;
				break;

			case 'o':
				if( par == DEF_PAR )
					par = PAR_ODD;
				else
					fprintf(stderr, "option -%c ignored.\n", c);
				break;

			case 'v':
				copyright(name);
				break;

			case 't':
				timestamps = 1;
				break;

			case 'E':
				if( !iscntrl(*optarg) )
					fprintf(stderr, "invalid escape char, using default.\n");
				else
					cbrk = *optarg;
				break;

			case 's':
				baud = s2b(optarg);
				if( baud < 0 ) {
					fprintf(stderr, "invalid baud rate %s, using default.\n", optarg);
					baud = DEF_BAUD;
				}
				break;

			case '?':
			default:
				usage(name);
		}
	}
	argc -= optind;
	argv += optind;

	if( argc < 1 )
		usage(name);

	line = argv[0];


	if (!strncmp(line,"tty",3) && line[3] && line[4])
	{
		/* my easy tip support */
		sprintf(buf,"/dev/ttyS%d",tolower(line[3])-'a');

		baud = s2b(&line[4]);
		cflg |= CLOCAL;
		line = buf;
	}

#if defined(VERBOSE)
printf("  line: %s\n speed: %d\nparity: %d\n", line, baud, par);
#endif  /* VERBOSE */

	fd = com_open(line, baud, par|cflg);

	if( fd < 0 ) {
		perror(line);
		exit(EXIT_FAILURE);
	}

	if( isatty(STDIN_FILENO) && !hdx ) {
		if( tty_raw(STDIN_FILENO, cbrk) < 0 ) {
			perror("unable to switch tty to raw mode\n");
			exit(EXIT_FAILURE);
		}

		/*
		 *  catch some common signals to make sure the tty
		 *  is reset upon exit
		 */
		if( signal(SIGHUP, sig_handle) == SIG_ERR )
			perror("signal(SIGHUP)");

		if( signal(SIGINT, sig_handle) == SIG_ERR )
			perror("signal(SIGINT)");

		if( signal(SIGQUIT, sig_handle) == SIG_ERR )
			perror("signal(SIGQUIT)");

		if( signal(SIGTERM, sig_handle) == SIG_ERR )
			perror("signal(SIGTERM)");
	}
	else
		hdx = 1;


	(void)loop(fd, hdx);

	(void)tty_rst(STDIN_FILENO);

	if( sigc )
		fprintf(stderr, "signal caught\n");
	close(fd);

	return(EXIT_SUCCESS);
}


static int
write_timestamp()
{
	char timestamp[20];
	int len;
	struct timeval tv;
	static struct tm last;
	struct tm now;

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &now);
	if (last.tm_mday != now.tm_mday || last.tm_mon != now.tm_mon
			|| last.tm_year != now.tm_year) {
		strftime(timestamp, 20, "%F", &now);
		fprintf(stdout, "--- day changed to %s ---\n", timestamp);
		fflush(stdout);
		last = now;
	}
	len = strftime(timestamp, 20, "%T", &now);
	len += snprintf(timestamp+len, 20-len, ".%03i: ", (int)tv.tv_usec/1000);
	return write(STDOUT_FILENO, timestamp, len);
}

static int
loop(int comd, int hdx)
{
	fd_set  afds, rfds;
	int     n, nfd;

	char   *buf;
	int write_ts = 1;


	buf = (char *)malloc(BUF_SIZ);
	if( buf == NULL ) {
		perror("buf");
		return(-1);
	}

	FD_ZERO(&afds);
	if( !(isatty(STDIN_FILENO) && hdx) )
		FD_SET(STDIN_FILENO, &afds);
	FD_SET(comd, &afds);

	nfd = (STDIN_FILENO < comd ? comd : STDIN_FILENO) + 1;

	for( ;; ) {
		if( sigc )
			break;

		memcpy((char *)&rfds, (char *)&afds, sizeof(rfds));
		n = select(nfd, &rfds, NULL, NULL, NULL);
		if( n < 0 ) {
			if( errno == EINTR )
				continue;
			else {
				perror("select()");
				break;
			}
		}

		if( n == 0 ) {
		/*  should never happen (tm)  */
			perror("select()");
			fflush(stderr);
			(void)sleep(1);
		}

		if( FD_ISSET(STDIN_FILENO, &rfds) ) {
			n = read(STDIN_FILENO, buf, BUF_SIZ);
			if( n <= 0 ) {
				if( n < 0 ) {
					perror("stdin");
					break;
				}
				else {
				/*  EOF (n == 0)  */
					if( hdx )
						FD_CLR(STDIN_FILENO, &afds);
					else
						break;
				}
			}

			else {
				if( write(comd, buf, n) < n ) {
					perror("cannot write on device");
					break;
				}
			}
		}

		if( FD_ISSET(comd, &rfds) ) {
			char *pos1, *pos2;
			n = read(comd, buf, BUF_SIZ);
			if( n <= 0 ) {
				if( n < 0 )
					perror("read error on device");
				if( n == 0)
					perror("EOF\n");
				break;
			}

			if (timestamps == 0) {
				if (write(STDOUT_FILENO, buf, n) < n) {
					fprintf(stderr, "cannot write on tty\n");
					break;
				}
				continue;
			}

			if (write_ts) {
				write_timestamp();
				write_ts = 0;
			}
			pos1 = buf;
			while ((pos2 = memchr(pos1, '\n', n-(pos1-buf)))) {
				if (write(STDOUT_FILENO, pos1, pos2-pos1+1) < pos2-pos1+1)
					fprintf(stderr, "cannot write on tty\n");

				pos1 = pos2+1;
				if (pos2 < buf+n-1) {
					write_timestamp();
				} else {
					write_ts = 1;
					break;
				}
			}
			if (write(STDOUT_FILENO, pos1, n-(pos1-buf)) < n-(pos1-buf)) {
				fprintf(stderr, "cannot write on tty\n");
				break;
			}
		}
	}

	free(buf);
	return(0);
}


static int
s2b(char *str)
{
	char *s;
	int   n;

	struct { char *spd_s; int spd_b; } sbv[] = {
#if defined(B4000000)
		{ "4000000", B4000000 },
#endif
#if defined(B3500000)
		{ "3500000", B3500000 },
#endif
#if defined(B3000000)
		{ "3000000", B3000000 },
#endif
#if defined(B2500000)
		{ "2500000", B2500000 },
#endif
#if defined(B2000000)
		{ "2000000", B2000000 },
#endif
#if defined(B1500000)
		{ "1500000", B1500000 },
#endif
#if defined(B1152000)
		{ "1152000", B1152000 },
#endif
#if defined(B1000000)
		{ "1000000", B1000000 },
#endif
#if defined(B921600)
		{ "921600", B921600 },
#endif
#if defined(B576000)
		{ "576000", B576000 },
#endif
#if defined(B500000)
		{ "500000", B500000 },
#endif
#if defined(B460800)
		{ "460800", B460800 },
#endif
#if defined(B230400)
		{ "230400", B230400 },
#endif
#if defined(B115200)
		{ "115200", B115200 },
#endif
#if defined(B76800)
		{  "76800",  B76800 },
#endif
#if defined(B57600)
		{  "57600",  B57600 },
#endif
		{  "38400",  B38400 }, {  "19200",  B19200 },
		{   "9600",   B9600 }, {   "4800",   B4800 },
		{   "2400",   B2400 }, {   "1800",   B1800 },
		{   "1200",   B1200 }, {    "600",    B600 },
		{    "300",    B300 }, {    "200",    B200 },
		{    "150",    B150 }, {    "134",    B134 },
		{    "110",    B110 }, {     "75",     B75 },
		{     "50",     B50 }, {      "0",      B0 },
		{     NULL,      -1 },
	};

	n = 0;
	s = sbv[0].spd_s;
	while( (s != NULL) && strcmp(s, str) )
		s = sbv[++n].spd_s;

	return(sbv[n].spd_b);
}


static int
tty_raw(int fd, int cbrk)
{
	struct termios  tio;

	if( tcgetattr(fd, &sio) < 0 )
		return(-1);

	/*
	 *  set the terminal to 'raw' mode
	 *  see termios(4) for details
	 */
	tio = sio;

	tio.c_lflag &= ~( ECHO | ICANON | IEXTEN );
	tio.c_lflag |= ISIG;
	tio.c_iflag &= ~( BRKINT | ICRNL | INPCK | ISTRIP | IXON );
	tio.c_cflag &= ~( CSIZE | PARENB );
	tio.c_cflag |= CS8;
	tio.c_oflag &= ~( OPOST );

	tio.c_cc[VMIN]  = 1;
	tio.c_cc[VTIME] = 0;
	tio.c_cc[VINTR] = cbrk;

	return
		tcsetattr(fd, TCSAFLUSH, &tio);
}


static int
tty_rst(int fd)
{
	if( !cfgetispeed(&sio) && !cfgetospeed(&sio) )
		return(0);
	else
		return
			tcsetattr(fd, TCSANOW, &sio);
}


static int
com_open(char *line, int baud, int cflag)
{
	struct termios  tios;
	int    fd, mode;


	fd = open(line, O_RDWR|O_NONBLOCK);
	if( fd < 0 )
		return(-1);

	if( tcgetattr(fd, &tios) < 0 )
		return(-1);

	tios.c_cflag = CREAD | cflag;
	tios.c_iflag = IXON | IXOFF | IGNBRK | ISTRIP | IGNPAR;
	tios.c_oflag = 0;
	tios.c_lflag = 0;

	tios.c_cc[VMIN]  = 1;
	tios.c_cc[VTIME] = 0;

	cfsetispeed(&tios, baud);
	cfsetospeed(&tios, baud);

	if( tcsetattr(fd, TCSANOW, &tios) < 0 )
		return(-1);

	mode = fcntl(fd, F_GETFL, 0);
	if( (mode < 0) || (fcntl(fd, F_SETFL, mode & ~(O_NONBLOCK)) < 0) )
		return(-1);

	if (tcgetattr(fd, &tios) < 0)
		return -1;
	if (cfgetispeed(&tios) != baud) {
		fprintf(stderr, "couldn't set input baudrate\n");
	}
	if (cfgetospeed(&tios) != baud) {
		fprintf(stderr, "couldn't set output baudrate\n");
	}

	return(fd);
}


static void
sig_handle(int signo)
{
	++sigc;
	return;
}


static void
copyright(char *name)
{
	printf("%s: rcom " VERSTR ", Copyright (c) 2000 - pirus, Zapek\n", name);
	exit(EXIT_SUCCESS);
}


static void
usage(char *name)
{
	fprintf(stderr, "usage: %s [options] device\n"
"options:\n"
"	-c		hangup on close\n"
"	-e		parity EVEN\n"
"	-h		half-duplex mode\n"
"	-l		ignore modem status lines (local)\n"
"	-o		parity ODD\n"
"	-v		display version informations\n"
"	-E char		set escape character (default: ^X)\n"
"	-s speed	set baud rate (default: 9600)\n"
"	-t		print timestamps\n"
"\n"
, name);
	exit(EXIT_FAILURE);
}

