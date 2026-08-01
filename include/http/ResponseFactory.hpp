#ifndef WEBSERV_HTTP_RESPONSE_FACTORY_HPP
#define WEBSERV_HTTP_RESPONSE_FACTORY_HPP

#include "http/Response.hpp"
#include "config/ServerConfig.hpp"
#include <string>

// Toda fabrica devolve uma Response pronta e nunca lanca excecao. A que pode
// falhar por I/O recebe o ServerConfig e ja converte a falha em makeError(),
// preservando as error_pages do vhost; o chamador que precisar ramificar
// consulta status(). Diretorio vazio e sucesso, nao erro.
class ResponseFactory {
public:
	static Response makeError(int code, const ServerConfig& cfg);
	static Response makeRedirect(const std::string& url, int code = 302);
	static Response makeFile(const std::string& fsPath,
	                         const std::string& mime,
	                         const ServerConfig& cfg);
	static Response makeAutoindex(const std::string& fsPath,
	                              const std::string& uriPath,
	                              const ServerConfig& cfg);
	static Response makeFromCgi(const std::string& rawCgiOutput);

private:
	ResponseFactory();
};

#endif
