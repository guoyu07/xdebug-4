/*
   +----------------------------------------------------------------------+
   | Xdebug                                                               |
   +----------------------------------------------------------------------+
   | Copyright (c) 2002-2010 Derick Rethans                               |
   +----------------------------------------------------------------------+
   | This source file is subject to version 1.0 of the Xdebug license,    |
   | that is bundled with this package in the file LICENSE, and is        |
   | available at through the world-wide-web at                           |
   | http://xdebug.derickrethans.nl/license.php                           |
   | If you did not receive a copy of the Xdebug license and are unable   |
   | to obtain it through the world-wide-web, please send a note to       |
   | xdebug@derickrethans.nl so we can mail you a copy immediately.       |
   +----------------------------------------------------------------------+
   | Authors:  Derick Rethans <derick@xdebug.org>                         |
   +----------------------------------------------------------------------+
 */

#ifdef linux
/* To enable CPU_ZERO and CPU_SET, etc.     */
# define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/file.h>
#else
#define PATH_MAX MAX_PATH
#include <winsock2.h>
#include <io.h>
#include "win32/time.h"
#include <process.h>
#endif
#include "php_xdebug.h"
#include "xdebug_mm.h"
#include "xdebug_str.h"
#include "usefulstuff.h"
#include "ext/standard/php_lcg.h"
#include "ext/standard/flock_compat.h"
#include "main/php_ini.h"

ZEND_EXTERN_MODULE_GLOBALS(xdebug)

#ifdef __APPLE__
/*
 * Patch for compiling in Mac OS X Leopard
 * @author Svilen Spasov <s.spasov@gmail.com> 
 */
#    include <mach/task.h>
#    include <mach/mach_init.h>
#    include <mach/thread_policy.h>
#    define cpu_set_t thread_affinity_policy_data_t
#    define CPU_SET(cpu_id, new_mask) \
        (*(new_mask)).affinity_tag = (cpu_id + 1)
#    define CPU_ZERO(new_mask)                 \
        (*(new_mask)).affinity_tag = THREAD_AFFINITY_TAG_NULL
#   define SET_AFFINITY(pid, size, mask)       \
        thread_policy_set(mach_thread_self(), THREAD_AFFINITY_POLICY, mask, \
                          THREAD_AFFINITY_POLICY_COUNT)
#elif linux
/* For sched_getaffinity, sched_setaffinity */
# include <sched.h>
# define SET_AFFINITY(pid, size, mask) sched_setaffinity(0, size, mask)
# define GET_AFFINITY(pid, size, mask) sched_getaffinity(0, size, mask)
# if _POSIX_C_SOURCE >= 199309L
#  include <time.h>
#  define HAVE_CLOCK_GETTIME 1
# endif
#else
#   error "This system is not supported"
#endif /* __Apple*/

#define READ_BUFFER_SIZE 128

struct xdebug_cputime_func {
	double (*get_cputime)(void);
	int (*bind_to_cpu)(int);
} xdebug_cputime_funcs;


