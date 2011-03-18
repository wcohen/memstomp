/*
  A hacky replacement for backtrace_symbols in glibc

  backtrace_symbols in glibc looks up symbols using dladdr which is limited in
  the symbols that it sees. libbacktracesymbols opens the executable and shared
  libraries using libbfd and will look up backtrace information using the symbol
  table and the dwarf line information.

  It may make more sense for this program to use libelf instead of libbfd.
  However, I have not investigated that yet.

  Derived from addr2line.c from GNU Binutils by Jeff Muizelaar

  Copyright 2007 Jeff Muizelaar
*/

/* addr2line.c -- convert addresses to line number and function name
   Copyright 1997, 1998, 1999, 2000, 2001, 2002 Free Software Foundation, Inc.
   Contributed by Ulrich Lauther <Ulrich.Lauther@mchp.siemens.de>

   This file was part of GNU Binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#define fatal(a, b) exit(1)
#define bfd_fatal(a) exit(1)
#define bfd_nonfatal(a) exit(1)
#define list_matching_formats(a) exit(1)

/* 2 characters for each byte, plus 1 each for 0, x, and NULL */
#define PTRSTR_LEN (sizeof(void *) * 2 + 3)
#define true 1
#define false 0

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <bfd.h>
#include <libiberty.h>
#include <dlfcn.h>
#include <link.h>
#if 0

void (*dbfd_init)(void);
bfd_vma (*dbfd_scan_vma)(const char *string, const char **end, int base);
bfd* (*dbfd_openr)(const char *filename, const char *target);
bfd_boolean (*dbfd_check_format)(bfd *abfd, bfd_format format);
bfd_boolean (*dbfd_check_format_matches)(bfd *abfd, bfd_format format, char ***matching);
bfd_boolean (*dbfd_close)(bfd *abfd);
bfd_boolean (*dbfd_map_over_sections)(bfd *abfd, void (*func)(bfd *abfd, asection *sect, void *obj),
		void *obj);
#define bfd_init dbfd_init

static void load_funcs(void)
{
	void * handle = dlopen("libbfd.so", RTLD_NOW);
	dbfd_init = dlsym(handle, "bfd_init");
	dbfd_scan_vma = dlsym(handle, "bfd_scan_vma");
	dbfd_openr = dlsym(handle, "bfd_openr");
	dbfd_check_format = dlsym(handle, "bfd_check_format");
	dbfd_check_format_matches = dlsym(handle, "bfd_check_format_matches");
	dbfd_close = dlsym(handle, "bfd_close");
	dbfd_map_over_sections = dlsym(handle, "bfd_map_over_sections");
}

#endif


/* Read in the symbol table.  */

static asymbol **slurp_symtab(bfd *const abfd)
{
	asymbol **syms = NULL;

	if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0)
		return syms;

	unsigned int size;
	long symcount = bfd_read_minisymbols(abfd, false, (PTR) & syms, &size);
	if (symcount == 0)
		symcount = bfd_read_minisymbols(abfd, true /* dynamic */ ,
						(PTR) & syms, &size);

	if (symcount < 0)
		bfd_fatal(bfd_get_filename(abfd));

	return syms;
}

typedef struct {
	asymbol **syms;		/* Symbol table.  */
	bfd_vma pc;
	char const *filename;
	char const *functionname;
	unsigned int line;
	int found;  /* result is valid */
} bmos_arg;

/* Look for an address in a section.  This is called via
   bfd_map_over_sections.  */

static void find_address_in_section(
	bfd *const abfd,
	asection *const section,
	void *const data
)
{
	bmos_arg *const arg = (bmos_arg *)data;

	if (arg->found)
		return;

	if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
		return;

	bfd_vma const vma = bfd_get_section_vma(abfd, section);
	if (arg->pc < vma)
		return;

	bfd_size_type const size = bfd_section_size(abfd, section);
	if (arg->pc >= vma + size)
		return;

	arg->found = bfd_find_nearest_line(abfd, section, arg->syms,
		arg->pc - vma, &arg->filename, &arg->functionname, &arg->line);
}

