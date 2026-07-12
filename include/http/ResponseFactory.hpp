#ifndef WEBSERV_HTTP_RESPONSE_FACTORY_HPP
#define WEBSERV_HTTP_RESPONSE_FACTORY_HPP

#include "http/Response.hpp"
#include "config/ServerConfig.hpp"
#include <string>

class ResponseFactory {
public:
	static Response makeError(int code, const ServerConfig& cfg);
	static Response makeRedirect(const std::string& url, int code = 302);
	static Response makeFile(const std::string& fsPath, const std::string& mime);
	static Response makeAutoindex(const std::string& fsPath, const std::string& uriPath);
	static Response makeFromCgi(const std::string& rawCgiOutput);

private:
	ResponseFactory();
};

#endif
