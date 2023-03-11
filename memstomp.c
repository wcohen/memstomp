/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
  This file is part of memstomp.

  Copyright 2009 Lennart Poettering
  Copyright 2011 Red Hat

  memstomp is free software: you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  memstomp is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with memstomp. If not, see <http://www.gnu.org/licenses/>.
***/

#include "config.h"

/* Get all #define from cdefs.h, including __restrict and __restrict_arr */
#include <sys/cdefs.h>

/* C99 keyword 'restrict' implies no overlap, thus a really good compiler
 * could remove as superfluous our explicit checks for overlap.
 * Therefore omit 'restrict' for the functions that we check.
 * This file must be compiled using:
 *	gcc -D_GNU_SOURCE -fno-builtin
 */
#undef  __restrict
#define __restrict /*empty*/
#undef  __restrict_arr
#define __restrict_arr /*empty*/

#include <execinfo.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sched.h>

#if !defined (__linux__) || !defined(__GLIBC__)
#error "This stuff only works on Linux!"
#endif

#include <signal.h>
#include <wchar.h>
#define ABRT_TRAP raise(SIGSEGV)

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#undef strcmp
#undef strncmp
#undef strdup
#undef strndup
#undef strchr
#undef strrchr
#undef strcspn
#undef strspn
#undef strpbrk
#undef strtok_r
#undef memcmp

static bool abrt_trap = false;

static bool quiet_mode = false;

#ifndef SCHED_RESET_ON_FORK
/* "Your libc lacks the definition of SCHED_RESET_ON_FORK. We'll now
 * define it ourselves, however make sure your kernel is new
 * enough! */
#define SCHED_RESET_ON_FORK 0x40000000
#endif

#if defined(__i386__) || defined(__x86_64__)
#define DEBUG_TRAP __asm__("int $3")
#else
#include <signal.h>
#define DEBUG_TRAP raise(SIGTRAP)
#endif

#define LIKELY(x) (__builtin_expect(!!(x),1))
#define UNLIKELY(x) (__builtin_expect(!!(x),0))

static unsigned frames_max = 16;

static void (*real_exit)(int status) __attribute__((noreturn)) = NULL;
static void (*real__exit)(int status) __attribute__((noreturn)) = NULL;
static void (*real__Exit)(int status) __attribute__((noreturn)) = NULL;
static int (*real_backtrace)(void **array, int size) = NULL;
static char **(*real_backtrace_symbols)(void *const *array, int size) = NULL;
static void (*real_backtrace_symbols_fd)(void *const *array, int size, int fd) = NULL;

static volatile bool initialized = false;

static void setup(void) __attribute ((constructor));
static void shutdown(void) __attribute ((destructor));

static const char *get_prname(char prname[17]) {
        int const r = prctl(PR_GET_NAME, prname);
        assert(r == 0);
        prname[16] = 0;
        return prname;
}

#if 0
static int parse_env(const char *n, unsigned *t) {
        const char *e;
        char *x = NULL;
        unsigned long ul;

        if (!(e = getenv(n)))
                return 0;

        errno = 0;
        ul = strtoul(e, &x, 0);
        if (!x || *x || errno != 0)
                return -1;

        *t = (unsigned) ul;

        if ((unsigned long) *t != ul)
                return -1;

        return 0;
}
#endif

