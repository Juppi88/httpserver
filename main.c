#include "httpserver.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <Windows.h>
#define SLEEP(x) Sleep(x)
#else
#define SLEEP(x) usleep(1000 * x)
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

	printf("method: %s, protocol: %s, request: %s\n", request->method, request->protocol, request->request);

	return response;
}

int main(int argc, char *argv[])
{
	uint16_t port = 8080;

	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "--port") && ++i < argc) {
			port = (uint16_t)atoi(argv[i]);
			break;
		}
	}

	if (port <= 0 || port > 0xFFFF) {
		printf("Invalid port!\n");
		return 0;
	}

	if (!http_server_initialize(port, handle_request)) {
		printf("Failed to start the server!\n");
		return 0;
	}

	printf("Started a http server on port %u\n", port);

	for (;;) {
		http_server_listen();
		SLEEP(100);
	}

	http_server_shutdown();
	return 0;
}
