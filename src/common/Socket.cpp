#include "common/Socket.hpp"


Socket::Socket() : fd_(-1) {}

Socket::~Socket() {}

void Socket::bindAndListen(const std::string& host, int port, int backlog) {
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		throw std::runtime_error("SOCKER FALHOU");
	}
	fd_.reset(socket_fd);

	int opt = 1;
	setsockopt(fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	this->setNonBlocking(fd());

	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);

	socklen_t addrlen = sizeof(addr);
	if (bind(fd(), (sockaddr *)&addr, addrlen) < 0) {
		throw std::runtime_error("BIND FALHOU");
	}

	if (listen(fd(), backlog) < 0) {
		throw std::runtime_error("LISTEN FALHOU");
	}
}

int Socket::acceptConnection() {
	sockaddr_in client;
	socklen_t clientlen = sizeof(client);

	int client_fd = accept(fd(), (sockaddr*)&client, &clientlen);
	if (client_fd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return -1;
		throw std::runtime_error("ACCEPT FALHOU");
	}

	FileDescriptor accepted_fd(client_fd);
	this->setNonBlocking(accepted_fd.get());

	return accepted_fd.release();
}

void Socket::setNonBlocking(int server_fd) {
	if (fcntl(server_fd, F_SETFL, O_NONBLOCK) < 0) {
		throw std::runtime_error("FCNTL FALHOU");
	}
}

int Socket::fd() const {
	return fd_.get();
}

int Socket::release() {
	return fd_.release();
}

