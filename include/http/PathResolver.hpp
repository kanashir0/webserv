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

	static bool percentDecode(const std::string& raw, std::string& out);

	static std::string encodeSegment(const std::string& segment);

	// Ultimo segmento do caminho. Corta em '/' e '\' para que um filename
	// vindo de multipart/form-data nao possa escapar do diretorio.
	static std::string basename(const std::string& path);

	// Remove a barra final, preservando a raiz "/".
	static std::string stripTrailingSlash(const std::string& path);

private:
	PathResolver();
};

#endif