static char ** translate_addresses_vec(
	bfd *const abfd,
	bfd_vma const addr[],
	int naddr
)
{
	char **ret_buf = NULL;
	bmos_arg arg; memset(&arg, 0, sizeof(arg));
	arg.syms = slurp_symtab(abfd);

	/* iterate over the formating twice.
	 * the first time we count how much space we need
	 * the second time we do the actual printing */
	int const naddr_orig = naddr;
	char b;  /* space for terminating '\0' in Count state */
	char *buf = &b;
	int len = 0;  /* snprintf limit for *buf */
	int total  = 0;

	enum { Count, Print } state;
	for (state=Count; state<=Print; state++) {
	if (state == Print) {
		naddr = naddr_orig;
		ret_buf = malloc(total + sizeof(char *)*naddr);
		buf = (char *)(ret_buf + naddr);
		len = total;
	}
	while (--naddr >= 0) {
		if (state == Print)
			ret_buf[naddr] = buf;
		arg.pc = addr[naddr];

		arg.found = false;
		bfd_map_over_sections(abfd, find_address_in_section, &arg);

		if (!arg.found) {
			total += 1+ snprintf(buf, len,
				"[%p] \?\?() \?\?:0",(void *)addr[naddr]);
		} else {
			char const *name = arg.functionname;
			if (name == NULL || *name == '\0')
				name = "\?\?";
			if (arg.filename != NULL) {
				char *h;

				h = strrchr(arg.filename, '/');
				if (h != NULL)
					arg.filename = h + 1;
			}
			total += snprintf(buf, len, "%s:%u\t%s()",
				(arg.filename ? arg.filename : "\?\?"),
			       arg.line, name) + 1;

		}
		if (state == Print) {
			/* set buf just past the end of string */
			buf += total + 1;
		}
	}
	}

	if (arg.syms != NULL) {
		free(arg.syms);
	}
	return ret_buf;
}
/* Process a file.  */

static char **process_file(
	char const *const file_name,
	bfd_vma const addr[],
	int const naddr
)
{
	bfd *const abfd = bfd_openr(file_name, NULL);

	if (abfd == NULL)
		bfd_fatal(file_name);

	if (bfd_check_format(abfd, bfd_archive))
		fatal("%s: can not get addresses from archive", file_name);

	char **matching;
	if (!bfd_check_format_matches(abfd, bfd_object, &matching)) {
		bfd_nonfatal(bfd_get_filename(abfd));
		if (bfd_get_error() ==
		    bfd_error_file_ambiguously_recognized) {
			list_matching_formats(matching);
			free(matching);
		}
		xexit(1);
	}

	char **const ret_buf = translate_addresses_vec(abfd, addr, naddr);
	bfd_close(abfd);
	return ret_buf;
}

#define MAX_DEPTH 16

struct file_match {  /* INput or OUTput w.r.t. find_matching_file() */
	char const *file;     /* OUT */
	void const *address;  /* IN */
	void const *base;     /* OUT */
	ElfW(Phdr) const *phdr;     /* OUT */
};

static int find_matching_file(struct dl_phdr_info *const info,
		size_t const size, void *const data)
{
	struct file_match *const match = data;
	/* This code is modeled from Gfind_proc_info-lsb.c:callback() from libunwind */
	ElfW(Addr) const load_base = info->dlpi_addr;
	ElfW(Phdr) const *phdr = info->dlpi_phdr;
	int n;
	for (n = info->dlpi_phnum; --n >= 0; phdr++) {
		if (phdr->p_type == PT_LOAD) {
			ElfW(Addr) const vaddr = phdr->p_vaddr + load_base;
			if (match->address >= (void *) vaddr
			&&  match->address <  (void *)((char *) vaddr + phdr->p_memsz)) {
				/* we found a match */
				match->file = info->dlpi_name;
				match->base = (void *) info->dlpi_addr;
				match->phdr = phdr;
				return 1;  /* first match is good enough */
			}
		}
	}
	return 0;  /* keep looking */
}

char **backtrace_symbols(void /*const*/ *const vector[], int const length)
{
	/* discard calling function */
	int const stack_depth = length - 1;

	int total = 0;
	char **locations[1+ stack_depth];  /* c99 or gcc extension */

	bfd_init();
	int x;
	for (x=stack_depth; x>=0; x--) {
		struct file_match match; memset(&match, 0, sizeof(match));
		match.address = vector[x];
		dl_iterate_phdr(find_matching_file, &match);
		bfd_vma const addr = (char *) vector[x] - (char *) match.base;
		if (match.file && strlen(match.file))
			locations[x] = process_file(match.file, &addr, 1);
		else
			locations[x] = process_file("/proc/self/exe", &addr, 1);
		total += strlen(locations[x][0]) + 1;
	}

	/* allocate the array of char * we are going to return and extra space for
	 * all of the strings */
	char **const final = malloc(total + (stack_depth + 1) * sizeof(char *));
	/* get a pointer to the extra space */
	char *f_strings = (char *)(final + stack_depth + 1);

	/* fill in all of strings and pointers */
	for (x=stack_depth; x>=0; x--) {
		strcpy(f_strings, locations[x][0]);
		free(locations[x]);
		final[x] = f_strings;
		f_strings += strlen(f_strings) + 1;
	}

	return final;
}
