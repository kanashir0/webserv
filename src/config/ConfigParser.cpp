#include "config/ConfigParser.hpp"
#include "common/StringUtils.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <sys/stat.h>

static bool isSymbol(char c) { return c == '{' || c == '}' || c == ';'; }

// max == 0 significa "sem limite superior".
static void requireArgCount(const std::string& directive, const StringVec& args,
                            std::size_t min, std::size_t max, std::size_t line) {
	if (args.size() < min || (max != 0 && args.size() > max))
		throw ConfigParser::ParseError(
			"wrong number of arguments for directive '" + directive + "'", line);
}

static long parseNumber(const std::string& s, const std::string& what, std::size_t line) {
	bool ok = false;
	long n = StringUtils::toLong(s, ok);
	if (!ok)
		throw ConfigParser::ParseError("invalid " + what + ": '" + s + "'", line);
	return n;
}

static int parseStatusCode(const std::string& s, std::size_t line) {
	long code = parseNumber(s, "status code", line);
	if (code < 100 || code > 599)
		throw ConfigParser::ParseError("status code out of range: '" + s + "'", line);
	return static_cast<int>(code);
}

static void parseListen(const std::string& arg, ServerConfig& srv, std::size_t line) {
	std::string portPart = arg;

	std::string::size_type colon = arg.rfind(':');
	if (colon != std::string::npos) {
		if (colon == 0)
			throw ConfigParser::ParseError("missing host in listen directive: '" + arg + "'", line);
		srv.host = arg.substr(0, colon);
		portPart = arg.substr(colon + 1);
	}

	long port = parseNumber(portPart, "port", line);
	if (port < 1 || port > 65535)
		throw ConfigParser::ParseError("port out of range: '" + portPart + "'", line);
	srv.port = static_cast<int>(port);
}

// Sufixos k/m/g case-insensitive; toLong rejeita o sufixo, então ele sai antes.
static std::size_t parseSize(const std::string& arg, std::size_t line) {
	if (arg.empty())
		throw ConfigParser::ParseError("empty size value", line);

	std::string digits = arg;
	std::size_t multiplier = 1;

	switch (std::tolower(static_cast<unsigned char>(arg[arg.size() - 1]))) {
		case 'k': multiplier = 1024UL; break;
		case 'm': multiplier = 1024UL * 1024UL; break;
		case 'g': multiplier = 1024UL * 1024UL * 1024UL; break;
		default:  multiplier = 0; break;
	}
	if (multiplier != 0)
		digits = arg.substr(0, arg.size() - 1);
	else
		multiplier = 1;

	long n = parseNumber(digits, "size", line);
	if (n < 0)
		throw ConfigParser::ParseError("negative size: '" + arg + "'", line);
	return static_cast<std::size_t>(n) * multiplier;
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

StringVec ConfigParser::readDirectiveArgs(const std::string& directive) {
	StringVec args;
	for (std::string tok = nextToken(); tok != ";"; tok = nextToken()) {
		if (tok.empty() || tok == "{" || tok == "}")
			throw ParseError("missing ';' after directive '" + directive + "'", line_);
		args.push_back(tok);
	}
	return args;
}

std::vector<ServerConfig> ConfigParser::doParse() {
	std::vector<ServerConfig> out;

	for (std::string tok = nextToken(); !tok.empty(); tok = nextToken()) {
		if (tok != "server")
			throw ParseError("unexpected token '" + tok + "' at top level", line_);
		expect("{");
		state_ = IN_SERVER;
		out.push_back(parseServerBlock());
		state_ = TOPLEVEL;
	}
	return out;
}

ServerConfig ConfigParser::parseServerBlock() {
	ServerConfig srv;

	for (;;) {
		std::string tok = nextToken();
		if (tok.empty())
			throw ParseError("unexpected EOF inside server block", line_);
		if (tok == "}")
			return srv;

		if (tok == "location") {
			skipLocationBlock();
			continue;
		}

		StringVec args = readDirectiveArgs(tok);

		if (tok == "listen") {
			requireArgCount(tok, args, 1, 1, line_);
			parseListen(args[0], srv, line_);
		} else if (tok == "server_name") {
			requireArgCount(tok, args, 1, 0, line_);
			srv.serverNames = args;
		} else if (tok == "root") {
			requireArgCount(tok, args, 1, 1, line_);
			srv.root = args[0];
		} else if (tok == "index") {
			requireArgCount(tok, args, 1, 1, line_);
			srv.index = args[0];
		} else if (tok == "client_max_body_size") {
			requireArgCount(tok, args, 1, 1, line_);
			srv.clientMaxBodySize = parseSize(args[0], line_);
		} else if (tok == "error_page") {
			requireArgCount(tok, args, 2, 0, line_);
			const std::string& page = args[args.size() - 1];
			for (std::size_t i = 0; i + 1 < args.size(); ++i)
				srv.errorPages[parseStatusCode(args[i], line_)] = page;
		} else {
			throw ParseError("unknown directive '" + tok + "' in server block", line_);
		}
	}
}

// ponytail: descarte temporário — E02-T04 substitui por parseLocationBlock().
void ConfigParser::skipLocationBlock() {
	nextToken();      // path
	expect("{");

	for (std::size_t depth = 1; depth > 0;) {
		std::string tok = nextToken();
		if (tok.empty())
			throw ParseError("unexpected EOF inside location block", line_);
		if (tok == "{") ++depth;
		else if (tok == "}") --depth;
	}
}

LocationConfig ConfigParser::parseLocationBlock() { return LocationConfig(); }

