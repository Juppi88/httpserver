#pragma once
#ifndef __HTTPUTILS_H
#define __HTTPUTILS_H

#include <stddef.h>

void string_get_file_extension(const char *str, char* buffer, size_t buffer_len);
char *string_parse_header_text(char *str, char **header, char **value);

#endif