char* xdebug_fd_read_line_delim(int socket, fd_buf *context, int type, unsigned char delim, int *length)
{
	int size = 0, newl = 0, nbufsize = 0;
	char *tmp;
	char *tmp_buf = NULL;
	char *ptr;
	char buffer[READ_BUFFER_SIZE + 1];

	if (!context->buffer) {
		context->buffer = calloc(1,1);
		context->buffer_size = 0;
	}

	while (context->buffer_size < 1 || context->buffer[context->buffer_size - 1] != delim) {
		ptr = context->buffer + context->buffer_size;
		if (type == FD_RL_FILE) {
			newl = read(socket, buffer, READ_BUFFER_SIZE);
		} else {
			newl = recv(socket, buffer, READ_BUFFER_SIZE, 0);
		}
		if (newl > 0) {
			context->buffer = realloc(context->buffer, context->buffer_size + newl + 1);
			memcpy(context->buffer + context->buffer_size, buffer, newl);
			context->buffer_size += newl;
			context->buffer[context->buffer_size] = '\0';
		} else {
			return NULL;
		}
	}

	ptr = memchr(context->buffer, delim, context->buffer_size);
	size = ptr - context->buffer;
	/* Copy that line into tmp */
	tmp = malloc(size + 1);
	tmp[size] = '\0';
	memcpy(tmp, context->buffer, size);
	/* Rewrite existing buffer */
	if ((nbufsize = context->buffer_size - size - 1)  > 0) {
		tmp_buf = malloc(nbufsize + 1);
		memcpy(tmp_buf, ptr + 1, nbufsize);
		tmp_buf[nbufsize] = 0;
	}
	free(context->buffer);
	context->buffer = tmp_buf;
	context->buffer_size = context->buffer_size - (size + 1);

	/* Return normal line */
	if (length) {
		*length = size;
	}
	return tmp;
}

char *xdebug_join(char *delim, xdebug_arg *args, int begin, int end)
{
	int         i;
	xdebug_str *ret;

	xdebug_str_ptr_init(ret);
	if (begin < 0) {
		begin = 0;
	}
	if (end > args->c - 1) {
		end = args->c - 1;
	}
	for (i = begin; i < end; i++) {
		xdebug_str_add(ret, args->args[i], 0);
		xdebug_str_add(ret, delim, 0);
	}
	xdebug_str_add(ret, args->args[end], 0);
	return ret->d;
}

void xdebug_explode(char *delim, char *str, xdebug_arg *args, int limit) 
{
	char *p1, *p2, *endp;

	endp = str + strlen(str);

	p1 = str;
	p2 = xdebug_memnstr(str, delim, strlen(delim), endp);

	if (p2 == NULL) {
		args->c++;
		args->args = (char**) xdrealloc(args->args, sizeof(char*) * args->c);
		args->args[args->c - 1] = (char*) xdmalloc(strlen(str) + 1);
		memcpy(args->args[args->c - 1], p1, strlen(str));
		args->args[args->c - 1][strlen(str)] = '\0';
	} else {
		do {
			args->c++;
			args->args = (char**) xdrealloc(args->args, sizeof(char*) * args->c);
			args->args[args->c - 1] = (char*) xdmalloc(p2 - p1 + 1);
			memcpy(args->args[args->c - 1], p1, p2 - p1);
			args->args[args->c - 1][p2 - p1] = '\0';
			p1 = p2 + strlen(delim);
		} while ((p2 = xdebug_memnstr(p1, delim, strlen(delim), endp)) != NULL && (limit == -1 || --limit > 1));

		if (p1 <= endp) {
			args->c++;
			args->args = (char**) xdrealloc(args->args, sizeof(char*) * args->c);
			args->args[args->c - 1] = (char*) xdmalloc(endp - p1 + 1);
			memcpy(args->args[args->c - 1], p1, endp - p1);
			args->args[args->c - 1][endp - p1] = '\0';
		}
	}
}

char* xdebug_memnstr(char *haystack, char *needle, int needle_len, char *end)
{
	char *p = haystack;
	char first = *needle;

	/* let end point to the last character where needle may start */
	end -= needle_len;
	
	while (p <= end) {
		while (*p != first)
			if (++p > end)
				return NULL;
		if (memcmp(p, needle, needle_len) == 0)
			return p;
		p++;
	}
	return NULL;
}

double xdebug_get_utime(void)
{
#ifdef HAVE_GETTIMEOFDAY
	struct timeval tp;
	long sec = 0L;
	double msec = 0.0;

	if (gettimeofday((struct timeval *) &tp, NULL) == 0) {
		sec = tp.tv_sec;
		msec = (double) (tp.tv_usec / MICRO_IN_SEC);

		if (msec >= 1.0) {
			msec -= (long) msec;
		}
		return msec + sec;
	}
#endif
	return 0;
}

