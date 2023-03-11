#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef STRTOK_R
#define STRTOK_R strtok_r
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
  return STRTOK_R (NULL, NULL, NULL) == 0 ;
}


