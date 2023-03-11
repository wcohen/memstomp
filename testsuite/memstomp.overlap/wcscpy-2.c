#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef WCSCPY
#define WCSCPY wcscpy
#endif


wchar_t arr[32] = L"this is a test";
wchar_t *p1 = &arr[0];
wchar_t *p2 = &arr[1];

int
main ()
{
  WCSCPY (p2, p1);
}


