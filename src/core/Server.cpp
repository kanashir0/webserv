#include "core/Server.hpp"

ListeningSocket::ListeningSocket(const std::string& host,
                                 int port,
                                 const std::vector<ServerConfig>& vhosts,
                                 Router& router,
                                 SessionStore& sessions,
                                 EventLoop& loop)
	: socket_()
	, vhosts_(vhosts)
	, router_(router)
	, sessions_(sessions)
	, loop_(loop)
{
	socket_.bindAndListen(host, port);
	socket_.setNonBlocking(fd());
}

ListeningSocket::~ListeningSocket() {}

int   ListeningSocket::fd() const            { return socket_.fd(); }
short ListeningSocket::interest() const      { return 0; }

void  ListeningSocket::onReadable() {
	while (true) {
		int client_fd = socket_.acceptConnection();

		if (client_fd < 0) {
			if (errno == EAGAIN)
				break;
		}

		Client* client = new Client(
			client_fd,
			vhosts_,
			router_,
			sessions_
		);

		loop_.add(client);
	}
}

void  ListeningSocket::onWritable()          {}
void  ListeningSocket::onHangup()            {}
bool  ListeningSocket::wantsClose() const    { return false; }

Server::Server(const std::vector<ServerConfig>& configs, Router& router)
	: configs_(configs)
	, loop_()
	, listeners_()
	, sessions_()
	, router_(router)
{}

Server::~Server() {
	for (std::vector<ListeningSocket*>::iterator it = listeners_.begin();
	     it != listeners_.end(); ++it) {
		delete *it;
	}
}

void Server::start() {
	// TODO Membro 1: agrupar configs por (host,port), criar ListeningSocket por endpoint
}

void Server::stop() { loop_.stop(); }

EventLoop&    Server::loop()     { return loop_; }
SessionStore& Server::sessions() { return sessions_; }

void Server::groupConfigsByEndpoint() {
	// TODO Membro 1
}

