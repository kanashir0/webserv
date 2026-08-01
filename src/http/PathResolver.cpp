#include "http/PathResolver.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/Types.hpp"


static int hexValue(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// O RequestParser nao decodifica: path() vem cru da request line. Sem decodificar
// aqui, "/a%20b.txt" procuraria um arquivo chamado literalmente "a%20b.txt".
bool PathResolver::percentDecode(const std::string& raw, std::string& out) {
	out.clear();
	out.reserve(raw.size());
	for (std::string::size_type i = 0; i < raw.size(); ++i) {
		if (raw[i] != '%') {
			out += raw[i];
			continue;
		}
		if (i + 2 >= raw.size()) {
			return false;
		}
		int high = hexValue(raw[i + 1]);
		int low  = hexValue(raw[i + 2]);
		if (high < 0 || low < 0 || (high == 0 && low == 0)) {
			return false;
		}
		out += static_cast<char>(high * 16 + low);
		i += 2;
	}
	return true;
}

// Barreira de path traversal. Roda DEPOIS do decode, senao "%2e%2e%2f" passaria
// intacto pelo filtro e so viraria "../" mais tarde. Feita sobre os segmentos da
// string porque realpath() nao esta na lista de funcoes autorizadas do subject.
static bool normalizePath(const std::string& path, std::string& out) {
	StringVec              segments;
	std::string::size_type i = 0;
	while (i <= path.size()) {
		std::string::size_type slash   = path.find('/', i);
		std::string            segment = (slash == std::string::npos)
			? path.substr(i)
			: path.substr(i, slash - i);
		if (segment == "..") {
			if (segments.empty()) {
				return false;  // subiria acima da raiz do location
			}
			segments.erase(segments.end() - 1);
		} else if (!segment.empty() && segment != ".") {
			segments.push_back(segment);
		}
		if (slash == std::string::npos) {
			break;
		}
		i = slash + 1;
	}
	out = "/";
	for (StringVec::const_iterator it = segments.begin(); it != segments.end(); ++it) {
		if (it != segments.begin()) {
			out += "/";
		}
		out += *it;
	}
	return true;
}

// Semantica de alias, conforme o subject (IV.3): "se o URL /kapouet estiver
// enraizado em /tmp/www, o URL /kapouet/pouic/toto/pouet procurara por
// /tmp/www/pouic/toto/pouet" -- o prefixo do location e descartado. Difere do
// `root` do nginx, que concatenaria o prefixo.
static std::string stripLocationPrefix(const std::string& path, const std::string& locPath) {
	std::string prefix = locPath;
	if (prefix.size() > 1 && prefix[prefix.size() - 1] == '/') {
		prefix.erase(prefix.size() - 1);
	}
	if (prefix.empty() || prefix == "/" || path.compare(0, prefix.size(), prefix) != 0) {
		return path;
	}
	std::string rest = path.substr(prefix.size());
	if (rest.empty()) {
		return "/";
	}
	if (rest[0] != '/') {
		return path;  // casou meio segmento: /kapouetX nao pertence a /kapouet
	}
	return rest;
}

std::string PathResolver::encodeSegment(const std::string& segment) {
	static const char hexDigits[] = "0123456789ABCDEF";
	std::string encoded;
	encoded.reserve(segment.size());
	for (std::string::size_type i = 0; i < segment.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(segment[i]);
		bool isUnreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		                    (c >= '0' && c <= '9') ||
		                    c == '-' || c == '.' || c == '_' || c == '~';
		if (isUnreserved) {
			encoded += static_cast<char>(c);
		} else {
			encoded += '%';
			encoded += hexDigits[c >> 4];
			encoded += hexDigits[c & 0x0F];
		}
	}
	return encoded;
}

std::string PathResolver::joinPath(const std::string& root, const std::string& rel) {
	if (root.empty()) {
		return rel;
	}
	std::string base = root;
	if (base.size() > 1 && base[base.size() - 1] == '/') {
		base.erase(base.size() - 1);
	}
	if (!rel.empty() && rel[0] == '/') {
		return base + rel;
	}
	return base + "/" + rel;
}

int PathResolver::resolve(const std::string& rawPath,
                          const LocationConfig& loc,
                          const ServerConfig& srv,
                          std::string& fsPath) {
	std::string decoded;
	if (!percentDecode(rawPath, decoded)) {
		LOG_WARN("PathResolver: percent-encoding invalido em \"" + rawPath + "\"");
		return HTTP_BAD_REQUEST;
	}

	std::string normalized;
	if (!normalizePath(decoded, normalized)) {
		LOG_WARN("PathResolver: path traversal bloqueado em \"" + rawPath + "\"");
		return HTTP_FORBIDDEN;
	}

	// Alias vale so quando o location declara root proprio: e ele que substitui o
	// prefixo. O root do bloco server mapeia "/", entao ali o path vai inteiro.
	const bool         hasLocationRoot = !loc.root.empty();
	const std::string& root            = hasLocationRoot ? loc.root : srv.root;
	if (root.empty()) {
		LOG_ERROR("PathResolver: nenhum root configurado para \"" + loc.path + "\"");
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	std::string relative = hasLocationRoot
		? stripLocationPrefix(normalized, loc.path)
		: normalized;
	fsPath = joinPath(root, relative);
	return HTTP_OK;
}
