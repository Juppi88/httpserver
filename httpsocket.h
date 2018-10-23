#pragma once
#ifndef __HTTPSOCKET_H
#define __HTTPSOCKET_H

#include <stdint.h>

#ifdef _WIN32
	#include <Winsock2.h>
	#include <Windows.h>
	#include <ws2tcpip.h>

	#define close closesocket
	#define SHUT_RDWR 2

	typedef SOCKET socket_t;
#else
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <sys/socket.h>
	#include <sys/select.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <fcntl.h>

	typedef int socket_t;
#endif

void http_socket_initialize(void);
void http_socket_shutdown(void);
void http_socket_set_non_blocking(socket_t sock);
int http_socket_write_all(socket_t sock, const void *buffer, size_t length);

#endif
