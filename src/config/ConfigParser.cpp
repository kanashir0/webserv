#include "config/ConfigParser.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>


ConfigParser::ParseError::ParseError(const std::string& msg, std::size_t line)
	: std::runtime_error(msg), line_(line) {}

std::size_t ConfigParser::ParseError::line() const { return line_; }

ConfigParser::ConfigParser() : source_(), pos_(0), line_(1), state_(TOPLEVEL) {}
ConfigParser::~ConfigParser() {}

std::vector<ServerConfig> ConfigParser::parseFile(const std::string& path) {
	// ifstream abre diretório sem erro e só devolve zero bytes — indistinguível
	// de um arquivo vazio, que é válido. Só stat() separa os dois casos.
	struct stat st;
	if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
		throw ParseError("config path is a directory: " + path, 0);

	std::ifstream file(path.c_str());
	if (!file.is_open())
		throw ParseError("cannot open config file: " + path, 0);

	std::ostringstream ss;
	ss << file.rdbuf();

	return parseString(ss.str());
}

std::vector<ServerConfig> ConfigParser::parseString(const std::string& source) {
	source_ = source;
	pos_    = 0;
	line_   = 1;
	state_  = TOPLEVEL;
	return doParse();
}

std::vector<ServerConfig> ConfigParser::doParse() { return std::vector<ServerConfig>(); }
ServerConfig              ConfigParser::parseServerBlock()   { return ServerConfig(); }
LocationConfig            ConfigParser::parseLocationBlock() { return LocationConfig(); }
std::string               ConfigParser::nextToken()          { return std::string(); }
void                      ConfigParser::expect(const std::string&) {}
void                      ConfigParser::skipWhitespace()    {}

