#ifndef WEBSERV_CONFIG_CONFIG_PARSER_HPP
#define WEBSERV_CONFIG_CONFIG_PARSER_HPP

#include "config/ServerConfig.hpp"
#include <string>
#include <vector>
#include <stdexcept>


class ConfigParser {
public:
	class ParseError : public std::runtime_error {
	public:
		ParseError(const std::string& msg, std::size_t line);
		std::size_t line() const;
	private:
		std::size_t line_;
	};

	ConfigParser();
	~ConfigParser();

	std::vector<ServerConfig> parseFile(const std::string& path);
	std::vector<ServerConfig> parseString(const std::string& source);

private:
	enum State { TOPLEVEL, IN_SERVER, IN_LOCATION };

	std::string  source_;
	std::size_t  pos_;
	std::size_t  line_;
	State        state_;

	std::vector<ServerConfig> doParse();
	ServerConfig              parseServerBlock();
	LocationConfig            parseLocationBlock();
	std::string               nextToken();
	void                      expect(const std::string& token);
	void                      skipWhitespace();

	ConfigParser(const ConfigParser&);
	ConfigParser& operator=(const ConfigParser&);
};


#endif
