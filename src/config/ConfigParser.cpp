#include "config/ConfigParser.hpp"


ConfigParser::ParseError::ParseError(const std::string& msg, std::size_t line)
	: std::runtime_error(msg), line_(line) {}

std::size_t ConfigParser::ParseError::line() const { return line_; }

ConfigParser::ConfigParser() : source_(), pos_(0), line_(1), state_(TOPLEVEL) {}
ConfigParser::~ConfigParser() {}

std::vector<ServerConfig> ConfigParser::parseFile(const std::string& /*path*/) {
	// TODO Membro 2: ler arquivo e delegar para parseString
	return std::vector<ServerConfig>();
}

std::vector<ServerConfig> ConfigParser::parseString(const std::string& /*source*/) {
	// TODO Membro 2: tokenizar e construir state machine
	return std::vector<ServerConfig>();
}

std::vector<ServerConfig> ConfigParser::doParse() { return std::vector<ServerConfig>(); }
ServerConfig              ConfigParser::parseServerBlock()   { return ServerConfig(); }
LocationConfig            ConfigParser::parseLocationBlock() { return LocationConfig(); }
std::string               ConfigParser::nextToken()          { return std::string(); }
void                      ConfigParser::expect(const std::string&) {}
void                      ConfigParser::skipWhitespace()    {}

