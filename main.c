#include "httpserver.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <Windows.h>
#define SLEEP(x) Sleep(x)
#else
#include <time.h>
#define SLEEP(x)\
	struct timespec t;\
	t.tv_sec = 0;\
	t.tv_nsec = 1000000L * x;\
	nanosleep(&t, NULL)
#endif

static struct http_response_t handle_request(struct http_request_t *request)
{
	struct http_response_t response;
	memset(&response, 0, sizeof(response));

	if (strcmp(request->request, "/") == 0) {
		response.message = HTTP_200_OK;
	}
	else {
		response.message = HTTP_404_NOT_FOUND;
		response.content = "<html><body><h1>Whoops, 404!</h1></body></html>\n";
		response.content_type = "text/html";
	}

	printf("%s request: IP: %s, request: %s\n", request->method, request->requester, request->request);

	return response;
}

int main(int argc, char *argv[])
{
	uint16_t port = 8080;

	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "--port") == 0 && ++i < argc) {
			port = (uint16_t)atoi(argv[i]);
			break;
		}
	}

	if (port <= 0 || port > 0xFFFF) {
		printf("Invalid port (%u)!\n", port);
		return 0;
	}

	// Initialize the web interface.
	struct server_settings_t settings;
	memset(&settings, 0, sizeof(settings));

	settings.handler = handle_request;
	settings.port = port;
	settings.timeout = 100;
	settings.max_connections = 10;
	settings.connection_timeout = 60;

	if (!http_server_initialize(settings)) {
		printf("Failed to start the server!\n");
		return 0;
	}

	printf("Started a HTTP server on port %u\n", port);

	for (;;) {
		http_server_listen();
	}

	http_server_shutdown();
	return 0;
}
