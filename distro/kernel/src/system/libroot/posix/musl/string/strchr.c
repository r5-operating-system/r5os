#define _DEFAULT_SOURCE
#include <string.h>
#undef strchr

char *strchr(const char *s, int c)
{
	char *r = strchrnul(s, c);
	return *(unsigned char *)r == (unsigned char)c ? r : 0;
}
