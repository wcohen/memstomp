#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef MEMCCPY
#define MEMCCPY memccpy
#define TYPE char
#endif

TYPE arr[10] = {0};
TYPE *p1 = &arr[0];
TYPE *p2 = &arr[1];
size_t count = 9;

int
main ()
{
  MEMCCPY (p1, p2, -1, count);
}


