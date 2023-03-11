#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef STRLEN
#define STRLEN strlen
#endif


char arr1[32] = "this is a test";
char arr2[32] = "this is a test";
char *p1 = &arr1[0];
char *p2 = &arr2[1];
main ()
{
  return STRLEN (NULL) == 0;
}


