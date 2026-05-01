#ifndef WEBSERV_HTTP_HANDLERS_IMETHOD_HANDLER_HPP
#define WEBSERV_HTTP_HANDLERS_IMETHOD_HANDLER_HPP

#include "http/Request.hpp"
#include "http/Response.hpp"
#include "config/ServerConfig.hpp"
#include "config/LocationConfig.hpp"

namespace ws {

class IMethodHandler {
public:
	virtual ~IMethodHandler() {}

	virtual Response handle(const Request& req,
	                        const LocationConfig& loc,
	                        const ServerConfig& srv) = 0;
};

}

#endif
