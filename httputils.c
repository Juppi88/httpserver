#include "httputils.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
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

static char *string_remove_leading_white_space(char* str)
{
	if (str == NULL) {
		return NULL;
	}

	for (;;) {
		switch (*str) {
		case ' ':
		case '\n':
		case '\r':
		case '\t':
			*str++ = '\0';
			continue;

		case '\0':
			return NULL;
		}
		break;
	}

	return str;
}

static char *string_skip_non_white_space(char *str, bool skip_until_new_line)
{
	if (str == NULL) {
		return NULL;
	}

	for (;;) {
		switch (*str) {
		case ' ':
		case '\t':
			if (!skip_until_new_line) {
				return str;
			}
			break;

		case '\n':
		case '\r':
			return str;

		case '\0':
			return NULL;
		}

		++str;
	}

	return str;
}

static char *string_skip_past_line_break(char* str)
{
	if (str == NULL) {
		return NULL;
	}

	for (;;) {
		switch (*str) {
		case '\0':
			return NULL;

		case '\r':
		case '\n':
			++str;

			if (*str == '\n' || *str == '\r') {
				++str;
			}

			return str;
		}

		++str;
	}

	return str;
}

char *string_parse_header_text(char *str, char **header, char **value)
{
	// Remove all leading white space. The starting string is the header.
	str = string_remove_leading_white_space(str);

	*header = str;

	// Skip the header text and possible white space after it, which gets to the header value.
	str = string_skip_non_white_space(str, false);
	str = string_remove_leading_white_space(str);

	*value = str;

	// Skip the header value and terminate the string.
	str = string_skip_non_white_space(str, true);
	str = string_skip_past_line_break(str);

	return str;
}
