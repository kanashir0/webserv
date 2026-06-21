#include "config/ConfigParser.hpp"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace ws {

ConfigParser::ParseError::ParseError(const std::string& msg, std::size_t line)
	: std::runtime_error(msg), line_(line) {}

std::size_t ConfigParser::ParseError::line() const { return line_; }

ConfigParser::ConfigParser() : source_(), pos_(0), line_(1), state_(TOPLEVEL) {}
ConfigParser::~ConfigParser() {}

std::vector<ServerConfig> ConfigParser::parseFile(const std::string& path) {
	struct stat st;
	if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		throw ParseError("não foi possível abrir o arquivo: " + path, 0);
	}

	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in.is_open()) {
		throw ParseError("não foi possível abrir o arquivo: " + path, 0);
	}

	std::stringstream buf;
	buf << in.rdbuf();
	if (in.bad()) {
		throw ParseError("não foi possível abrir o arquivo: " + path, 0);
	}

	source_ = buf.str();
	pos_    = 0;
	line_   = 1;
	state_  = TOPLEVEL;

	return parseString(source_);
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

}
