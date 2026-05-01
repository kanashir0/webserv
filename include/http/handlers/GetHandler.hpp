#ifndef WEBSERV_HTTP_HANDLERS_GET_HANDLER_HPP
#define WEBSERV_HTTP_HANDLERS_GET_HANDLER_HPP

#include "http/handlers/IMethodHandler.hpp"

namespace ws {

class GetHandler : public IMethodHandler {
public:
	GetHandler();
	~GetHandler();

	Response handle(const Request& req,
	                const LocationConfig& loc,
	                const ServerConfig& srv);

private:
	Response serveFile(const std::string& fsPath, const ServerConfig& srv);
	Response serveDirectory(const std::string& fsPath,
	                        const std::string& uriPath,
	                        const LocationConfig& loc,
	                        const ServerConfig& srv);
};

}

#endif
