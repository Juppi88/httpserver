#include "httpserver.h"
#include "httpsocket.h"
#include "httputils.h"
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <time.h>
#include <signal.h>

// --------------------------------------------------------------------------------

struct client_t {
	socket_t socket;
	struct sockaddr_in addr;
	char *ip_address;
	time_t timeout;
	bool terminate;
	struct client_t *next;
};

// --------------------------------------------------------------------------------

struct file_dir_entry_t {
	char *path;
	char *directory;
	size_t path_len;
	struct file_dir_entry_t *next;
};

// --------------------------------------------------------------------------------

static struct server_settings_t settings;

static bool initialized = false;
static socket_t host_socket = -1;
static struct client_t *first_connection;
static struct file_dir_entry_t *first_dir;

static char message[1000000];
static char file_buffer[1000000];

// --------------------------------------------------------------------------------

static void http_server_add_static_directory(const char *path, const char *directory);
static void http_server_process(void);
static void http_server_process_client(struct client_t *client);
static void http_server_send_response(struct client_t *client, const struct http_response_t *response);
static bool http_server_handle_static_file(struct client_t *client, const struct http_request_t *request);
static const char *http_server_get_message_text(enum http_message_t message);

// --------------------------------------------------------------------------------

bool http_server_initialize(struct server_settings_t configuration)
{
	if (initialized) {
		return false;
	}

	settings = configuration;
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
	snprintf(service, sizeof(service), "%u", settings.port);

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

		// Force the socket to reuse the address even if it's still in use.
		int opt = true;
		setsockopt(host_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

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
	if (listen(host_socket, settings.max_connections) != 0) {
		http_server_shutdown();
		return false;
	}

	int opt = 3;
	setsockopt(host_socket, SOL_SOCKET, SO_RCVLOWAT, (const char *)&opt, sizeof(opt));

	// Add static file locations.
	for (size_t i = 0; i < settings.directories_len; ++i) {
		http_server_add_static_directory(settings.directories[i].path, settings.directories[i].directory);
	}

	// Ignore broken pipe signals, so they can be handled in client processing.
	signal(SIGPIPE, SIG_IGN);

	initialized = true;

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
			shutdown(client->socket, SHUT_RDWR);
			close(client->socket);
		}

		free(client->ip_address);
		free(client);
	}

	http_socket_shutdown();

	// Remove all static file directory entries.
	for (struct file_dir_entry_t *dir = first_dir, *tmp; dir != NULL; dir = tmp) {

		tmp = dir->next;

		free(dir->path);
		free(dir->directory);
		free(dir);
	}

	first_dir = NULL;

	initialized = false;
}

void http_server_listen(void)
{
	if (!initialized) {
		return;
	}

	time_t now = time(NULL);

	// Create a set for the collection of sockets to listen to.
	fd_set set;
	FD_ZERO(&set);
	FD_SET(host_socket, &set);

	// Add the active client sockets to the set and find the highest socket descriptor value.
	// While doing this, terminate all timed out connections.
	socket_t highest = host_socket;

	for (struct client_t *client = first_connection, *previous = NULL, *tmp;
		 client != NULL;
		 client = tmp) {

		tmp = client->next;

		// If the client times out or wants to disconnects itself, terminate it.
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
				shutdown(client->socket, SHUT_RDWR);
				close(client->socket);
			}

			// Free data.
			free(client->ip_address);
			free(client);

			continue;
		}
		else {
			previous = client;
		}

		// Client is not terminated, add the socket to the set.
		FD_SET(client->socket, &set);

		if (client->socket > highest) {
			highest = client->socket;
		}
	}

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 1000L * (long)settings.timeout;

	// Process all active sockets for incoming connections and/or requests.
	if (select((int)(highest + 1), &set, NULL, NULL, &timeout) > 0) {
		
		// Listen to the server socket for new incoming connections.
		if (FD_ISSET(host_socket, &set)) {
			http_server_process();
		}
		
		// Process all active client connections.
		for (struct client_t *client = first_connection;
			client != NULL;
			client = client->next)
		{
			if (FD_ISSET(client->socket, &set)) {
				http_server_process_client(client);
			}
		}
	}
}