/**
 * Bind the current process to a specified CPU. This function is to ensure that
 * the OS won't schedule the process to different processors, which would make
 * values read by rdtsc unreliable.
 *
 * @param int cpu_id, the id of the logical cpu to be bound to.
 * @return int, 0 on success, and -1 on failure.
 *
 * @author cjiang
 */
int xdebug_bind_to_cpu(int cpu_id) {
  cpu_set_t new_mask;

  CPU_ZERO(&new_mask);
  CPU_SET(cpu_id, &new_mask);

  if (SET_AFFINITY(0, sizeof(cpu_set_t), &new_mask) < 0) {
    perror("setaffinity");
    return -1;
  }

  /* record the cpu_id the process is bound to. */
  XG(cpu_cur_id) = cpu_id;

  return 0;
}

/**
 * Convert from TSC counter values to equivalent microseconds.
 *
 * @param long count, TSC count value
 * @param double cpu_frequency, the CPU clock rate (MHz)
 * @return 64 bit unsigned integer
 *
 * @author cjiang
 */
inline double xdebug_get_us_from_tsc(long count, double cpu_frequency) {
  return count / cpu_frequency;
}
/**
 * Get time delta in microseconds.
 */
static long xdebug_get_us_interval(struct timeval *start, struct timeval *end) {
  return (((end->tv_sec - start->tv_sec) * 1000000)
          + (end->tv_usec - start->tv_usec));
}

/**
 * This is a microbenchmark to get cpu frequency the process is running on. The
 * returned value is used to convert TSC counter values to microseconds.
 *
 * @return double.
 * @author cjiang
 */
static double xdebug_get_cpu_frequency(int cpu) {
    struct timeval start;
    struct timeval end;
    long tsc_start, tsc_end;

    if (xdebug_cputime_funcs.bind_to_cpu(cpu) != 0) {
        /* bailout */
        fprintf(stderr, "Cannot bind to CPU %d\n", cpu);
        return 0.0;
    }

    if (gettimeofday(&start, 0)) {
        perror("gettimeofday");
        return 0.0;
    }
    tsc_start = xdebug_cycle_timer();
    /* Sleep for 5 miliseconds. Comparaing with gettimeofday's  few microseconds
     * execution time, this should be enough. */
    usleep(5000);
    if (gettimeofday(&end, 0)) {
        perror("gettimeofday");
        return 0.0;
    }
    tsc_end = xdebug_cycle_timer();
    return (tsc_end - tsc_start) * 1.0 / (xdebug_get_us_interval(&start, &end));
}

/**
 * Return the current cputime
 */
double xdebug_get_cputime(void)
{
	if (!XG(profiler_cputime) || !xdebug_cputime_funcs.get_cputime) {
		return 0.0;
	}
	xdebug_cputime_funcs.get_cputime();
}

/**
 * Linux specific implementaiton based on clock_gettime
 */
double xdebug_linux_get_cputime(void)
{
	unsigned long time;
	double ns;
	struct timespec tsc;

	clock_gettime(XG(cpu_cur_id), &tsc);

	time = ((unsigned long) tsc.tv_sec) * 1000000000 + tsc.tv_nsec;
    return time - XG(cpu_stime);
}

/**
 * Implementation based on rdtsc counter, which is not very accurate.
 */
double xdebug_fallback_get_cputime(void)
{
	unsigned int __a,__d;
	unsigned long time;
	asm volatile("rdtsc" : "=a" (__a), "=d" (__d));
	(time) = ((unsigned long)__a) | (((unsigned long)__d)<<32);

	return xdebug_get_us_from_tsc((time - XG(cpu_stime)), XG(cpu_frequency));
}

char* xdebug_get_time(void)
{
	time_t cur_time;
	char  *str_time;

	str_time = xdmalloc(24);
	cur_time = time(NULL);
	strftime(str_time, 24, "%Y-%m-%d %H:%M:%S", gmtime (&cur_time));
	return str_time;
}

