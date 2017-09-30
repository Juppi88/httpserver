#include "httputils.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

void string_get_file_extension(const char *str, char* buffer, size_t buffer_len)
{
	// Initialize the output as empty string.
	*buffer = 0;

	size_t len = strlen(str);

	// If the length of the filename is 0, it has no extension.
	if (len == 0) {
		return;
	}

	for (int64_t i = len - 1; i >= 0; --i) {

		char c = str[i];

		// Could not find extension or the extension is too long!
		if (len - i >= buffer_len) {
			return;
		}

		// Extension must be valid alphanumeric characters.
		if (!isalnum(c) && c != '.') {
			return;
		}

		// Found the beginning of the extension, copy it into the buffer.
		if (c == '.') {
			strcpy(buffer, &str[i]);
			return;
		}
	}
}
