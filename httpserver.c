#include "httpserver.h"
#include "httpsocket.h"
#include <string.h>
#include <stdio.h>

static bool initialized = false;
static int64_t host_socket = -1;
static handle_request_t request_handler = NULL;

static char message[100000];

static const char *messages[] = {
	"200 OK", // HTTP_200_OK
	"400 Bad Request", // HTTP_400_BAD_REQUEST
	"401 Unauthorized", // HTTP_401_UNAUTHORIZED
	"404 Not Found", // HTTP_404_NOT_FOUND
};

// --------------------------------------------------------------------------------

static void http_server_send_response(int64_t client, const struct http_response_t *response);

bool http_server_initialize(uint16_t port, handle_request_t handler)
{
	initialized = true;
	host_socket = -1;

	// Initialize sockets. On Windows this initializes WinSock.
	http_socket_initialize();

	// Get address info for the host.
	struct addrinfo hints, *res, *p;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	char service[8];
	snprintf(service, sizeof(service), "%u", port);

	if (getaddrinfo(NULL, service, &hints, &res) != 0) {
		http_server_shutdown();
		return false;
	}

	// Create a socket for the host and bind it to the address.
	for (p = res; p != NULL; p = p->ai_next) {
		host_socket = socket(p->ai_family, p->ai_socktype, 0);

		if (host_socket < 0) {
			continue;
		}

		if (bind(host_socket, p->ai_addr, (int)p->ai_addrlen) == 0) {
			break;
		}
	}

	// Could not create a socket or bind failed!
	if (p == NULL || host_socket < 0) {
		http_server_shutdown();
		return false;
	}

	freeaddrinfo(res);

	// Start listening for incoming connections.
	if (listen(host_socket, MAX_CONNECTIONS) != 0) {
		http_server_shutdown();
		return false;
	}

	int opt = 3;
	setsockopt(host_socket, SOL_SOCKET, SO_RCVLOWAT, &opt, sizeof(opt));

	request_handler = handler;
	return true;
}

void http_server_shutdown(void)
{
	if (!initialized) {
		return;
	}

	if (host_socket != -1) {
		shutdown(host_socket, SHUT_RDWR);
		close(host_socket);

		host_socket = -1;
	}

	http_socket_shutdown();

	initialized = false;
}

void http_server_listen(void)
{
	if (!initialized) {
		return;
	}

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 1000;

	fd_set set;
	FD_ZERO(&set);
	FD_SET(host_socket, &set);

	// Listen to the server socket for new incoming connections.
	if (select((int)(host_socket + 1), &set, NULL, NULL, &timeout) <= 0) {
		return;
	}
	
	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);

	int64_t client = accept(host_socket, (struct sockaddr *)&client_addr, &addr_len);

	if (client < 0) {
		return;
	}

	http_socket_set_non_blocking(client);

	int received = recv(client, message, sizeof(message) - 1, 0);

	// Receiving the request from the client failed.
	if (received < 0) {
		goto terminate;
	}

	// Client connection was terminated unexpectedly.
	if (received == 0) {
		goto terminate;
	}

	message[received] = 0;
	
	// Parse the request and respond to it.
	struct http_request_t request;

	request.method = strtok(message, " \t\n");

	// Out web server handles only GET requests currently.
	if (strncmp(request.method, "GET\0", 4) == 0) {

		request.request = strtok(NULL, " \t");
		request.protocol = strtok(NULL, " \t\n\r");

		// We only support HTTP 1.0/1.1 right now.
		if (strncmp(request.protocol, "HTTP/1.0", 8) != 0 &&
			strncmp(request.protocol, "HTTP/1.1", 8) != 0) {

			struct http_response_t failure;
			failure.message = HTTP_400_BAD_REQUEST;;
			failure.content = NULL;
			failure.content_type = NULL;

			http_server_send_response(client, &failure);
		}
		else {
			// Let the user of this library handle the request as they see fit.
			if (request_handler != NULL) {
				struct http_response_t response = request_handler(&request);
				http_server_send_response(client, &response);
			}
		}
	}

terminate:

	// Terminate the client connection.
	close(client);
}

static void http_server_send_response(int64_t client, const struct http_response_t *response)
{
	if (response->message >= NUM_MESSAGES) {
		return;
	}

	// Write the header.
	const char *origin = "Access-Control-Allow-Origin: *\n";

	char buffer[1024];
	int len = snprintf(buffer, sizeof(buffer), "HTTP/1.0 %s\n", messages[response->message]);

	send(client, buffer, len, 0);

	// Write the content if there is any.
	if (response->content != NULL && response->content_type != NULL) {

		size_t content_len = strlen(response->content);

		len = snprintf(buffer, sizeof(buffer), "Content-Length: %u\n", (uint32_t)content_len);
		send(client, buffer, len, 0);

		len = snprintf(buffer, sizeof(buffer), "Content-Type: %s\n", response->content_type);
		send(client, buffer, len, 0);

		len = snprintf(buffer, sizeof(buffer), "%s\n", origin);
		send(client, buffer, len, 0);

		send(client, response->content, (int)content_len, 0);
	}
	else {
		send(client, origin, strlen(origin), 0);
	}
}
