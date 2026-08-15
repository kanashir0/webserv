#ifndef WEBSERV_HTTP_ROUTER_HPP
#define WEBSERV_HTTP_ROUTER_HPP

#include "http/Request.hpp"
#include "http/Response.hpp"
#include "http/handlers/GetHandler.hpp"
#include "http/handlers/PostHandler.hpp"
#include "http/handlers/DeleteHandler.hpp"
#include "config/ServerConfig.hpp"
#include <vector>


class SessionStore;

struct CgiTarget {
	const LocationConfig* loc;
	std::string           interpreter;
	std::string           scriptPath;

	CgiTarget();
	bool active() const;
};

class Router {
public:
	Router(SessionStore& sessions);
	~Router();

	Response route(const Request& req, const ServerConfig& vhost, CgiTarget& cgi);

private:
	SessionStore& sessions_;

	GetHandler    getH_;
	PostHandler   postH_;
	DeleteHandler deleteH_;

	bool     methodAllowed(const std::string& method, const LocationConfig& loc) const;
	Response prepareCgi(const Request& req,
	                    const LocationConfig& loc,
	                    const ServerConfig& vhost,
	                    const std::string& interpreter,
	                    CgiTarget& cgi);
	// Bonus: pagina de demonstracao de sessao servida pelo proprio servidor.
	Response handleSession(const Request& req);

	Router(const Router&);
	Router& operator=(const Router&);
};


#endif
