#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef STRCPY
#define STRCPY strcpy
#endif


char arr[32] = "this is a test";
char *p1 = &arr[0];
char *p2 = &arr[1];

int
main ()
{
  STRCPY (p2, p1);
}