static void http_server_add_static_directory(const char *path, const char *directory)
{
	if (path == NULL || directory == NULL) {
		return;
	}

	// Allocate a new static content folder entry and duplicate the path names to it.
	struct file_dir_entry_t *dir = malloc(sizeof(*dir));

	size_t path_len = strlen(path);
	char *path_copy = (char *)malloc(path_len + 1);
	char *directory_copy = (char *)malloc(strlen(directory) + 1);

	if (dir == NULL || path_copy == NULL || directory_copy == NULL) {
		return;
	}

	strcpy(path_copy, path);
	strcpy(directory_copy, directory);

	dir->path = path_copy;
	dir->directory = directory_copy;
	dir->path_len = path_len;
	dir->next = NULL;

	// Add the entry to the list of directories to serve static content from.
	if (first_dir != NULL) {
		dir->next = first_dir;
	}

	first_dir = dir;
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
		client->timeout = time(NULL) + settings.connection_timeout;

		// Store the client's IP address.
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client->addr.sin_addr, ip, sizeof(ip));

		client->ip_address = malloc(strlen(ip) + 1);
		strcpy(client->ip_address, ip);

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
	request.requester = client->ip_address;

	// The library only serves GET and POST request.
	if (strncmp(request.method, "GET\0", 4) == 0 ||
		strncmp(request.method, "POST\0", 5) == 0 ||
		strncmp(request.method, "PUT\0", 4) == 0 ||
		strncmp(request.method, "DELETE\0", 7) == 0) {

		// Parse the requested resource and the used protocol.
		request.request = strtok(NULL, " \t");
		char *protocol = strtok(NULL, " \t\n\r");

		// The rest of the request message is a list of headers and the request body.
		// Read all the headers and find out whether the client wants to keep the connection alive.
		char *header_line = &protocol[strlen(protocol) + 1], *header, *value;
		bool keep_alive = false;
		
		do {
			// Empty line ending in a CRLF indicates the end of headers and the start of request body.
			if (strncmp(header_line, "\r\n", 2) == 0) {
				break;
			}

			// Parse the header line and split it into the header and value strings.
			header_line = string_parse_header_text(header_line, &header, &value);

			// If the name of the header is Connection, check its value.
			if (strcmp(header, "Connection:") == 0) {
				keep_alive = (strcmp(value, "keep-alive") == 0);
			}
		}
		while (header_line != NULL);

		// The rest of the data is the request body preceeded by CRLF.
		request.content = &header_line[2];

		// If the client didn't specify a keep-alive header, terminate the connection after serving the request.
		client->terminate = !keep_alive;

		// Only HTTP 1.1 is supported right now.
		if (strncmp(protocol, "HTTP/1.1", 8) != 0) {

			struct http_response_t failure;
			failure.message = HTTP_400_BAD_REQUEST;
			failure.content = NULL;
			failure.content_type = NULL;

			http_server_send_response(client, &failure);
		}

		else if (!http_server_handle_static_file(client, &request)) {

			// If the request was not requesting anything from a static content path,
			// let the user of this library handle the request as they see fit.
			if (settings.handler != NULL) {
				struct http_response_t response = settings.handler(&request, settings.context);
				http_server_send_response(client, &response);
			}
		}
	}

	// Extend the timeout value.
	client->timeout = time(NULL) + settings.connection_timeout;
}

#define WRITE_VALIDATED(sock, buf, buflen)\
	if (write((sock), (buf), (buflen)) < 0) {\
		client->terminate = true;\
		return;\
	}

static void http_server_send_response(struct client_t *client, const struct http_response_t *response)
{
	// Write the header.
	const char *origin = "Access-Control-Allow-Origin: *\n";

	char buffer[1024];
	int len = snprintf(buffer, sizeof(buffer), "HTTP/1.1 %s\n", http_server_get_message_text(response->message));

	WRITE_VALIDATED(client->socket, buffer, len);

	// Keep the connection alive unless the client wants to terminate it.
	if (!client->terminate) {

		len = snprintf(buffer, sizeof(buffer), "Connection: keep-alive\n");
		WRITE_VALIDATED(client->socket, buffer, len);
	}

	// Tell the client to not cache our response.
	len = snprintf(buffer, sizeof(buffer), "Cache-Control: max-age=0, no-cache, must-revalidate, proxy-revalidate\n");
	WRITE_VALIDATED(client->socket, buffer, len);

	// Write the content if there is any.
	if (response->content != NULL && response->content_type != NULL) {

		size_t content_length = response->content_length;

		// If response length is not set, assume it is plain text and use strlen to calculate its length.
		if (content_length == 0) {
			content_length = strlen(response->content);
		}

		len = snprintf(buffer, sizeof(buffer), "Content-Length: %u\n", (uint32_t)content_length);
		WRITE_VALIDATED(client->socket, buffer, len);

		len = snprintf(buffer, sizeof(buffer), "Content-Type: %s\n", response->content_type);
		WRITE_VALIDATED(client->socket, buffer, len);

		len = snprintf(buffer, sizeof(buffer), "%s\n", origin);
		WRITE_VALIDATED(client->socket, buffer, len);

		// Send the response content. The data may be fragmented, so use a special method which
		// keeps sending data until all of it has been written.
		if (http_socket_write_all(client->socket, response->content, (int)content_length) < 0) {
			client->terminate = true;
			return;
		}
	}
	else {
		WRITE_VALIDATED(client->socket, origin, strlen(origin));
	}
}

