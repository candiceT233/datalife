#ifndef UNIXIO_H_
#define UNIXIO_H_
#include <dlfcn.h>
#include <stdio.h>

typedef int (*unixopen_t)(const char *path, int flags, ...);
typedef int (*unixclose_t)(int fd);
typedef int (*unixopenat_t)(int dirfd, const char *pathname, int flags, ...);

typedef ssize_t (*unixread_t)(int fd, void *buf, size_t count);
typedef ssize_t (*unixwrite_t)(int fd, const void *buf, size_t count);

typedef ssize_t (*unixlseek_t)(int fd, off_t offset, int whence);
typedef off64_t (*unixlseek64_t)(int fd, off64_t offset, int whence);

// Define the function pointer type for lstat
typedef int (*unixlstat_t)(int version, const char *filename, struct stat *buf); // candice

typedef int (*unixxstat_t)(int version, const char *filename, struct stat *buf);
typedef int (*unixxstat64_t)(int version, const char *filename, struct stat64 *buf);

typedef int (*unixfsync_t)(int fd);
typedef int (*unixfdatasync_t)(int fd);

typedef FILE *(*unixfopen_t)(__const char *__restrict __filename, __const char *__restrict __modes);
typedef int (*unixfclose_t)(FILE *fp);

typedef size_t (*unixfread_t)(void *ptr, size_t size, size_t nmemb, FILE *stream);
typedef size_t (*unixfwrite_t)(const void *ptr, size_t size, size_t nmemb, FILE *stream);

typedef long int (*unixftell_t)(FILE *__stream);
typedef int (*unixfseek_t)(FILE *__stream, long int __off, int __whence);
typedef void (*unixrewind_t)(FILE *__stream);

typedef int (*unixfgetc_t)(FILE *fp);
typedef char *(*unixfgets_t)(char *__restrict s, int n, FILE *__restrict fp);
typedef int (*unixfputc_t)(int __c, FILE *__stream);
typedef int (*unixfputs_t)(const char *__restrict __s, FILE *__restrict __stream);

typedef int (*unixfileno_t)(FILE *fp);
typedef int (*unixflockfile_t)(FILE *fp);
typedef int (*unixftrylockfile_t)(FILE *fp);
typedef int (*unixfunlockfile_t)(FILE *fp);

typedef int (*unixfflush_t)(FILE *fp);
typedef int (*unixfeof_t)(FILE *fp);

typedef ssize_t (*unixreadv_t)(int fd, const struct iovec *iov, int iovcnt);
typedef ssize_t (*unixwritev_t)(int fd, const struct iovec *iov, int iovcnt);

typedef void (*unixexit_t)(int status);
typedef void (*unix_exit_t)(int status);
typedef void (*unix_Exit_t)(int status);
typedef void (*unix_exit_group_t)(int status);

typedef int (*unix_vfprintf_t)(FILE * stream, const char * format, va_list arg);
#endif /* UNIXIO_H_ */
