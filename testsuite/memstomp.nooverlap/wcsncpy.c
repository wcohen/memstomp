#define _GNU_SOURCE
#include <string.h>
#include <wchar.h>

#ifndef WCSNCPY
#define WCSNCPY wcsncpy
#endif


wchar_t arr1[32] = L"this is a test";
wchar_t arr2[32] = L"this is a test";
wchar_t *p1 = &arr1[0];
wchar_t *p2 = &arr2[1];
main ()
{
  WCSNCPY (p1, p2, wcslen (p2));
}


