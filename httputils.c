#include "httputils.h"
#include <stdio.h>
#include <string.h>

bool string_ends_with(const char *str, const char *suffix)
{
	size_t len = strlen(str);
	size_t suffix_len = strlen(suffix);

	return (len >= suffix_len && strcmp(suffix, &str[len - suffix_len]) == 0);
}
