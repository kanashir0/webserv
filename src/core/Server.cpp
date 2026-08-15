#include "core/Server.hpp"

ListeningSocket::ListeningSocket(const std::string& host,
                                 int port,
                                 const std::vector<ServerConfig>& vhosts,
                                 Router& router,
                                 EventLoop& loop)
	: socket_()
	, host_(host)
    , port_(port)
	, vhosts_(vhosts)
	, router_(router)
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

		if (client_fd < 0)
			break;

		Client* client = new Client(
			client_fd,
			vhosts_,
			router_,
			loop_
		);

		loop_.add(client);
	}
}

void  ListeningSocket::onWritable()          {}
void  ListeningSocket::onHangup()            {}

bool  ListeningSocket::wantsClose() const    {
	return false;
}

Server::Server(const std::vector<ServerConfig>& configs, Router& router, SessionStore& sessions)
	: configs_(configs)
	, sessions_(sessions)
	, router_(router)
	, loop_()
{}

Server::~Server() {}

void Server::start() {
	std::map<Endpoint, std::vector<ServerConfig> > groups_;

	for (size_t i = 0; i < configs_.size(); i++) {
		Endpoint key(configs_[i].host, configs_[i].port);

		groups_[key].push_back(configs_[i]);
	}

	for (std::map<Endpoint, std::vector<ServerConfig> >::iterator it = groups_.begin();
         it != groups_.end(); it++) {

		ListeningSocket* listener = new ListeningSocket(it->first.first, it->first.second, it->second, router_, loop_);

		std::ostringstream oss;
		oss << "SOCKET OUVINDO NA PORT: " << it->first.second;
		LOG_INFO(oss.str());

		// O EventLoop assume a posse do listener e o deleta no destrutor.
		loop_.add(listener);
	}
	// GC das sessoes a cada tick do loop, no store que o Router realmente usa.
	loop_.setTickHandler(&sessions_);
	loop_.run();
}

void Server::stop() {
	loop_.stop();
}

EventLoop&    Server::loop()     {
	return loop_;
}

void        ListeningSocket::checkTimeout(time_t now, time_t timeout) {
	(void)now;
	(void)timeout;
}
