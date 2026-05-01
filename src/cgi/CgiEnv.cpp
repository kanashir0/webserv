#include "cgi/CgiEnv.hpp"

namespace ws {

CgiEnv::CgiEnv(const Request& req,
               const LocationConfig& loc,
               const ServerConfig& srv,
               const std::string& scriptPath)
	: entries_(), envp_()
{
	build(req, loc, srv, scriptPath);
}

CgiEnv::~CgiEnv() {}

void CgiEnv::build(const Request& /*req*/,
                   const LocationConfig& /*loc*/,
                   const ServerConfig& /*srv*/,
                   const std::string& /*scriptPath*/) {
	// TODO Membro 3: GATEWAY_INTERFACE, REQUEST_METHOD, CONTENT_LENGTH, ...
}

char** CgiEnv::asEnvp() {
	envp_.clear();
	for (std::vector<std::string>::iterator it = entries_.begin();
	     it != entries_.end(); ++it) {
		envp_.push_back(const_cast<char*>(it->c_str()));
	}
	envp_.push_back(0);
	return envp_.empty() ? 0 : &envp_[0];
}

const std::vector<std::string>& CgiEnv::asVector() const { return entries_; }

}