/* not all versions of php export this */
static int xdebug_htoi(char *s)
{
	int value;
	int c;

	c = s[0];
	if (isupper(c)) {
		c = tolower(c);
	}
	value = (c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10) * 16;

	c = s[1];
	if (isupper(c)) {
		c = tolower(c);
	}
	value += c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10;

	return value;
}

/* not all versions of php export this */
int xdebug_raw_url_decode(char *str, int len)
{
	char *dest = str;
	char *data = str;

	while (len--) {
		if (*data == '%' && len >= 2 && isxdigit((int) *(data + 1)) && isxdigit((int) *(data + 2))) {
			*dest = (char) xdebug_htoi(data + 1);
			data += 2;
			len -= 2;
		} else {
			*dest = *data;
		}
		data++;
		dest++;
	}
	*dest = '\0';
	return dest - str;
}

static unsigned char hexchars[] = "0123456789ABCDEF";

char *xdebug_raw_url_encode(char const *s, int len, int *new_length, int skip_slash)
{
	register int x, y;
	unsigned char *str;

	str = (unsigned char *) xdmalloc(3 * len + 1);
	for (x = 0, y = 0; len--; x++, y++) {
		str[y] = (unsigned char) s[x];
		if ((str[y] < '0' && str[y] != '-' && str[y] != '.' && (str[y] != '/' || !skip_slash)) ||
			(str[y] < 'A' && str[y] > '9' && str[y] != ':') ||
			(str[y] > 'Z' && str[y] < 'a' && str[y] != '_' && (str[y] != '\\' || !skip_slash)) ||
			(str[y] > 'z'))
		{
			str[y++] = '%';
			str[y++] = hexchars[(unsigned char) s[x] >> 4];
			str[y] = hexchars[(unsigned char) s[x] & 15];
		}
	}
	str[y] = '\0';
	if (new_length) {
		*new_length = y;
	}
	return ((char *) str);
}

/* fake URI's per IETF RFC 1738 and 2396 format */
char *xdebug_path_from_url(const char *fileurl TSRMLS_DC)
{
	/* deal with file: url's */
	char dfp[PATH_MAX * 2];
	const char *fp = dfp, *efp = fileurl;
	int l = 0;
#ifdef PHP_WIN32
	int i;
#endif
	char *tmp = NULL, *ret = NULL;;

	memset(dfp, 0, sizeof(dfp));
	strncpy(dfp, efp, sizeof(dfp) - 1);
	xdebug_raw_url_decode(dfp, strlen(dfp));
	tmp = strstr(fp, "file://");

	if (tmp) {
		fp = tmp + 7;
		if (fp[0] == '/' && fp[2] == ':') {
			fp++;
		}
		ret = xdstrdup(fp);
		l = strlen(ret);
#ifdef PHP_WIN32
		/* convert '/' to '\' */
		for (i = 0; i < l; i++) {
			if (ret[i] == '/') {
				ret[i] = '\\';
			}
		}
#endif
	} else {
		ret = xdstrdup(fileurl);
	}

	return ret;
}

