#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef MEMSET
#define MEMSET memset
#define TYPE char
#endif

TYPE arr1[10] = {0};
TYPE arr2[10] = {0};
TYPE *p1 = &arr1[0];
TYPE *p2 = &arr2[1];
size_t count = 9;

int
main ()
{
  return MEMSET (NULL, -1, 0) == 0;
}


