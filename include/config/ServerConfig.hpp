#ifndef WEBSERV_CONFIG_SERVER_CONFIG_HPP
#define WEBSERV_CONFIG_SERVER_CONFIG_HPP

#include "common/Types.hpp"
#include "config/LocationConfig.hpp"
#include <string>
#include <map>
#include <vector>
#include <cstddef>


struct ServerConfig {
	std::string                  host;
	int                          port;
	StringVec                    serverNames;
	std::size_t                  clientMaxBodySize;
	std::map<int, std::string>   errorPages;
	std::vector<LocationConfig>  locations;

	ServerConfig();

	const LocationConfig* findLocation(const std::string& uriPath) const;
};


#endif
