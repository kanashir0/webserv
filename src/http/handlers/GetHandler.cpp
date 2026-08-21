#include "http/handlers/GetHandler.hpp"
#include "http/MimeTypes.hpp"
#include "http/PathResolver.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/StringUtils.hpp"
#include <sys/stat.h>


GetHandler::GetHandler() {}
GetHandler::~GetHandler() {}

Response GetHandler::handle(const Request& req,
                            const LocationConfig& loc,
                            const ServerConfig& srv) {
	std::string fsPath;
	int         status = PathResolver::resolve(req.path(), loc, srv, fsPath);
	if (status != HTTP_OK) {
		return ResponseFactory::makeError(status, srv, &loc);
	}

	struct stat info;
	if (stat(fsPath.c_str(), &info) != 0) {
		return ResponseFactory::makeError(HTTP_NOT_FOUND, srv, &loc);
	}
	if (S_ISDIR(info.st_mode)) {
		if (!StringUtils::endsWith(req.path(), "/")) {
			std::string target = req.path() + "/";
			if (!req.query().empty()) {
				target += "?" + req.query();
			}
			return ResponseFactory::makeRedirect(target, HTTP_MOVED_PERMANENTLY);
		}
		return serveDirectory(fsPath, req.path(), loc, srv);
	}
	if (!S_ISREG(info.st_mode)) {
		return ResponseFactory::makeError(HTTP_FORBIDDEN, srv, &loc);
	}
	return serveFile(fsPath, loc, srv);
}

Response GetHandler::serveFile(const std::string& fsPath,
                               const LocationConfig& loc,
                               const ServerConfig& srv) {
	return ResponseFactory::makeFile(fsPath, MimeTypes::fromPath(fsPath), srv, &loc);
}

Response GetHandler::serveDirectory(const std::string& fsPath,
                                    const std::string& uriPath,
                                    const LocationConfig& loc,
                                    const ServerConfig& srv) {
	const std::string& index = !loc.index.empty() ? loc.index : srv.index;
	if (!index.empty()) {
		std::string indexPath = PathResolver::joinPath(fsPath, index);
		struct stat info;
		if (stat(indexPath.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
			return serveFile(indexPath, loc, srv);
		}
	}
	// Sem a diretiva na location vale o default do server.
	const bool autoindex = loc.autoindexSet ? loc.autoindex : srv.autoindex;
	if (autoindex) {
		return ResponseFactory::makeAutoindex(fsPath, uriPath, srv, &loc);
	}
	// Sem index e sem listagem nao ha recurso algum para expor nesta URI, e
	// responder 403 apenas revelaria que o diretorio existe.
	return ResponseFactory::makeError(HTTP_NOT_FOUND, srv, &loc);
}
