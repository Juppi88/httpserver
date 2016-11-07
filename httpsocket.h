#pragma once
#ifndef __HTTPSOCKET_H
#define __HTTPSOCKET_H

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
#endif

void http_socket_initialize(void);
void http_socket_shutdown(void);

#endif
