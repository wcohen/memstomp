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

#include <pthread.h>
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
#define ABRT_TRAP raise(SIGSEGV)

static bool abrt_trap = false;

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

static volatile unsigned n_broken = 0;
static volatile unsigned n_collisions = 0;
static volatile unsigned n_self_contended = 0;


static void (*real_exit)(int status) __attribute__((noreturn)) = NULL;
static void (*real__exit)(int status) __attribute__((noreturn)) = NULL;
static void (*real__Exit)(int status) __attribute__((noreturn)) = NULL;
static int (*real_backtrace)(void **array, int size) = NULL;
static char **(*real_backtrace_symbols)(void *const *array, int size) = NULL;
static void (*real_backtrace_symbols_fd)(void *const *array, int size, int fd) = NULL;

static __thread bool recursive = false;

static volatile bool initialized = false;
static volatile bool threads_existing = false;

static void setup(void) __attribute ((constructor));
static void shutdown(void) __attribute ((destructor));

static pid_t _gettid(void) {
        return (pid_t) syscall(SYS_gettid);
}

static const char *get_prname(void) {
        static char prname[17];
        int r;

        r = prctl(PR_GET_NAME, prname);
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

        recursive = true;

        LOAD_FUNC(exit);
        LOAD_FUNC(_exit);
        LOAD_FUNC(_Exit);

        LOAD_FUNC(backtrace);
        LOAD_FUNC(backtrace_symbols);
        LOAD_FUNC(backtrace_symbols_fd);

        loaded = true;
        recursive = false;
}

static void setup(void) {

        load_functions();

        if (LIKELY(initialized))
                return;

        if (!dlsym(NULL, "main"))
                fprintf(stderr,
                        "memstomp: Application appears to be compiled without -rdynamic. It might be a\n"
                        "memstomp: good idea to recompile with -rdynamic enabled since this produces more\n"
                        "memstomp: useful stack traces.\n\n");

        if (getenv("MEMSTOMP_KILL"))
                abrt_trap = true;

        initialized = true;

        fprintf(stderr, "memstomp: "PACKAGE_VERSION" sucessfully initialized for process %s (pid %lu).\n",
                get_prname(), (unsigned long) getpid());
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

static char* generate_stacktrace(void) {
        char **strings, *ret, *p;
        int n, i;
        size_t k;
        bool b;

        void *buffer[frames_max];  /* c99 or gcc extension */

        n = real_backtrace(buffer, frames_max);
        assert(n >= 0);

        strings = real_backtrace_symbols(buffer, n);
        assert(strings);

        k = 0;
        for (i = 0; i < n; i++)
                k += strlen(strings[i]) + 2;

        ret = malloc(k + 1);
        assert(ret);

        b = false;
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

int backtrace(void **array, int size) {
        int r;

        load_functions();

        /* backtrace() internally uses a mutex. To avoid an endless
         * loop we need to disable ourselves so that we don't try to
         * call backtrace() ourselves when looking at that lock. */

        recursive = true;
        r = real_backtrace(array, size);
        recursive = false;

        return r;
}

char **backtrace_symbols(void *const *array, int size) {
        char **r;

        load_functions();

        recursive = true;
        r = real_backtrace_symbols(array, size);
        recursive = false;

        return r;
}

void backtrace_symbols_fd(void *const *array, int size, int fd) {
        load_functions();

        recursive = true;
        real_backtrace_symbols_fd(array, size, fd);
        recursive = false;
}

static void warn_memcpy(void * dest, const void * src, size_t count)
{
	fprintf(stderr, "memcpy(%p, %p, %ld) overlap for %s(%d)\n",
		dest, src, count, get_prname(), getpid());
	/* generate stack backtrace */
	char *const info = generate_stacktrace();
	fprintf(stderr, "%s", info);
	free(info);
}


void * memcpy(void * dest, const void * src, size_t count)
{
	size_t distance = (dest > src) ? ((char *)dest - (char *)src)
		: ((char *) src - (char *) dest);
	
	/* Check for overlap. */
	if (distance < count) {
		if (abrt_trap) ABRT_TRAP;
		/* report the overlap */
		warn_memcpy(dest, src, count);
	}
	/* be safe use memmove */
	return memmove(dest, src, count);
}
