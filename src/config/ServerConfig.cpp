#include "config/ServerConfig.hpp"

ServerConfig::ServerConfig()
	: host("0.0.0.0")
	, port(80)
	, serverNames()
	, clientMaxBodySize(1 * 1024 * 1024)
	, errorPages()
	, locations()
{}

const LocationConfig* ServerConfig::findLocation(const std::string& uriPath) const {
	const LocationConfig* bestMatch = 0;

	for (std::vector<LocationConfig>::const_iterator it = locations.begin();
	     it != locations.end(); ++it) {
		const std::string& prefix = it->path;
		if (prefix.empty() || uriPath.compare(0, prefix.size(), prefix) != 0) {
			continue;
		}
		if (bestMatch == 0 || prefix.size() > bestMatch->path.size()) {
			bestMatch = &(*it);
		}
	}
	return bestMatch;
}
