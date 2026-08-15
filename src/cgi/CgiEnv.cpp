#include "cgi/CgiEnv.hpp"
#include "common/StringUtils.hpp"
#include <cctype>


static std::string headerToEnvName(const std::string& headerName) {
	std::string name = "HTTP_";
	for (std::string::size_type i = 0; i < headerName.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(headerName[i]);
		name += (c == '-') ? '_' : static_cast<char>(std::toupper(c));
	}
	return name;
}


CgiEnv::CgiEnv(const Request& req,
               const LocationConfig& loc,
               const ServerConfig& srv,
               const std::string& scriptPath)
	: entries_(), envp_()
{
	build(req, loc, srv, scriptPath);
}

CgiEnv::~CgiEnv() {}

void CgiEnv::add(const std::string& name, const std::string& value) {
	entries_.push_back(name + "=" + value);
}

void CgiEnv::build(const Request& req,
                   const LocationConfig& /*loc*/,
                   const ServerConfig& srv,
                   const std::string& scriptPath) {
	add("GATEWAY_INTERFACE", "CGI/1.1");
	add("SERVER_SOFTWARE",   "webserv/1.0");
	add("SERVER_PROTOCOL",   "HTTP/1.1");
	add("SERVER_NAME",       srv.serverNames.empty() ? srv.host : srv.serverNames[0]);
	add("SERVER_PORT",       StringUtils::toString(srv.port));

	add("REQUEST_METHOD",  req.method());
	add("REQUEST_URI",     req.uri());
	add("QUERY_STRING",    req.query());
	add("SCRIPT_NAME",     req.path());
	add("SCRIPT_FILENAME", scriptPath);
	add("PATH_INFO",       "");

	// php-cgi recusa rodar sem esta variavel (protecao contra invocacao direta).
	add("REDIRECT_STATUS", "200");

	add("CONTENT_LENGTH", StringUtils::toString(static_cast<long>(req.body().size())));
	if (req.hasHeader("Content-Type")) {
		add("CONTENT_TYPE", req.header("Content-Type"));
	}

	for (HeaderMap::const_iterator it = req.headers().begin();
	     it != req.headers().end(); ++it) {
		// Ja exportados acima sem o prefixo HTTP_, conforme a RFC.
		if (StringUtils::iequals(it->first, "Content-Length") ||
		    StringUtils::iequals(it->first, "Content-Type")) {
			continue;
		}
		add(headerToEnvName(it->first), it->second);
	}
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

