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

int   ListeningSocket::fd() const            {
	return socket_.fd();
}

short ListeningSocket::interest() const      {
	return POLLIN;
}

void  ListeningSocket::onReadable() {
	while (true) {
		int client_fd = socket_.acceptConnection();

		if (client_fd < 0) {
			if (errno == EAGAIN)
				break;
			LOG_ERROR("ACCEPT CLIENT FAIL");
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

bool  ListeningSocket::wantsClose() const    {
	return false;
}

Server::Server(const std::vector<ServerConfig>& configs,  Router& router)
	: configs_(configs)
	, groups_()
	, loop_()
	, listeners_()
	, sessions_()
	, router_(router)
{}

Server::~Server() {}

void Server::start() {
	for (size_t i = 0; i < configs_.size(); i++) {
		Endpoint key(configs_[i].host, configs_[i].port);

		groups_[key].push_back(configs_[i]);
	}

	for (std::map<Endpoint, std::vector<ServerConfig> >::iterator it = groups_.begin();
         it != groups_.end(); it++) {
		try {
			ListeningSocket* listener = new ListeningSocket(it->first.first, it->first.second, it->second, router_, sessions_, loop_);
			std::ostringstream oss;
			oss << "SOCKET OUVINDO NA PORT: " << it->first.second;
			LOG_INFO(oss.str());

			listeners_.push_back(listener);

			loop_.add(listener);
		}
		catch (const std::exception& e) {}
	}
	loop_.setTickHandler(&sessions_);
	loop_.run();
}

void Server::stop() {
	loop_.stop();
}

EventLoop&    Server::loop()     {
	return loop_;
}

SessionStore& Server::sessions() {
	return sessions_;
}

void        ListeningSocket::checkTimeout(time_t now, time_t timeout) {
	(void)now;
	(void)timeout;
}

void        ListeningSocket::addServer(ServerConfig& config) {
	vhosts_.push_back(config);
}

std::string ListeningSocket::getHost() {
	return host_;
}

int ListeningSocket::getPort() {
	return port_;
}
