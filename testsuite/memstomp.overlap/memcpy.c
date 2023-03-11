#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef MEMCPY
#define MEMCPY memcpy
#define TYPE char
#endif

TYPE arr[10] = {0};
TYPE *p1 = &arr[0];
TYPE *p2 = &arr[1];
size_t count = 9;

int
main ()
{
  MEMCPY (p1, p2, count);
}


