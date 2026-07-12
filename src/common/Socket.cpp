#include "common/Socket.hpp"


Socket::Socket() : fd_(-1) {}

Socket::~Socket() {}

void Socket::bindAndListen(const std::string& /*host*/, int /*port*/, int /*backlog*/) {
	// TODO Membro 1: socket() + setsockopt SO_REUSEADDR + bind() + listen()
}

int Socket::accept(struct sockaddr_in& /*outAddr*/) {
	// TODO Membro 1
	return -1;
}

void Socket::setNonBlocking(int /*fd*/) {
	// TODO Membro 1: fcntl(fd, F_SETFL, O_NONBLOCK)
}

int Socket::fd() const { return fd_.get(); }

int Socket::release() { return fd_.release(); }

