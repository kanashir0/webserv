#ifndef WEBSERV_HTTP_HANDLERS_DELETE_HANDLER_HPP
#define WEBSERV_HTTP_HANDLERS_DELETE_HANDLER_HPP

#include "http/handlers/IMethodHandler.hpp"

namespace ws {

class DeleteHandler : public IMethodHandler {
public:
	DeleteHandler();
	~DeleteHandler();

	Response handle(const Request& req,
	                const LocationConfig& loc,
	                const ServerConfig& srv);
};

}

#endif
