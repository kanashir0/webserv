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

// Exige que o caminho exista e seja um diretorio. Falhar no startup e muito
// mais barato de diagnosticar que um 404/500 na primeira requisicao.
static void requireDirectory(const std::string& directive, const std::string& path,
                             std::size_t line) {
	struct stat st;
	if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
		throw ConfigParser::ParseError(
			directive + " is not an existing directory: '" + path + "'", line);
}

static void parseListen(const std::string& arg, ServerConfig& srv, std::size_t line) {
	std::string portPart = arg;

	std::string::size_type colon = arg.rfind(':');
	if (colon != std::string::npos) {
		if (colon == 0)
			throw ConfigParser::ParseError("missing host in listen directive: '" + arg + "'", line);
		srv.host = arg.substr(0, colon);
		unsigned long ignored = 0;
		if (!StringUtils::parseIPv4(srv.host, ignored))
			throw ConfigParser::ParseError("invalid host in listen directive: '" + srv.host + "'", line);
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


// server_name vira SERVER_NAME no ambiente CGI e e comparado com o header
// Host: aceitar so o charset valido de hostname evita surpresas nos dois usos.
static void validateServerName(const std::string& name, std::size_t line) {
	if (name.empty())
		throw ConfigParser::ParseError("empty server_name", line);
	for (std::string::size_type i = 0; i < name.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(name[i]);
		if (!std::isalnum(c) && c != '.' && c != '-' && c != '_')
			throw ConfigParser::ParseError("invalid server_name: '" + name + "'", line);
	}
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

void ConfigParser::markUnique(std::set<std::string>& seen, const std::string& directive) {
	if (!seen.insert(directive).second)
		throw ParseError("duplicate directive '" + directive + "'", line_);
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

	// Sem isso o processo sobe sem listener nenhum e fica mudo, parecendo vivo.
	if (out.empty())
		throw ParseError("no server block defined", line_);

	return out;
}

ServerConfig ConfigParser::parseServerBlock() {
	ServerConfig          srv;
	std::set<std::string> seen;
	std::set<std::string> locationPaths;

	for (;;) {
		std::string tok = nextToken();
		if (tok.empty())
			throw ParseError("unexpected EOF inside server block", line_);
		if (tok == "}")
			return srv;

		if (tok == "location") {
			state_ = IN_LOCATION;
			LocationConfig loc = parseLocationBlock();
			if (!locationPaths.insert(loc.path).second)
				throw ParseError("duplicate location '" + loc.path + "'", line_);
			srv.locations.push_back(loc);
			state_ = IN_SERVER;
			continue;
		}

		StringVec args = readDirectiveArgs(tok);

		if (tok == "listen") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			parseListen(args[0], srv, line_);
		} else if (tok == "server_name") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 0, line_);
			for (std::size_t i = 0; i < args.size(); ++i)
				validateServerName(args[i], line_);
			srv.serverNames = args;
		} else if (tok == "root") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			requireDirectory(tok, args[0], line_);
			srv.root = args[0];
		} else if (tok == "index") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			srv.index = args[0];
		} else if (tok == "client_max_body_size") {
			markUnique(seen, tok);
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

LocationConfig ConfigParser::parseLocationBlock() {
	LocationConfig        loc;
	std::set<std::string> seen;

	loc.path = nextToken();
	if (loc.path.empty() || loc.path == "{" || loc.path == "}" || loc.path == ";")
		throw ParseError("missing path in location block", line_);
	if (loc.path[0] != '/')
		throw ParseError("location path must start with '/': '" + loc.path + "'", line_);
	expect("{");

	for (;;) {
		std::string tok = nextToken();
		if (tok.empty())
			throw ParseError("unexpected EOF inside location block", line_);
		if (tok == "}")
			return loc;

		StringVec args = readDirectiveArgs(tok);

		if (tok == "methods") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 0, line_);
			for (std::size_t i = 0; i < args.size(); ++i) {
				const std::string method = StringUtils::toUpper(args[i]);
				if (method != "GET" && method != "POST" && method != "DELETE")
					throw ParseError("unsupported method '" + args[i] + "'", line_);
				loc.methods.push_back(method);
			}
		} else if (tok == "root") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			requireDirectory(tok, args[0], line_);
			loc.root = args[0];
		} else if (tok == "index") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			loc.index = args[0];
		} else if (tok == "autoindex") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			if (args[0] == "on")
				loc.autoindex = true;
			else if (args[0] == "off")
				loc.autoindex = false;
			else
				throw ParseError("autoindex expects 'on' or 'off', got '" + args[0] + "'", line_);
		} else if (tok == "return") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 2, line_);
			if (args.size() == 2) {
				loc.redirectCode = parseStatusCode(args[0], line_);
				if (loc.redirectCode != 301 && loc.redirectCode != 302)
					throw ParseError("return expects code 301 or 302, got '" + args[0] + "'", line_);
				loc.redirect     = args[1];
			} else {
				// Um argumento so-digitos e um codigo sem destino: 'return 42;'
				// nao redireciona para lugar nenhum e nao e um path valido.
				if (args[0].find_first_not_of("0123456789") == std::string::npos)
					throw ParseError("return with a status code requires a target URL: '"
					                 + args[0] + "'", line_);
				loc.redirect = args[0];
			}
		} else if (tok == "upload_store") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			requireDirectory(tok, args[0], line_);
			loc.uploadStore = args[0];
		} else if (tok == "client_max_body_size") {
			markUnique(seen, tok);
			requireArgCount(tok, args, 1, 1, line_);
			loc.clientMaxBodySize = parseSize(args[0], line_);
		} else if (tok == "cgi") {
			requireArgCount(tok, args, 2, 2, line_);
			if (args[0].empty() || args[0][0] != '.')
				throw ParseError("cgi extension must start with '.': '" + args[0] + "'", line_);
			// Extensoes distintas podem coexistir; a mesma duas vezes e engano.
			if (!loc.cgi.insert(std::make_pair(args[0], args[1])).second)
				throw ParseError("duplicate cgi extension '" + args[0] + "'", line_);
		} else {
			throw ParseError("unknown directive '" + tok + "' in location block", line_);
		}
	}
}

