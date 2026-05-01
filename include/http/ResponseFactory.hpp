#ifndef WEBSERV_HTTP_RESPONSE_FACTORY_HPP
#define WEBSERV_HTTP_RESPONSE_FACTORY_HPP

#include "http/Response.hpp"
#include "config/ServerConfig.hpp"
#include <string>

namespace ws {
namespace ResponseFactory {

Response makeError(int code, const ServerConfig& cfg);
Response makeRedirect(const std::string& url, int code = 302);
Response makeFile(const std::string& fsPath, const std::string& mime);
Response makeAutoindex(const std::string& fsPath, const std::string& uriPath);
Response makeFromCgi(const std::string& rawCgiOutput);

}
}

#endif
