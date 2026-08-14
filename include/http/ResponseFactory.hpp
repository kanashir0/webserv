#ifndef WEBSERV_HTTP_RESPONSE_FACTORY_HPP
#define WEBSERV_HTTP_RESPONSE_FACTORY_HPP

#include "http/Response.hpp"
#include "config/ServerConfig.hpp"
#include <string>

// O parametro `loc` opcional carrega a location que atendeu a requisicao: as
// error_page dela vencem as do server. Passe 0 quando nenhuma location casou
// (404 do Router) ou quando a requisicao nem chegou a ser roteada (erro de
// parsing), casos em que so restam as paginas do server.
class ResponseFactory {
public:
	static Response makeError(int code, const ServerConfig& cfg,
	                          const LocationConfig* loc = 0);
	static Response makeRedirect(const std::string& url, int code = 302);
	static Response makeFile(const std::string& fsPath,
	                         const std::string& mime,
	                         const ServerConfig& cfg,
	                         const LocationConfig* loc = 0);
	static Response makeAutoindex(const std::string& fsPath,
	                              const std::string& uriPath,
	                              const ServerConfig& cfg,
	                              const LocationConfig* loc = 0);
	static Response makeFromCgi(const std::string& rawCgiOutput, const ServerConfig& cfg,
	                            const LocationConfig* loc = 0);

private:
	ResponseFactory();
};

#endif
