#ifndef WEBSERV_COMMON_SOCKET_HPP
# define WEBSERV_COMMON_SOCKET_HPP

#include "common/FileDescriptor.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>


class Socket {
public:
	Socket();
	~Socket();

	void bindAndListen(const std::string& host, int port, int backlog = 128);
	int  acceptConnection(struct sockaddr_in& outAddr);
	void setNonBlocking(int fd);

	int  fd() const;
	int  release();

private:
	FileDescriptor fd_;

	Socket(const Socket&);
	Socket& operator=(const Socket&);
};


#endif
