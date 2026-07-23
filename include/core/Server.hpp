#ifndef WEBSERV_CORE_SERVER_HPP
#define WEBSERV_CORE_SERVER_HPP

#include "core/EventLoop.hpp"
#include "core/IPollable.hpp"
#include "core/Client.hpp"
#include "common/Socket.hpp"
#include "config/ServerConfig.hpp"
#include "session/SessionStore.hpp"
#include <vector>


class Router;

class ListeningSocket : public IPollable {
public:
	ListeningSocket(const std::string& host,
	                int port,
	                const std::vector<ServerConfig>& vhosts,
	                Router& router,
	                SessionStore& sessions,
	                EventLoop& loop);
	~ListeningSocket();

	int   fd() const;
	short interest() const;
	void  onReadable();
	void  onWritable();
	void  onHangup();
	bool  wantsClose() const;

	void        addServer(ServerConfig& config);

	std::string getHost();
	int         getPort();

private:
	Socket                           socket_;
	std::string                      host_;
	int                              port_;
	std::vector<ServerConfig>        vhosts_;
	Router&                          router_;
	SessionStore&                    sessions_;
	EventLoop&                       loop_;

	ListeningSocket(const ListeningSocket&);
	ListeningSocket& operator=(const ListeningSocket&);
};

class Server {
public:
	Server(const std::vector<ServerConfig>& configs, Router& router);
	~Server();

	void start();
	void stop();

	EventLoop&    loop();
	SessionStore& sessions();

private:
	std::vector<ServerConfig>     configs_;
	EventLoop                     loop_;
	std::vector<ListeningSocket*> listeners_;
	SessionStore                  sessions_;
	Router&                       router_;

	Server(const Server&);
	Server& operator=(const Server&);
};


#endif
