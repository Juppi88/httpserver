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
#else
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <fcntl.h>
#endif

void http_socket_initialize(void);
void http_socket_shutdown(void);
void http_socket_set_non_blocking(int64_t sock);

#endif
