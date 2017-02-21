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

void http_socket_set_non_blocking(int64_t sock)
{
	u_long mode = 1;
	ioctlsocket(sock, FIONBIO, &mode);
}

#else

void http_socket_initialize(void) {}
void http_socket_shutdown(void) {}

void http_socket_set_non_blocking(int64_t sock)
{
	int flags = fcntl(sock, F_GETFL, 0);

	if (flags < 0) {
		return;
	}

	flags |= O_NONBLOCK;
	fcntl(sock, F_SETFL, flags);
}

#endif
