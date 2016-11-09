#pragma once
#ifndef __HTTPSERVER_H
#define __HTTPSERVER_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_CONNECTIONS 128

#ifdef __cplusplus
extern "C" {
#endif

enum HttpMessage {
	HTTP_200_OK,
	HTTP_400_BAD_REQUEST,
	HTTP_401_UNAUTHORIZED,
	HTTP_404_NOT_FOUND,
	NUM_MESSAGES
};

struct http_request_t {
	const char *method;
	const char *request;
	const char *protocol;
};

struct http_response_t {
	enum HttpMessage message;
	const char *content;
	const char *content_type;
};

typedef struct http_response_t (*handle_request_t)(struct http_request_t *request);

extern bool http_server_initialize(uint16_t port, handle_request_t handler);
extern void http_server_shutdown(void);
extern void http_server_listen(void);

#ifdef __cplusplus
}
#endif

#endif
