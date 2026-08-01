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
		// Diretorios (e qualquer nao-regular) nao sao deletaveis via HTTP.
		return ResponseFactory::makeError(HTTP_FORBIDDEN, srv);
	}
	// Quem autoriza remover uma entrada e o DIRETORIO (write+exec), nao o
	// arquivo. Pre-check via access(): o subject veta decidir por errno, entao a
	// classificacao vem antes, no estilo de readRegularFile.
	if (access(parentDir(fsPath).c_str(), W_OK | X_OK) != 0) {
		return ResponseFactory::makeError(HTTP_FORBIDDEN, srv);
	}
	// std::remove e biblioteca padrao (unlink nao esta na lista de funcoes
	// autorizadas do subject). Permissao ja foi checada: falhar aqui e anomalia.
	if (std::remove(fsPath.c_str()) != 0) {
		LOG_ERROR("DeleteHandler: remove falhou em \"" + fsPath + "\"");
		return ResponseFactory::makeError(HTTP_INTERNAL_SERVER_ERROR, srv);
	}
	// 204 sem setBody(): a RFC 7230 3.3.2 proibe Content-Length em 204.
	return Response(HTTP_NO_CONTENT);
}