/* fake URI's per IETF RFC 1738 and 2396 format */
char *xdebug_path_to_url(const char *fileurl TSRMLS_DC)
{
	int l, i, new_len;
	char *tmp = NULL;
	char *encoded_fileurl;

	/* encode the url */
	encoded_fileurl = xdebug_raw_url_encode(fileurl, strlen(fileurl), &new_len, 1);

	if (strncmp(fileurl, "phar://", 7) == 0) {
		/* ignore, phar is cool */
		tmp = xdebug_sprintf("dbgp://%s", fileurl);
	} else if (fileurl[0] != '/' && fileurl[0] != '\\' && fileurl[1] != ':') {
		/* convert relative paths */
		cwd_state new_state;
		char cwd[MAXPATHLEN];
		char *result;

		result = VCWD_GETCWD(cwd, MAXPATHLEN);
		if (!result) {
			cwd[0] = '\0';
		}

		new_state.cwd = strdup(cwd);
		new_state.cwd_length = strlen(cwd);

		if(!virtual_file_ex(&new_state, fileurl, NULL, 1)) {
			char *s = estrndup(new_state.cwd, new_state.cwd_length);
			tmp = xdebug_sprintf("file://%s",s);
			efree(s);
		}
		free(new_state.cwd);

	} else if (fileurl[1] == '/' || fileurl[1] == '\\') {
		/* convert UNC paths (eg. \\server\sharepath) */
		/* See http://blogs.msdn.com/ie/archive/2006/12/06/file-uris-in-windows.aspx */
		tmp = xdebug_sprintf("file:%s", encoded_fileurl);
	} else if (fileurl[0] == '/' || fileurl[0] == '\\') {
		/* convert *nix paths (eg. /path) */
		tmp = xdebug_sprintf("file://%s", encoded_fileurl);
	} else if (fileurl[1] == ':') {
		/* convert windows drive paths (eg. c:\path) */
		tmp = xdebug_sprintf("file:///%s", encoded_fileurl);
	} else {
		/* no clue about it, use it raw */
		tmp = xdstrdup(encoded_fileurl);
	}
	l = strlen(tmp);
	/* convert '\' to '/' */
	for (i = 0; i < l; i++) {
		if (tmp[i] == '\\') {
			tmp[i]='/';
		}
	}
	xdfree(encoded_fileurl);
	return tmp;
}

long xdebug_crc32(const char *string, int str_len)
{
	unsigned int crc = ~0;
	int len;
	
	len = 0 ;
	for (len += str_len; str_len--; ++string) {
	    XDEBUG_CRC32(crc, *string);
	}
	return ~crc;
}

#ifndef PHP_WIN32
static FILE *xdebug_open_file(char *fname, char *mode, char *extension, char **new_fname)
{
	FILE *fh;
	char *tmp_fname;

	if (extension) {
		tmp_fname = xdebug_sprintf("%s.%s", fname, extension);
	} else {
		tmp_fname = xdstrdup(fname);
	}
	fh = fopen(tmp_fname, mode);
	if (fh && new_fname) {
		*new_fname = tmp_fname;
	} else {
		xdfree(tmp_fname);
	}
	return fh;
}

static FILE *xdebug_open_file_with_random_ext(char *fname, char *mode, char *extension, char **new_fname)
{
	FILE *fh;
	char *tmp_fname;
	TSRMLS_FETCH();

	if (extension) {
		tmp_fname = xdebug_sprintf("%s.%06x.%s", fname, (long) (1000000 * php_combined_lcg(TSRMLS_C)), extension);
	} else {
		tmp_fname = xdebug_sprintf("%s.%06x", fname, (long) (1000000 * php_combined_lcg(TSRMLS_C)), extension);
	}
	fh = fopen(tmp_fname, mode);
	if (fh && new_fname) {
		*new_fname = tmp_fname;
	} else {
		xdfree(tmp_fname);
	}
	return fh;
}

