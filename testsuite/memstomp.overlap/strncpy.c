#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef STRNCPY
#define STRNCPY strncpy
#endif


char arr[32] = "this is a test";
char *p1 = &arr[0];
char *p2 = &arr[1];
main ()
{
  STRNCPY (p1, p2, strlen (p2));
}


