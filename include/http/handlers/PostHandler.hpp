#ifndef WEBSERV_HTTP_HANDLERS_POST_HANDLER_HPP
#define WEBSERV_HTTP_HANDLERS_POST_HANDLER_HPP

#include "http/handlers/IMethodHandler.hpp"

namespace ws {

class PostHandler : public IMethodHandler {
public:
	PostHandler();
	~PostHandler();

	Response handle(const Request& req,
	                const LocationConfig& loc,
	                const ServerConfig& srv);

private:
	Response handleUpload(const Request& req,
	                      const LocationConfig& loc,
	                      const ServerConfig& srv);
	Response handleCgi(const Request& req,
	                   const LocationConfig& loc,
	                   const ServerConfig& srv,
	                   const std::string& interpreter);
};

}

#endif
