#include "httpserver.h"
#include "httpsocket.h"
#include "httputils.h"
#include <string.h>
#include <stdio.h>
#include <malloc.h>

static bool initialized = false;
static int64_t host_socket = -1;
static handle_request_t request_handler = NULL;

static char message[100000];
static char file_buffer[100000];

static const char *messages[] = {
	"200 OK", // HTTP_200_OK
	"400 Bad Request", // HTTP_400_BAD_REQUEST
	"401 Unauthorized", // HTTP_401_UNAUTHORIZED
	"404 Not Found", // HTTP_404_NOT_FOUND
};


struct file_dir_entry_t {
	char *path;
	char *directory;
	size_t path_len;
	struct file_dir_entry_t *next;
};

static struct file_dir_entry_t *first_dir = NULL;

// --------------------------------------------------------------------------------

static void http_server_send_response(int64_t client, const struct http_response_t *response);
static bool http_server_handle_static_file(int64_t client, const struct http_request_t *request);

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

	char ip[INET_ADDRSTRLEN], host[1024];
	getnameinfo((struct sockaddr *)&client_addr, addr_len, host, sizeof(host), ip, sizeof(ip), 0);

	request.method = strtok(message, " \t\n");
	request.hostname = host;

	// The library only serves GET and POST request.
	if (strncmp(request.method, "GET\0", 4) == 0 ||
		strncmp(request.method, "POST\0", 5) == 0) {

		request.request = strtok(NULL, " \t");
		request.protocol = strtok(NULL, " \t\n\r");

		// Only HTTP 1.0/1.1 are supported right now.
		if (strncmp(request.protocol, "HTTP/1.0", 8) != 0 &&
			strncmp(request.protocol, "HTTP/1.1", 8) != 0) {

			struct http_response_t failure;
			failure.message = HTTP_400_BAD_REQUEST;
			failure.content = NULL;
			failure.content_type = NULL;

			http_server_send_response(client, &failure);
		}

		else if (!http_server_handle_static_file(client, &request)) {

			// If the request was not requesting anything from a static content path,
			// let the user of this library handle the request as they see fit.
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

	// Tell the client to not cache our response.
	len = snprintf(buffer, sizeof(buffer), "Cache-Control: max-age=0, no-cache, must-revalidate, proxy-revalidate\n");
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

static bool http_server_handle_static_file(int64_t client, const struct http_request_t *request)
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
	if (strstr(file_name, "..") != NULL) {
		return false;
	}

	// Interpret an empty file name or a slash as index.html.
	if (file_name[0] == 0 || (file_name[0] == '/' && file_name[1] == 0)) {
		file_name = "index.html";
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/%s", dir->directory, file_name);

	FILE *file = fopen(path, "r");

	// Requested file does not exist or it can't be opened.
	if (file == NULL) {
		return false;
	}

	// Create a response.
	struct http_response_t response;
	memset(&response, 0, sizeof(response));

	response.message = HTTP_200_OK;

	// Set the correct MIME type for the requested file.
	if (string_ends_with(req_path, ".html")) {
		response.content_type = "text/html";
	}
	else if (string_ends_with(req_path, ".css")) {
		response.content_type = "text/css";
	}
	else if (string_ends_with(req_path, ".js")) {
		response.content_type = "application/javascript";
	}
	else {
		response.content_type = "text/plain";
	}

	// Get the size of the file.
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	// Read the contents of the file into a buffer which we can send to the requester.
	if (length >= sizeof(file_buffer)) {
		length = sizeof(file_buffer) - 1;
	}

	size_t read = fread(file_buffer, 1, length, file);
	fclose(file);

	file_buffer[read] = 0;
	response.content = file_buffer;

	// Send a response along with the requested file.
	http_server_send_response(client, &response);

	return true;
}

void http_server_add_static_directory(const char *path, const char *directory)
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
