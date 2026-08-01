#ifndef WEBSERV_HTTP_PATH_RESOLVER_HPP
#define WEBSERV_HTTP_PATH_RESOLVER_HPP

#include "config/LocationConfig.hpp"
#include "config/ServerConfig.hpp"
#include <string>

// Pipeline unico de URI -> caminho de filesystem, compartilhado pelos handlers
// GET/POST/DELETE. Concentra o codigo de seguranca (decode, traversal, alias)
// num lugar so: copias divergem, e divergencia aqui e vulnerabilidade.
class PathResolver {
public:
	// percent-decode -> normaliza "." e ".." -> strip do prefixo do location
	// (semantica de alias do subject; so quando o location declara root proprio)
	// -> join com o root. Retorna um HttpStatus: HTTP_OK preenche fsPath;
	// 400 encoding invalido ou %00; 403 traversal acima da raiz; 500 sem root.
	static int resolve(const std::string& rawPath,
	                   const LocationConfig& loc,
	                   const ServerConfig& srv,
	                   std::string& fsPath);

	static std::string joinPath(const std::string& root, const std::string& rel);

	// false em encoding invalido ou %00 (um NUL truncaria o caminho no open()).
	static bool percentDecode(const std::string& raw, std::string& out);

	// Percent-encoda um segmento de URI; unreserved da RFC 3986 passam intactos.
	static std::string encodeSegment(const std::string& segment);

private:
	PathResolver();
};

#endif
