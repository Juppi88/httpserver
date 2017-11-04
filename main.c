#include "httpserver.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static struct http_response_t handle_request(struct http_request_t *request)
{
	struct http_response_t response;
	memset(&response, 0, sizeof(response));

	if (strcmp(request->request, "/test") == 0) {
		response.message = HTTP_200_OK;
		response.content = "<html><body><h1>:) Everything seems to work!</h1></body></html>\n";
	}
	else {
		response.message = HTTP_404_NOT_FOUND;
		response.content = "<html><body><h1>Whoops, 404!</h1></body></html>\n";

	}

	response.content_type = "text/html";
	response.content_length = strlen(response.content);

	printf("%s request: IP: %s, request: %s\n", request->method, request->requester, request->request);

	return response;
}

int main(int argc, char *argv[])
{
	uint16_t port = 80;
	const char *static_directory = NULL;

	for (int i = 0; i < argc; ++i) {
		if (strcmp(argv[i], "--port") == 0 && ++i < argc) {
			port = (uint16_t)atoi(argv[i]);
			break;
		}

		else if (strcmp(argv[i], "--path") == 0 && ++i < argc) {
			static_directory = argv[i];
			break;
		}
	}

	if (port <= 0 || port > 0xFFFF) {
		printf("Invalid port (%u)!\n", port);
		return 0;
	}

	// Initialize the web interface.
	struct server_directory_t directories[] = { { "/", "" } };

	struct server_settings_t settings;
	memset(&settings, 0, sizeof(settings));

	settings.handler = handle_request;
	settings.port = port;
	settings.timeout = 10;
	settings.max_connections = 10;
	settings.connection_timeout = 60;

	// Set a folder to serve static content from.
	if (static_directory != NULL) {
		directories[0].directory = static_directory;

		settings.directories = directories;
		settings.directories_len = 1;
	}

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
