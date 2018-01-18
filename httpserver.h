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
	HTTP_201_CREATED = 201,
	HTTP_204_NO_CONTENT = 204,
	HTTP_304_NOT_MODIFIED = 304,
	HTTP_400_BAD_REQUEST = 400,
	HTTP_401_UNAUTHORIZED = 401,
	HTTP_403_FORBIDDEN = 403,
	HTTP_404_NOT_FOUND = 404,
	HTTP_409_CONFLICT = 409,
	HTTP_500_INTERNAL_SERVER_ERROR = 500,
};

struct http_request_t {
	const char *requester;		// IP address of the client who performed the request
	const char *method;			// The method used by the client. Currently 'GET', 'POST', 'PUT' and 'DELETE' are recognised
	const char *request;		// Path to the resource requested by the client
	const char *content;		// Request body, usually used in POST requests
};

struct http_response_t {
	enum http_message_t message; // Request status code (see the enum above)
	const char *content;		// Content to be delivered to the client
	const char *content_type;	// MIME type of the content
	size_t content_length;		// Length for the content to be delivered, in bytes
};

typedef struct http_response_t(*handle_request_t)(struct http_request_t *request, void *context);

struct server_settings_t {
	handle_request_t handler;		// Handler method for custom requests (such as dynamic data in JSON format)

	uint16_t port;					// Port this web server is listening on
	uint16_t max_connections;		// Maximum connections this web server can handle simultaneously
	uint32_t timeout;				// Socket polling timeout in milliseconds (can be left to zero)
	uint32_t connection_timeout;	// Connection timeout in seconds for clients who want to keep the connection alive between requests. 60 seconds is a good value

	struct server_directory_t {		// List of directories containing static files
		const char *path;				// The URL path which links to this directory entry
		const char *directory;			// Actual directory from which to serve the files
	} *directories;
	
	size_t directories_len;			// Number of items on the list above

	void *context;					// User specified context data. Can be NULL.
};

// --------------------------------------------------------------------------------

extern bool http_server_initialize(struct server_settings_t configuration);
extern void http_server_shutdown(void);
extern void http_server_listen(void);

// --------------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif
