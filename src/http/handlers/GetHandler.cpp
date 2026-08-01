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

Response GetHandler::serveFile(const std::string& fsPath, const ServerConfig& srv) {
	return ResponseFactory::makeFile(fsPath, MimeTypes::fromPath(fsPath), srv);
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
			return serveFile(indexPath, srv);
		}
	}
	if (loc.autoindex) {
		return ResponseFactory::makeAutoindex(fsPath, uriPath, srv);
	}
	return ResponseFactory::makeError(HTTP_FORBIDDEN, srv);
}
