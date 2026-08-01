#include "http/handlers/GetHandler.hpp"
#include "http/MimeTypes.hpp"
#include "http/PathResolver.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include <sys/stat.h>


static bool endsWithSlash(const std::string& s) {
	return !s.empty() && s[s.size() - 1] == '/';
}


GetHandler::GetHandler() {}
GetHandler::~GetHandler() {}

Response GetHandler::handle(const Request& req,
                            const LocationConfig& loc,
                            const ServerConfig& srv) {
	std::string fsPath;
	int         status = PathResolver::resolve(req.path(), loc, srv, fsPath);
	if (status != HTTP_OK) {
		return ResponseFactory::makeError(status, srv);
	}

	struct stat info;
	if (stat(fsPath.c_str(), &info) != 0) {
		return ResponseFactory::makeError(HTTP_NOT_FOUND, srv);
	}
	if (S_ISDIR(info.st_mode)) {
		// Sem a barra final o navegador resolve os links relativos do index a
		// partir do diretorio pai. Redireciona com o path cru: o cliente
		// reenvia a URI ja codificada.
		if (!endsWithSlash(req.path())) {
			return ResponseFactory::makeRedirect(req.path() + "/", HTTP_MOVED_PERMANENTLY);
		}
		return serveDirectory(fsPath, req.path(), loc, srv);
	}
	if (!S_ISREG(info.st_mode)) {
		return ResponseFactory::makeError(HTTP_FORBIDDEN, srv);
	}
	return serveFile(fsPath, srv);
}

// makeFile ja classifica 404/403/500 via stat()+access() e converte em makeError
// com a error_page do vhost; repetir as checagens aqui so duplicaria a logica.
Response GetHandler::serveFile(const std::string& fsPath, const ServerConfig& srv) {
	return ResponseFactory::makeFile(fsPath, MimeTypes::fromPath(fsPath), srv);
}

Response GetHandler::serveDirectory(const std::string& fsPath,
                                    const std::string& uriPath,
                                    const LocationConfig& loc,
                                    const ServerConfig& srv) {
	if (!loc.index.empty()) {
		std::string indexPath = PathResolver::joinPath(fsPath, loc.index);
		struct stat info;
		if (stat(indexPath.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
			return serveFile(indexPath, srv);
		}
	}
	if (loc.autoindex) {
		// uriPath cru de proposito: makeAutoindex percent-encoda as entradas mas
		// nao a base, entao a base precisa chegar ja codificada.
		return ResponseFactory::makeAutoindex(fsPath, uriPath, srv);
	}
	return ResponseFactory::makeError(HTTP_FORBIDDEN, srv);
}
