#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef WCSNCPY
#define WCSNCPY wcsncpy
#endif


wchar_t arr[32] = L"this is a test";
wchar_t *p1 = &arr[0];
wchar_t *p2 = &arr[1];

int
main ()
{
  WCSNCPY (p1, p2, wcslen (p2));
}


