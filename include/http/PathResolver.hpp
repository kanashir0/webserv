#ifndef WEBSERV_HTTP_PATH_RESOLVER_HPP
#define WEBSERV_HTTP_PATH_RESOLVER_HPP

#include "config/LocationConfig.hpp"
#include "config/ServerConfig.hpp"
#include <string>

class PathResolver {
public:
	static int resolve(const std::string& rawPath,
	                   const LocationConfig& loc,
	                   const ServerConfig& srv,
	                   std::string& fsPath);

	static std::string joinPath(const std::string& root, const std::string& rel);

	// O CGI faz chdir() para o diretorio do script antes do execve, entao
	// caminhos relativos do config precisam ser fixados contra o cwd de origem.
	static std::string toAbsolute(const std::string& path);

	static bool percentDecode(const std::string& raw, std::string& out);

	static std::string encodeSegment(const std::string& segment);

	static std::string basename(const std::string& path);

	// Remove a barra final, preservando a raiz "/".
	static std::string stripTrailingSlash(const std::string& path);

private:
	PathResolver();
};

#endif
