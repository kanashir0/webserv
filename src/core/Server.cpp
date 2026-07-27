#include "core/Server.hpp"

ListeningSocket::ListeningSocket(const std::string& host,
                                 int port,
                                 const std::vector<ServerConfig>& vhosts,
                                 Router& router,
                                 SessionStore& sessions,
                                 EventLoop& loop)
	: socket_()
	, host_(host)
    , port_(port)
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
short ListeningSocket::interest() const      {
	return POLLIN;
	// return 0;

}

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
	for (size_t i = 0; i < configs_.size(); i++) {
		for (size_t j = 0; j < listeners_.size(); j++) {
			if (configs_[i].host == listeners_[j]->getHost() && configs_[i].port == listeners_[j]->getPort())
				listeners_[j]->addServer(configs_[i]);
			else {
				break ;
			}
		}

		try {
			std::vector<ServerConfig> vhost;
			vhost.push_back(configs_[i]);

			ListeningSocket* listener = new ListeningSocket(configs_[i].host, configs_[i].port, vhost, router_, sessions_, loop_);
			std::ostringstream oss;
			oss << "SOCKET OUVINDO NA PORT: " << configs_[i].port;
			LOG_WARN(oss.str());

			listeners_.push_back(listener);

			loop_.add(listener);
		}
		catch (const std::exception& e) {
			std::cout << "FALHA EM BINDAR LISTENEN (SERVIDOR)" << e.what() << std::endl;
		}
	}
	loop_.run();
}

void Server::stop() { loop_.stop(); }

EventLoop&    Server::loop()     { return loop_; }
SessionStore& Server::sessions() { return sessions_; }

void        ListeningSocket::addServer(ServerConfig& config) {
	vhosts_.push_back(config);
}

std::string ListeningSocket::getHost() {
	return host_;
}

int ListeningSocket::getPort() {
	return port_;
}
