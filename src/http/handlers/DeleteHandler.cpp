#include "http/handlers/DeleteHandler.hpp"
#include "http/PathResolver.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>


static std::string parentDir(const std::string& fsPath) {
	std::string::size_type slash = fsPath.find_last_of('/');
	if (slash == std::string::npos) {
		return ".";
	}
	if (slash == 0) {
		return "/";
	}
	return fsPath.substr(0, slash);
}


DeleteHandler::DeleteHandler() {}
DeleteHandler::~DeleteHandler() {}

Response DeleteHandler::handle(const Request& req,
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
	if (!S_ISREG(info.st_mode)) {
		return ResponseFactory::makeError(HTTP_FORBIDDEN, srv);
	}
	if (access(parentDir(fsPath).c_str(), W_OK | X_OK) != 0) {
		return ResponseFactory::makeError(HTTP_FORBIDDEN, srv);
	}
	if (std::remove(fsPath.c_str()) != 0) {
		LOG_ERROR("DeleteHandler: remove falhou em \"" + fsPath + "\"");
		return ResponseFactory::makeError(HTTP_INTERNAL_SERVER_ERROR, srv);
	}
	return Response(HTTP_NO_CONTENT);
}