FILE *xdebug_fopen(char *fname, char *mode, char *extension, char **new_fname)
{
	int   r;
	FILE *fh;
	struct stat buf;
	char *tmp_fname = NULL;

	/* We're not doing any tricks for append mode... as that has atomic writes
	 * anyway. And we ignore read mode as well. */
	if (mode[0] == 'a' || mode[0] == 'r') {
		return xdebug_open_file(fname, mode, extension, new_fname);
	}

	/* In write mode however we do have to do some stuff. */
	/* 1. Check if the file exists */
	if (extension) {
		tmp_fname = xdebug_sprintf("%s.%s", fname, extension);
	} else {
		tmp_fname = xdstrdup(fname);
	}
	r = stat(tmp_fname, &buf);
	/* We're not freeing "tmp_fname" as that is used in the freopen as well. */

	if (r == -1) {
		/* 2. Cool, the file doesn't exist so we can open it without probs now. */
		fh = xdebug_open_file(fname, "w", extension, new_fname);
		goto lock;
	}

	/* 3. It exists, check if we can open it. */
	fh = xdebug_open_file(fname, "r+", extension, new_fname);
	if (!fh) {
		/* 4. If fh == null we couldn't even open the file, so open a new one with a new name */
		fh = xdebug_open_file_with_random_ext(fname, "w", extension, new_fname);
		goto lock;
	}

	/* 5. It exists and we can open it, check if we can exclusively lock it. */
	r = flock(fileno(fh), LOCK_EX | LOCK_NB);
	if (r == -1) {
		if (errno == EWOULDBLOCK) {
			fclose(fh);
			/* 6. The file is in use, so we open one with a new name. */
			fh = xdebug_open_file_with_random_ext(fname, "w", extension, new_fname);
			goto lock;
		}
	}

	/* 7. We established a lock, now we truncate and return the handle */
	fh = freopen(tmp_fname, "w", fh);

lock: /* Yes yes, an evil goto label here!!! */
	if (fh) {
		/* 8. We have to lock again after the reopen as that basically closes
		 * the file and opens it again. There is a small race condition here...
		 */
		flock(fileno(fh), LOCK_EX | LOCK_NB);
	}
	xdfree(tmp_fname);
	return fh;
}
#else
FILE *xdebug_fopen(char *fname, char *mode, char *extension, char **new_fname)
{
	char *tmp_fname;
	FILE *ret;

	if (extension) {
		tmp_fname = xdebug_sprintf("%s.%s", fname, extension);
	} else {
		tmp_fname = xdstrdup(fname);
	}
	ret = fopen(tmp_fname, mode);
	if (new_fname) {
		*new_fname = tmp_fname;
	} else {
		xdfree(tmp_fname);
	}
	return ret;
}
#endif

