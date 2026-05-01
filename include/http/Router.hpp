#ifndef WEBSERV_HTTP_ROUTER_HPP
#define WEBSERV_HTTP_ROUTER_HPP

#include "http/Request.hpp"
#include "http/Response.hpp"
#include "http/handlers/GetHandler.hpp"
#include "http/handlers/PostHandler.hpp"
#include "http/handlers/DeleteHandler.hpp"
#include "config/ServerConfig.hpp"
#include <vector>

namespace ws {

class SessionStore;

class Router {
public:
	Router(const std::vector<ServerConfig>& configs, SessionStore& sessions);
	~Router();

	Response route(const Request& req, const ServerConfig& vhost);

private:
	const std::vector<ServerConfig>& configs_;
	SessionStore&                    sessions_;

	GetHandler    getH_;
	PostHandler   postH_;
	DeleteHandler deleteH_;

	bool methodAllowed(const std::string& method, const LocationConfig& loc) const;
	void attachSessionCookie(const Request& req, Response& resp);

	Router(const Router&);
	Router& operator=(const Router&);
};

}

#endif
