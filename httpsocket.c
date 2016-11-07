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

#else

void http_socket_initialize(void) {}
void http_socket_shutdown(void) {}

#endif
