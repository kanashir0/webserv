#include "config/ServerConfig.hpp"

namespace ws {

ServerConfig::ServerConfig()
	: host("0.0.0.0")
	, port(80)
	, serverNames()
	, clientMaxBodySize(1 * 1024 * 1024)
	, errorPages()
	, locations()
{}

const LocationConfig* ServerConfig::findLocation(const std::string& /*uriPath*/) const {
	// TODO Membro 3: longest-prefix match
	return 0;
}

}
