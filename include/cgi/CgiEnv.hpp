#ifndef WEBSERV_CGI_CGI_ENV_HPP
#define WEBSERV_CGI_CGI_ENV_HPP

#include "http/Request.hpp"
#include "config/LocationConfig.hpp"
#include "config/ServerConfig.hpp"
#include <string>
#include <vector>


class CgiEnv {
public:
	CgiEnv(const Request& req,
	       const LocationConfig& loc,
	       const ServerConfig& srv,
	       const std::string& scriptPath);
	~CgiEnv();

	char**             asEnvp();
	const std::vector<std::string>& asVector() const;

private:
	std::vector<std::string> entries_;
	std::vector<char*>       envp_;

	void build(const Request& req,
	           const LocationConfig& loc,
	           const ServerConfig& srv,
	           const std::string& scriptPath);

	CgiEnv(const CgiEnv&);
	CgiEnv& operator=(const CgiEnv&);
};


#endif