#define LOAD_FUNC(name)                                                 \
        do {                                                            \
                *(void**) (&real_##name) = dlsym(RTLD_NEXT, #name);     \
                assert(real_##name);                                    \
        } while (false)

#define LOAD_FUNC_VERSIONED(name, version)                              \
        do {                                                            \
                *(void**) (&real_##name) = dlvsym(RTLD_NEXT, #name, version); \
                assert(real_##name);                                    \
        } while (false)

static void load_functions(void) {
        static volatile bool loaded = false;

        if (LIKELY(loaded))
                return;


        LOAD_FUNC(exit);
        LOAD_FUNC(_exit);
        LOAD_FUNC(_Exit);

        LOAD_FUNC(backtrace);
        LOAD_FUNC(backtrace_symbols);
        LOAD_FUNC(backtrace_symbols_fd);

        loaded = true;
}

static void setup(void) {

        load_functions();

        if (LIKELY(initialized))
                return;

        if (getenv("MEMSTOMP_QUIET"))
                quiet_mode = true;

        if (!dlsym(NULL, "main") && !quiet_mode)
                fprintf(stderr,
                        "memstomp: Application appears to be compiled without -rdynamic. It might be a\n"
                        "memstomp: good idea to recompile with -rdynamic enabled since this produces more\n"
                        "memstomp: useful stack traces.\n\n");

        if (getenv("MEMSTOMP_KILL"))
                abrt_trap = true;

        initialized = true;

        if (!quiet_mode) {
                char prname[17];
                fprintf(stderr, "memstomp: "PACKAGE_VERSION" successfully initialized for process %s (pid %lu).\n",
                        get_prname(prname), (unsigned long) getpid());
        }
}

static void show_summary(void) { }

static void shutdown(void) {
        show_summary();
}

void exit(int status) {
        show_summary();
        real_exit(status);
}

void _exit(int status) {
        show_summary();
        real_exit(status);
}

void _Exit(int status) {
        show_summary();
        real__Exit(status);
}

static bool verify_frame(const char *s) {

        /* Generated by glibc's native backtrace_symbols() on Fedora */
        if (strstr(s, "/" SONAME "("))
                return false;

        /* Generated by glibc's native backtrace_symbols() on Debian */
        if (strstr(s, "/" SONAME " ["))
                return false;

        /* Generated by backtrace-symbols.c */
        if (strstr(s, __FILE__":"))
                return false;

        return true;
}

static char* generate_stacktrace(void)
{
        void *retaddr[frames_max];  /* c99 or gcc extension */
        int const n = real_backtrace(retaddr, frames_max);
        assert(n >= 0);

        char **const strings = real_backtrace_symbols(retaddr, n);
        assert(strings);

        char *ret;
	{
		size_t k = 0;
		int i;
		for (i = 0; i < n; i++)
			k += strlen(strings[i]) + 2;

		ret = malloc(k + 1);
	}
        assert(ret);

	char *p;
        int i;
	bool b = false;
        for (i = 0, p = ret; i < n; i++) {
                if (!b && !verify_frame(strings[i]))
                        continue;

                if (!b && i > 0) {
                        /* Skip all but the first stack frame of ours */
                        *(p++) = '\t';
                        strcpy(p, strings[i-1]);
                        p += strlen(strings[i-1]);
                        *(p++) = '\n';
                }

                b = true;

                *(p++) = '\t';
                strcpy(p, strings[i]);
                p += strlen(strings[i]);
                *(p++) = '\n';
        }
        *p = 0;
        free(strings);
        return ret;
}

int backtrace(void **array, int size)
{
        load_functions();
        return real_backtrace(array, size);
}

char **backtrace_symbols(void *const *array, int size)
{
        load_functions();
        return real_backtrace_symbols(array, size);
}

void backtrace_symbols_fd(void *const *array, int size, int fd)
{
        load_functions();
        real_backtrace_symbols_fd(array, size, fd);
}

static unsigned umin(unsigned a, unsigned b)
{
	return (a <= b) ? a : b;
}

static void warn_copylap(void * dest, const void * src, size_t count, char const *name)
{
	char prname[17];
	char buf[160];

/* Avoid fprintf which is not async signal safe.  fprintf may call malloc,
 * which may experience trouble if the bad memcpy was called from a signal
 * handler whose invoking signal interrupted malloc.
 */
	int const j = snprintf(buf, sizeof(buf),
		"\n\n%s(dest=%p, src=%p, bytes=%lu) overlap for %s(%d)\n",
		name, dest, src, count, get_prname(prname), getpid());
	write(STDERR_FILENO, buf, umin(j, sizeof(buf)));
	
/* If generate_stacktrace() indirectly invokes malloc (such as via libbfd),
 * then this is not async signal safe.  But the write() above will produce
 * some evidence before any possible trouble below.
 */
	char *const info = generate_stacktrace();
	fprintf(stderr, "%s", info);
	free(info);
}

static void warn_null(char const *name)
{
	char prname[17];
	char buf[160];

/* Avoid fprintf which is not async signal safe.  fprintf may call malloc,
 * which may experience trouble if the bad memcpy was called from a signal
 * handler whose invoking signal interrupted malloc.
 */
	int const j = snprintf(buf, sizeof(buf),
		"\n\n%s NULL pointer %s(%d)\n",
		name, get_prname(prname), getpid());
	write(STDERR_FILENO, buf, umin(j, sizeof(buf)));
	
/* If generate_stacktrace() indirectly invokes malloc (such as via libbfd),
 * then this is not async signal safe.  But the write() above will produce
 * some evidence before any possible trouble below.
 */
	char *const info = generate_stacktrace();
	fprintf(stderr, "%s", info);
	free(info);
}

static void *copy(void *dest, void const *src, size_t count, char const *name)
{
	size_t d = (char *)dest - (char *)src;
	
	/* Check for overlap. */
	if (unlikely(d < count || -d < count)) {
		if (abrt_trap) ABRT_TRAP;
		/* report the overlap */
		warn_copylap(dest, src, count, name);
	}

	/* be safe use memmove */
	return memmove(dest, src, count);
}

void *memcpy(void *dst, const void *src, size_t count)
{
	return copy(dst, src, count, "memcpy");
}

wchar_t *wmemcpy(wchar_t *dst, wchar_t const *src, size_t n)
{
	return copy(dst, src, sizeof(*src) * n, "wmemcpy");
}

char *strcat(char *dst, char const *src)
{
	copy(&dst[strlen(dst)], src, 1+ strlen(src), "strcat");
	return dst;
}

wchar_t *wcscat(wchar_t *dst, wchar_t const *src)
{
	copy(&dst[wcslen(dst)], src, sizeof(*src) * (1+ wcslen(src)), "wcscat");
	return dst;
}

char *strncat(char *dst, char const *src, size_t n)
{
	char *const join = &dst[strlen(dst)];
	char const *const nulp = memchr(src, 0, n);
	if (!nulp) {
		/* 'restrict' still covers '\0' */
		if (unlikely(&join[n] == src || &src[n] == join))
			warn_copylap(join, src, 1+ n, "strncat");
		copy(join, src, n, "strncat");
		join[n] = '\0';
		return dst;
	}
	/* Check overlap of SRC and '\0' at resulting DST. */
	size_t const lsrc = nulp - src;
	copy(join, src, 1+ lsrc, "strncat");
	join[lsrc] = '\0';
	return dst;
}

wchar_t *wcsncat(wchar_t *dst, wchar_t const *src, size_t n)
{
	wchar_t *const join = &dst[wcslen(dst)];
	wchar_t const *const nulp = wmemchr(src, 0, n);
	if (!nulp) {
		/* 'restrict' still covers L'\0' */
		if (unlikely(&join[n] == src || &src[n] == join))
			warn_copylap(join, src, sizeof(*src) * (1+ n),
				"wcsncat");
		copy(join, src, sizeof(*src) * n, "wcsncat");
		join[n] = L'\0';
		return dst;
	}
	/* Check overlap of SRC and L'\0' at resulting DST. */
	size_t const lsrc = (char const *)nulp - (char const *)src;
	copy(join, src, sizeof(*src) + lsrc, "wcsncat");
	join[lsrc] = L'\0';
	return dst;
}

char *strcpy(char *dst, char const *src)
{
	return copy(dst, src, 1+ strlen(src), "strcpy");
}

wchar_t *wcscpy(wchar_t *dst, wchar_t const *src)
{
	return copy(dst, src, sizeof(*src) * (1+ wcslen(src)), "wcscpy");
}

void *memccpy(void *dst, void const *src, int c, size_t n)
{
	char const *const nulp = memchr(src, c, n);
	if (!nulp) {
		copy(dst, src, n, "memccpy");
		return NULL;
	}
	size_t const n2 = 1+ (nulp - (char const *)src);  /* <= n */
	copy(dst, src, n2, "memccpy");
	return n2 + (char *)dst;  /* after copied c */
}

char *strncpy(char *dst, char const *src, size_t n)
{
	char const *const nulp = memchr(src, 0, n);
	if (!nulp)  /* will be no '\0' terminator on DST */
		return copy(dst, src, n, "strncpy");

	/* Asymmetric case: '\0' fill at end of DST. */
	size_t const lsrc = nulp - src;  /* < n */
	size_t const d = src - dst;
	if ( d < n           /* DST overlaps beginning of SRC. */
	||  -d < (1+ lsrc))  /* SRC overlaps beginning of DST. */
		warn_copylap(dst, src, n, "strncpy");

	/* Could tail merge on memmove by doing memset first,
	 * but doing memmove first is friendlier if overlap.
	 */
	memmove(dst, src, lsrc);
	memset(&dst[lsrc], 0, n - lsrc);
	return dst;
}

wchar_t *wcsncpy(wchar_t *dst, wchar_t const *src, size_t n)
{
	char const *const nulp = (char const *)wmemchr(src, 0, n);
	/* Convert to byte length. */
	n *= sizeof(*src);
	if (!nulp)  /* no '\0' terminator on DST */
		return copy(dst, src, n, "wcsncpy");

	/* Asymmetric case: '\0' fill at end of DST. */
	size_t const lsrc = nulp - (char const *)src;
	size_t const d = (char *)src - (char *)dst;
	if ( d <  n                      /* DST overlaps beginning of SRC. */
	||  -d < (sizeof(*src) + lsrc))  /* SRC overlaps beginning of DST. */
		warn_copylap(dst, src, n, "wcsncpy");

	/* Could tail merge on memmove by doing memset first,
	 * but doing memmove first is friendlier if overlap.
	 */
	memmove(dst, src, lsrc);
	memset(&dst[lsrc], 0, n - lsrc);
	return dst;
}

void *mempcpy(void *dst, void const *src, size_t n)
{
	return n + (char *)copy(dst, src, n, "mempcpy");
}

wchar_t *wmempcpy(wchar_t *dst, wchar_t const *src, size_t n)
{
	size_t const size = sizeof(*src) * n;
	return size + copy(dst, src, size, "wmempcpy");
}

char *stpcpy(char *dst, char const *src)
{
	size_t const len = strlen(src);
	return len + copy(dst, src, 1+ len, "stpcpy");
}

char *stpncpy(char *dst, char const *src, size_t n)
{
	char const *const nulp = memchr(src, 0, n);
	if (!nulp) { /* no '\0' terminator on DST */
		copy(dst, src, n, "stpncpy");
		return &dst[n];
	}

	/* Asymmetric case: '\0' fill at end of DST. */
	size_t const lsrc = nulp - src;
	size_t const d = src - dst;
	if ( d < n           /* DST overlaps beginning of SRC. */
	||  -d < (1+ lsrc))  /* SRC overlaps beginning of DST. */
		warn_copylap(dst, src, n, "stpncpy");
	memmove(dst, src, lsrc);
	return memset(&dst[lsrc], 0, n - lsrc);
}

/* XXX strtok, strtok_r, strsep:  Ugly! */

/* XXX wcstok: Ugly! */

/* All the interposition routines below are just checking for NULL
   arguments when ISO demands they be non-null.  */

void *memmove (void *dest, const void *src, size_t n)
{
  static void * (*real_memmove)(void *, const void *, size_t) = NULL;
  if (!real_memmove)
	real_memmove = dlsym(RTLD_NEXT, "memmove");

  if (unlikely (dest == NULL || src == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("memmove");
	return 0;
  }

  return real_memmove (dest, src, n);
}

int memcmp (const void *__s1, const void *__s2, size_t __n)
{
  static int (*real_memcmp)(const void *, const void *, size_t) = NULL;
  if (!real_memcmp)
	real_memcmp = dlsym(RTLD_NEXT, "memcmp");

  if (unlikely (__s1 == NULL || __s2 == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("memcmp");
	return 0;
  }

  return real_memcmp (__s1, __s2, __n);
}

int strcmp (const char *__s1, const char *__s2)
{
  static int (*real_strcmp)(const char *, const char *) = NULL;
  if (!real_strcmp)
	real_strcmp = dlsym(RTLD_NEXT, "strcmp");

  if (unlikely (__s1 == NULL || __s2 == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strcmp");
	return 0;
  }

  return real_strcmp (__s1, __s2);
}

int strncmp (const char *__s1, const char *__s2, size_t __n)
{
  static int (*real_strncmp)(const char *, const char *, size_t) = NULL;
  if (!real_strncmp)
	real_strncmp = dlsym(RTLD_NEXT, "strncmp");

  if (unlikely (__s1 == NULL || __s2 == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strncmp");
	return 0;
  }

  return real_strncmp (__s1, __s2, __n);
}

int strcoll (const char *__s1, const char *__s2)
{
  static int (*real_strcoll)(const char *, const char *) = NULL;
  if (!real_strcoll)
	real_strcoll = dlsym(RTLD_NEXT, "strcoll");

  if (unlikely (__s1 == NULL || __s2 == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strcoll");
	return 0;
  }

  return real_strcoll (__s1, __s2);
}

int strcoll_l (const char *__s1, const char *__s2, __locale_t __l)
{
  static int (*real_strcoll_l)(const char *, const char *, __locale_t) = NULL;
  if (!real_strcoll_l)
	real_strcoll_l = dlsym(RTLD_NEXT, "strcoll_l");

  if (unlikely (__s1 == NULL || __s2 == NULL || __l == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strcoll_l");
	return 0;
  }

  return real_strcoll_l (__s1, __s2, __l);
}

size_t strxfrm (char *__dest, const char *__src, size_t __n)
{
  static size_t (*real_strxfrm)(char *, const char *, size_t) = NULL;
  if (!real_strxfrm)
	real_strxfrm = dlsym(RTLD_NEXT, "strxfrm");

  if (unlikely (__src == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strxfrm");
	return 0;
  }

  return real_strxfrm (__dest, __src, __n);
}

size_t strxfrm_l (char *__dest, const char *__src, size_t __n, __locale_t __l)
{
  static size_t (*real_strxfrm_l)(char *, const char *, size_t, __locale_t) = NULL;
  if (!real_strxfrm_l)
	real_strxfrm_l = dlsym(RTLD_NEXT, "strxfrm_l");

  if (unlikely (__src == NULL || __l == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strxfrm_l");
	return 0;
  }

  return real_strxfrm_l (__dest, __src, __n, __l);
}


void *memset (void *__s, int __c, size_t __n)
{
  static void * (*real_memset)(void *, int, size_t) = NULL;
  if (!real_memset)
	real_memset = dlsym(RTLD_NEXT, "memset");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("memset");
	return 0;
  }

  return real_memset (__s, __c, __n);
}

void *memchr (const void *__s, int __c, size_t __n)
{
  static void * (*real_memchr)(const void *, int, size_t) = NULL;
  if (!real_memchr)
	real_memchr = dlsym(RTLD_NEXT, "memchr");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("memchr");
	return 0;
  }

  return real_memchr (__s, __c, __n);
}

void *memrchr (const void *__s, int __c, size_t __n)
{
  static void * (*real_memrchr)(const void *, int, size_t) = NULL;
  if (!real_memrchr)
	real_memrchr = dlsym(RTLD_NEXT, "memrchr");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("memrchr");
	return 0;
  }

  return real_memrchr (__s, __c, __n);
}

void *rawmemchr (const void *__s, int __c)
{
  static void * (*real_rawmemchr)(const void *, int, size_t) = NULL;
  if (!real_rawmemchr)
	real_rawmemchr = dlsym(RTLD_NEXT, "rawmemchr");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("rawmemchr");
	return 0;
  }

  return real_rawmemchr (__s, __c, __c);
}

size_t strlen (const char *__s)
{
  static size_t (*real_strlen)(const char *) = NULL;
  if (!real_strlen)
	real_strlen = dlsym(RTLD_NEXT, "strlen");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strlen");
	return 0;
  }

  return real_strlen (__s);
}

char *strdup (const char *__s)
{
  static char *(*real_strdup)(const char *) = NULL;
  if (!real_strdup)
	real_strdup = dlsym(RTLD_NEXT, "strdup");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strdup");
	return 0;
  }

  return real_strdup (__s);
}

char *strndup (const char *__s, size_t __n)
{
  static char *(*real_strndup)(const char *, size_t) = NULL;
  if (!real_strndup)
	real_strndup = dlsym(RTLD_NEXT, "strndup");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strndup");
	return 0;
  }

  return real_strndup (__s, __n);
}

char *strchr (const char *__s, int __c)
{
  static char * (*real_strchr)(const void *, int) = NULL;
  if (!real_strchr)
	real_strchr = dlsym(RTLD_NEXT, "strchr");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strchr");
	return 0;
  }

  return real_strchr (__s, __c);
}

char *strrchr (const char *__s, int __c)
{
  static char * (*real_strrchr)(const void *, int) = NULL;
  if (!real_strrchr)
	real_strrchr = dlsym(RTLD_NEXT, "strrchr");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strrchr");
	return 0;
  }

  return real_strrchr (__s, __c);
}

char *strchrnul (const char *__s, int __c)
{
  static char * (*real_strchrnul)(const void *, int) = NULL;
  if (!real_strchrnul)
	real_strchrnul = dlsym(RTLD_NEXT, "strchrnul");

  if (unlikely (__s == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strchrnul");
	return 0;
  }

  return real_strchrnul (__s, __c);
}

size_t strcspn (const char *__s, const char *__reject)
{
  static size_t (*real_strcspn)(const void *, const char *) = NULL;
  if (!real_strcspn)
	real_strcspn = dlsym(RTLD_NEXT, "strcspn");

  if (unlikely (__s == NULL || __reject == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strcspn");
	return 0;
  }

  return real_strcspn (__s, __reject);
}

size_t strspn (const char *__s, const char *__accept)
{
  static size_t (*real_strspn)(const void *, const char *) = NULL;
  if (!real_strspn)
	real_strspn = dlsym(RTLD_NEXT, "strspn");

  if (unlikely (__s == NULL || __accept == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strspn");
	return 0;
  }

  return real_strspn (__s, __accept);
}

char * strpbrk (const char *__s, const char *__accept)
{
  static char * (*real_strpbrk)(const void *, const char *) = NULL;
  if (!real_strpbrk)
	real_strpbrk = dlsym(RTLD_NEXT, "strpbrk");

  if (unlikely (__s == NULL || __accept == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strpbrk");
	return 0;
  }

  return real_strpbrk (__s, __accept);
}

char * strstr (const char *__haystack, const char *__needle)
{
  static char * (*real_strstr)(const void *, const char *) = NULL;
  if (!real_strstr)
	real_strstr = dlsym(RTLD_NEXT, "strstr");

  if (unlikely (__haystack == NULL || __needle == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strstr");
	return 0;
  }

  return real_strstr (__haystack, __needle);
}

char *strcasestr (const char *__haystack, const char *__needle)
{
  static char * (*real_strcasestr)(const void *, const char *) = NULL;
  if (!real_strcasestr)
	real_strcasestr = dlsym(RTLD_NEXT, "strcasestr");

  if (unlikely (__haystack == NULL || __needle == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strcasestr");
	return 0;
  }

  return real_strcasestr (__haystack, __needle);
}

void *memmem (const void *__haystack, size_t __haystacklen,
	      const void *__needle, size_t __needlelen)
{
  static char * (*real_memmem)(const void *, size_t, const char *, size_t) = NULL;
  if (!real_memmem)
	real_memmem = dlsym(RTLD_NEXT, "memmem");

  if (unlikely (__haystack == NULL || __needle == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("memmem");
	return 0;
  }

  return real_memmem (__haystack, __haystacklen, __needle, __needlelen);
}

char * strtok (char *__s, const char *__delim)
{
  static char * (*real_strtok)(void *, const char *) = NULL;
  if (!real_strtok)
	real_strtok = dlsym(RTLD_NEXT, "strtok");

  if (unlikely (__delim == NULL)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strtok");
	return 0;
  }

  return real_strtok (__s, __delim);
}


char * strtok_r (char *__s, const char *__delim, char **__save_ptr)
{
  static char * (*real_strtok_r)(void *, const char *, char **) = NULL;
  if (!real_strtok_r)
	real_strtok_r = dlsym(RTLD_NEXT, "strtok_r");

  if (unlikely (__delim == NULL || __save_ptr)) {
	if (abrt_trap) ABRT_TRAP;
	/* report the NULL pointer */
	warn_null("strtok_r");
	return 0;
  }

  return real_strtok_r (__s, __delim, __save_ptr);
}

