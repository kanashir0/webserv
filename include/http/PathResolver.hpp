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

private:
	PathResolver();
};

#endif
