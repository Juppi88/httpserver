#include "httpsocket.h"

#ifdef _WIN32

void http_socket_initialize(void)
{
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
}

void http_socket_shutdown(void)
{
	WSACleanup();
}

void http_socket_set_non_blocking(socket_t sock)
{
	u_long mode = 1;
	ioctlsocket(sock, FIONBIO, &mode);
}

#else

void http_socket_initialize(void) {}
void http_socket_shutdown(void) {}

void http_socket_set_non_blocking(socket_t sock)
{
	int flags = fcntl(sock, F_GETFL, 0);

	if (flags < 0) {
		return;
	}

	flags |= O_NONBLOCK;
	fcntl(sock, F_SETFL, flags);
}

#endif

int http_socket_write_all(socket_t sock, const void *buffer, size_t length)
{
	const char *p = buffer;
	ssize_t sent;

	while (length > 0) {

		sent = write(sock, p, length);

		if (sent <= 0) {
			return -1;
		}

		p += sent;
		length -= sent;
	}

	return 0;
}
