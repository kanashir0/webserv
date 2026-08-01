#include "config/ConfigParser.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <sys/stat.h>

namespace {
	bool isSymbol(char c) { return c == '{' || c == '}' || c == ';'; }
}


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

void ConfigParser::skipWhitespace() {
	while (pos_ < source_.size()) {
		char c = source_[pos_];
		if (c == '\n') {
			++line_;
			++pos_;
		} else if (std::isspace(static_cast<unsigned char>(c))) {
			++pos_;
		} else if (c == '#') {
			while (pos_ < source_.size() && source_[pos_] != '\n')
				++pos_;
		} else {
			break;
		}
	}
}

// String vazia significa EOF — nenhum token válido é vazio.
std::string ConfigParser::nextToken() {
	skipWhitespace();
	if (pos_ >= source_.size())
		return std::string();

	char c = source_[pos_];
	if (isSymbol(c)) {
		++pos_;
		return std::string(1, c);
	}

	std::size_t start = pos_;
	while (pos_ < source_.size()) {
		c = source_[pos_];
		if (isSymbol(c) || c == '#' || std::isspace(static_cast<unsigned char>(c)))
			break;
		++pos_;
	}
	return source_.substr(start, pos_ - start);
}

void ConfigParser::expect(const std::string& token) {
	std::string got = nextToken();
	if (got != token)
		throw ParseError("expected '" + token + "' but got '" +
		                 (got.empty() ? std::string("<EOF>") : got) + "'", line_);
}

std::vector<ServerConfig> ConfigParser::doParse() { return std::vector<ServerConfig>(); }
ServerConfig              ConfigParser::parseServerBlock()   { return ServerConfig(); }
LocationConfig            ConfigParser::parseLocationBlock() { return LocationConfig(); }