static bool http_server_handle_static_file(struct client_t *client, const struct http_request_t *request)
{
	const char *req_path = request->request;
	
	// No directories to serve static data from.
	if (first_dir == NULL) {
		return false;
	}

	// Static data directories have been added, is the requested file inside one of them?
	struct file_dir_entry_t *dir = NULL;

	for (struct file_dir_entry_t *tmp = first_dir; tmp != NULL; tmp = tmp->next) {

		// If the client's request starts with the assigned path for a static file directory, use the directory.
		if (strncmp(tmp->path, req_path, tmp->path_len) == 0) {
			dir = tmp;
			break;
		}
	}

	// Client did not request a file from a valid directory.
	if (dir == NULL) {
		return false;
	}

	const char *file_name = &req_path[dir->path_len];

	// Ignore requests which attempt to access a parent folder for safety reasons.
	//if (strstr(file_name, "..") != NULL) {
	//	return false;
	//}

	char ext[8];
	string_get_file_extension(file_name, ext, sizeof(ext));

	char path[512];

	// Interpret a missing file extension as an index.html for the folder.
	if (*ext == 0) {
		snprintf(path, sizeof(path), "%s/%s/index.html", dir->directory, req_path);
		strcpy(ext, ".html");
	}
	else {
		snprintf(path, sizeof(path), "%s/%s", dir->directory, file_name);
	}

	FILE *file = fopen(path, "rb");

	// Requested file does not exist or it can't be opened.
	if (file == NULL) {
		return false;
	}

	// Get the size of the file.
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (length >= sizeof(file_buffer)) {
		length = sizeof(file_buffer) - 1;
	}

	// Create a response.
	struct http_response_t response;
	memset(&response, 0, sizeof(response));

	response.message = HTTP_200_OK;

	// Set the correct MIME type for the requested file.
	if (strcmp(ext, ".html") == 0) {
		response.content_type = "text/html";
	}
	else if (strcmp(ext, ".css") == 0) {
		response.content_type = "text/css";
	}
	else if (strcmp(ext, ".js") == 0) {
		response.content_type = "application/javascript";
	}
	else if (strcmp(ext, ".png") == 0) {
		response.content_type = "image/png";
	}
	else if (strcmp(ext, ".jpg") == 0) {
		response.content_type = "image/jpeg";
	}
	else if (strcmp(ext, ".gif") == 0) {
		response.content_type = "image/gif";
	}
	else if (strcmp(ext, ".svg") == 0) {
		response.content_type = "image/svg+xml";
	}
	else {
		response.content_type = "text/plain";
	}

	// Read the contents of the file into a buffer which we can send to the requester.
	size_t elements_read = fread(file_buffer, length, 1, file);
	fclose(file);

	response.content_length = elements_read * length;
	response.content = file_buffer;

	// Null terminate the content buffer.
	file_buffer[response.content_length] = 0;
	
	// Send a response along with the requested file.
	http_server_send_response(client, &response);

	return true;
}

static const char *http_server_get_message_text(enum http_message_t message)
{
	switch (message) {

	case HTTP_200_OK:
		return "200 OK";

	case HTTP_201_CREATED:
		return "201 Created";

	case HTTP_204_NO_CONTENT:
		return "204 No Content";

	case HTTP_304_NOT_MODIFIED:
		return "304 Not Modified";

	case HTTP_400_BAD_REQUEST:
		return "400 Bad Request";

	case HTTP_401_UNAUTHORIZED:
		return "401 Unauthorized";

	case HTTP_403_FORBIDDEN:
		return "403 Forbidden";

	case HTTP_404_NOT_FOUND:
		return "404 Not Found";

	case HTTP_409_CONFLICT:
		return "409 Conflict";

	case HTTP_500_INTERNAL_SERVER_ERROR:
		return "500 Internal Server Error";
	}

	return NULL;
}