int xdebug_format_output_filename(char **filename, char *format, char *script_name)
{
	xdebug_str fname = {0, 0, NULL};
	char       cwd[128];
	TSRMLS_FETCH();
	
	while (*format)
	{
		if (*format != '%') {
			xdebug_str_addl(&fname, (char *) format, 1, 0);
		} else {
			format++;
			switch (*format)
			{
				case 'c': /* crc32 of the current working directory */
					VCWD_GETCWD(cwd, 127);
					xdebug_str_add(&fname, xdebug_sprintf("%lu", xdebug_crc32(cwd, strlen(cwd))), 1);
					break;

				case 'p': /* pid */
					xdebug_str_add(&fname, xdebug_sprintf("%ld", getpid()), 1);
					break;
				
				case 'r': /* random number */
					xdebug_str_add(&fname, xdebug_sprintf("%06x", (long) (1000000 * php_combined_lcg(TSRMLS_C))), 1);
					break;

				case 's': { /* script fname */
					char *char_ptr, *script_name_tmp;
				
					/* we do not always have script_name available, so if we
					 * don't have it and this format specifier is used then we
					 * simple do nothing for this specifier */
					if (!script_name) {
						break;
					}

					/* create a copy to work on */
					script_name_tmp = xdstrdup(script_name);

					/* replace slashes, whitespace and colons with underscores */
					while ((char_ptr = strpbrk(script_name_tmp, "/\\: ")) != NULL) {
						char_ptr[0] = '_';
					}
					/* replace .php with _php */
					char_ptr = strrchr(script_name_tmp, '.');
					if (char_ptr) {
						char_ptr[0] = '_';
					}
					xdebug_str_add(&fname, script_name_tmp, 0);
					xdfree(script_name_tmp);
				}	break;

				case 't': { /* timestamp (in seconds) */
					time_t the_time = time(NULL);
					xdebug_str_add(&fname, xdebug_sprintf("%ld", the_time), 1);
				}	break;

				case 'u': { /* timestamp (in microseconds) */
					char *char_ptr, *utime = xdebug_sprintf("%f", xdebug_get_utime());
					
					/* Replace . with _ (or should it be nuked?) */
					char_ptr = strrchr(utime, '.');  
					if (char_ptr) {
						char_ptr[0] = '_';
					}
					xdebug_str_add(&fname, utime, 1);
				}	break;

				case 'H':   /* $_SERVER['HTTP_HOST'] */
				case 'R': { /* $_SERVER['REQUEST_URI'] */
					zval **data;
					char *char_ptr, *strval;
					int retval;

					if (PG(http_globals)[TRACK_VARS_SERVER]) {
						switch (*format) {
						case 'H':
							retval = zend_hash_find(Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_SERVER]), "HTTP_HOST", sizeof("HTTP_HOST"), (void **) &data);
							break;
						case 'R':
							retval = zend_hash_find(Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_SERVER]), "REQUEST_URI", sizeof("REQUEST_URI"), (void **) &data);
							break;
						}

						if (retval == SUCCESS) {
							strval = estrdup(Z_STRVAL_PP(data));
							
							/* replace slashes, dots, question marks, plus signs,
							 * ambersands and spaces with underscores */
							while ((char_ptr = strpbrk(strval, "/\\.?&+ ")) != NULL) {
								char_ptr[0] = '_';
							}
							xdebug_str_add(&fname, strval, 0);
							efree(strval);
						}
					}
				}	break;

				case 'S': { /* session id */
					zval **data;
					char *char_ptr, *strval;
					char *sess_name;
					
					sess_name = zend_ini_string("session.name", sizeof("session.name"), 0);
					
					if (sess_name && PG(http_globals)[TRACK_VARS_COOKIE] &&
						zend_hash_find(Z_ARRVAL_P(PG(http_globals)[TRACK_VARS_COOKIE]), sess_name, strlen(sess_name) + 1, (void **) &data) == SUCCESS &&
						Z_STRLEN_PP(data) < 100 /* Prevent any unrealistically long data being set as filename */
					) {
						strval = estrdup(Z_STRVAL_PP(data));
						
						/* replace slashes, dots, question marks, plus signs,
						 * ampersands and spaces with underscores */
						while ((char_ptr = strpbrk(strval, "/\\.?&+ ")) != NULL) {
							char_ptr[0] = '_';
						}
						xdebug_str_add(&fname, strval, 0);
						efree(strval);
					}
				}	break;

				case '%': /* literal % */
					xdebug_str_addl(&fname, "%", 1, 0);
					break;
			}
		}
		format++;
	}
	
	*filename = fname.d;

	return fname.l;
}

void xdebug_init_cputime_statistics()
{
	int cpu;

	xdebug_cputime_funcs.bind_to_cpu = xdebug_bind_to_cpu;
#ifdef linux
	xdebug_cputime_funcs.get_cputime = xdebug_linux_get_cputime;
#else
	xdebug_cputime_funcs.get_cputime = xdebug_fallback_get_cputime;
#endif

	XG(cpu_num) = sysconf(_SC_NPROCESSORS_ONLN); 
	srandom(time(NULL));
	cpu = random() % XG(cpu_num);
    xdebug_cputime_funcs.bind_to_cpu(cpu);

#if defined (_SC_CLK_TCK)
	XG(cpu_frequency) = sysconf(_SC_CLK_TCK);
#else
	if (XG(cpu_frequency) <= 0) {
        XG(cpu_frequency) = (unsigned long) xdebug_get_cpu_frequency(cpu);
    }
#endif
    if (XG(cpu_frequency) <= 0.0) {
        fprintf(stderr, "Cannot get CPU frequencies.");
        return;
    } 

    XG(cpu_cur_id) = cpu;
    XG(cpu_stime) = xdebug_cputime_funcs.get_cputime();
}
