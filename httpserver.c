#include "httpserver.h"
#include "httpsocket.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

// --------------------------------------------------------------------------------

struct client_t {
	int64_t socket;
	struct sockaddr_in addr;
	char *hostname;
	time_t timeout;
	bool terminate;
	struct client_t *next;
};

// --------------------------------------------------------------------------------

#define CONNECTION_TIMEOUT 120

static bool initialized = false;
static int64_t host_socket = -1;
static handle_request_t request_handler = NULL;
static struct client_t *first_connection;

static char message[100000];

static const char *messages[] = {
	"200 OK", // HTTP_200_OK
	"400 Bad Request", // HTTP_400_BAD_REQUEST
	"401 Unauthorized", // HTTP_401_UNAUTHORIZED
	"404 Not Found", // HTTP_404_NOT_FOUND
};

// --------------------------------------------------------------------------------

static void http_server_process(void);
static void http_server_process_client(struct client_t *client);
static void http_server_send_response(struct client_t *client, const struct http_response_t *response);

// --------------------------------------------------------------------------------

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

	// Terminate all active connections.
	for (struct client_t *client = first_connection, *tmp;
		client != NULL;
		client = tmp)
	{
		tmp = client->next;

		if (client->socket >= 0) {
			close(client->socket);
		}

		free(client->hostname);
		free(client);
	}

	http_socket_shutdown();

	initialized = false;
}

void http_server_listen(void)
{
	if (!initialized) {
		return;
	}

	// Create an fd_set for the collection of sockets to listen to.
	fd_set set;
	FD_ZERO(&set);
	FD_SET(host_socket, &set);

	// Also find the highest socket descriptor value.
	int64_t highest = host_socket;

	for (struct client_t *client = first_connection;
		client != NULL;
		client = client->next) {

		FD_SET(client->socket, &set);

		if (client->socket > highest) {
			highest = client->socket;
		}
	}

	time_t now = time(NULL);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	// Process all active sockets for incoming connectiosn and/or requests.
	if (select((int)(highest + 1), &set, NULL, NULL, &timeout) > 0) {
		
		// Listen to the server socket for new incoming connections.
		if (FD_ISSET(host_socket, &set)) {
			http_server_process();
		}
		
		// Process all active client connections.
		for (struct client_t *client = first_connection, *previous = NULL, *tmp;
			client != NULL;
			previous = client, client = tmp)
		{
			tmp = client->next;

			if (FD_ISSET(client->socket, &set)) {
				http_server_process_client(client);
			}
			
			// If the client times out or disconnects itself, terminate it.
			if (client->timeout < now ||
				client->terminate) {

				// Update the list.
				if (client == first_connection) {
					first_connection = client->next;
				}
				else if (previous != NULL) {
					previous->next = client->next;
				}

				// Close the connection.
				if (client->socket >= 0) {
					close(client->socket);
				}

				// Free data.
				free(client->hostname);
				free(client);
			}
			else {
				previous = client;
			}
		}
	}
}

static void http_server_process(void)
{
	struct client_t *client = malloc(sizeof(*client));
	memset(client, 0, sizeof(*client));

	// Accept a new connection.
	socklen_t addr_len = sizeof(client->addr);
	client->socket = accept(host_socket, (struct sockaddr *)&client->addr, &addr_len);

	if (client < 0) {

		// Connection could not be made. Discard the client data.
		free(client);
		client = NULL;
	}
	else {
		// Make the client socket non-blocking.
		http_socket_set_non_blocking(client->socket);

		// Set a default timeout value. We're assuming HTTP/1.1 protocol where clients want to keep the connection open.
		client->timeout = time(NULL) + CONNECTION_TIMEOUT;

		// Copy the hostname of the client.
		char ip[INET_ADDRSTRLEN], host[1024];
		getnameinfo(&client->addr, addr_len, host, sizeof(host), ip, sizeof(ip), 0);

		size_t host_len = strlen(host);
		client->hostname = malloc(host_len + 1);
		strcpy(client->hostname, host);

		// Add the client to the list of active connections.
		client->next = first_connection;
		first_connection = client;
	}
}

static void http_server_process_client(struct client_t *client)
{
	int received = recv(client->socket, message, sizeof(message) - 1, 0);
	
	// Receiving the request from the client failed.
	if (received < 0) {
		client->terminate = true;
		return;
	}

	// Client connection was terminated unexpectedly.
	if (received == 0) {
		client->terminate = true;
		return;
	}
	
	message[received] = 0;

	// Parse the request and respond to it.
	struct http_request_t request;

	request.method = strtok(message, " \t\n");
	request.hostname = client->hostname;

	// Out web server handles only GET requests currently.
	if (strncmp(request.method, "GET\0", 4) == 0 ||
		strncmp(request.method, "POST\0", 5) == 0) {

		request.request = strtok(NULL, " \t");
		request.protocol = strtok(NULL, " \t\n\r");

		// We only support HTTP 1.0/1.1 right now.
		if (strncmp(request.protocol, "HTTP/1.0", 8) != 0 &&
			strncmp(request.protocol, "HTTP/1.1", 8) != 0) {

			struct http_response_t failure;
			failure.message = HTTP_400_BAD_REQUEST;
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

	// Extend the timeout value.
	client->timeout = time(NULL) + CONNECTION_TIMEOUT;
}

static void http_server_send_response(struct client_t *client, const struct http_response_t *response)
{
	if (response->message >= NUM_MESSAGES) {
		return;
	}

	// Write the header.
	const char *origin = "Access-Control-Allow-Origin: *\n";

	char buffer[1024];
	int len = snprintf(buffer, sizeof(buffer), "HTTP/1.1 %s\n", messages[response->message]);

	send(client->socket, buffer, len, 0);

	len = snprintf(buffer, sizeof(buffer), "Connection: keep-alive\n");
	send(client->socket, buffer, len, 0);

	// Tell the client to not cache our response.
	len = snprintf(buffer, sizeof(buffer), "Cache-Control: max-age = 0, no-cache, must-revalidate, proxy-revalidate\n");
	send(client->socket, buffer, len, 0);

	// Write the content if there is any.
	if (response->content != NULL && response->content_type != NULL) {

		size_t content_len = strlen(response->content);

		len = snprintf(buffer, sizeof(buffer), "Content-Length: %u\n", (uint32_t)content_len);
		send(client->socket, buffer, len, 0);

		len = snprintf(buffer, sizeof(buffer), "Content-Type: %s\n", response->content_type);
		send(client->socket, buffer, len, 0);

		len = snprintf(buffer, sizeof(buffer), "%s\n", origin);
		send(client->socket, buffer, len, 0);

		send(client->socket, response->content, (int)content_len, 0);
	}
	else {
		send(client->socket, origin, strlen(origin), 0);
	}
}
