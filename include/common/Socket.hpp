#ifndef WEBSERV_COMMON_SOCKET_HPP
#define WEBSERV_COMMON_SOCKET_HPP

#include "common/FileDescriptor.hpp"
#include <string>
#include <netinet/in.h>

namespace ws {

class Socket {
public:
	Socket();
	~Socket();

	void bindAndListen(const std::string& host, int port, int backlog = 128);
	int  accept(struct sockaddr_in& outAddr);
	void setNonBlocking(int fd);

	int  fd() const;
	int  release();

private:
	FileDescriptor fd_;

	Socket(const Socket&);
	Socket& operator=(const Socket&);
};

}

#endif
