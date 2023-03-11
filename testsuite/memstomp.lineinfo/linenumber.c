#define __NO_STRING_INLINES
#include <string.h>

void
something (void)
{
  char a[20] = "     hello";
  memcpy (a, a + 5, 6);
}

void
nothing (void)
{
  something ();
}

int
main (void)
{
  nothing ();
  return 0;
}

