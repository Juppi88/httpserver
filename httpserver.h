#pragma once
#ifndef __HTTPSERVER_H
#define __HTTPSERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --------------------------------------------------------------------------------

enum http_message_t {
	HTTP_200_OK = 200,
	HTTP_400_BAD_REQUEST = 400,
	HTTP_401_UNAUTHORIZED = 401,
	HTTP_404_NOT_FOUND = 404,
};

struct http_request_t {
	const char *requester;		// Hostname of the client who performed the request
	const char *method;			// The method used by the client. Currently either 'GET' or 'POST' is recognised
	const char *request;		// Path to the resource requested by the client
};

struct http_response_t {
	enum http_message_t message; // Request status code (see the enum above)
	const char *content;		// Content to be delivered to the client
	const char *content_type;	// MIME type of the content
	size_t content_length;		// Content length when the response data is binary. For text should be left to 0
};

typedef struct http_response_t(*handle_request_t)(struct http_request_t *request);

struct server_ssettings_t {
	handle_request_t handler;		// Handler method for custom requests (such as dynamic data in JSON format)

	uint16_t port;					// Port this web server is listening on
	uint16_t max_connections;		// Maximum connections this web server can handle simultaneously
	uint32_t timeout;				// Socket polling timeout in milliseconds (can be left to zero)
	uint32_t connection_timeout;	// Connection timeout in seconds for clients who want to keep the connection alive between requests

	struct server_directory_t {		// List of directories containing static files
		const char *path;				// The URL path which links to this directory entry
		const char *directory;			// Actual directory from which to serve the files
	} *directories;
	
	size_t directories_len;			// Number of items on the list above
};

// --------------------------------------------------------------------------------

extern bool http_server_initialize(struct server_ssettings_t configuration);
extern void http_server_shutdown(void);
extern void http_server_listen(void);

// --------------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif
