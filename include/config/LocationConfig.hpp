#ifndef WEBSERV_CONFIG_LOCATION_CONFIG_HPP
#define WEBSERV_CONFIG_LOCATION_CONFIG_HPP

#include "common/Types.hpp"
#include <string>
#include <map>
#include <cstddef>


struct LocationConfig {
	std::string                        path;
	StringVec                          methods;
	std::string                        root;
	std::string                        index;
	bool                               autoindex;
	std::string                        redirect;
	int                                redirectCode;
	std::string                        uploadStore;
	std::map<std::string, std::string> cgi;
	std::size_t                        clientMaxBodySize; // 0 = herda do server

	LocationConfig();
};


#endif
