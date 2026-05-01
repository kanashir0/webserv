#include "http/handlers/GetHandler.hpp"
#include "http/ResponseFactory.hpp"

namespace ws {

GetHandler::GetHandler() {}
GetHandler::~GetHandler() {}

Response GetHandler::handle(const Request& /*req*/,
                            const LocationConfig& /*loc*/,
                            const ServerConfig& srv) {
	// TODO Membro 3
	return ResponseFactory::makeError(501, srv);
}

Response GetHandler::serveFile(const std::string& /*fsPath*/, const ServerConfig& srv) {
	return ResponseFactory::makeError(501, srv);
}

Response GetHandler::serveDirectory(const std::string& /*fsPath*/,
                                    const std::string& /*uriPath*/,
                                    const LocationConfig& /*loc*/,
                                    const ServerConfig& srv) {
	return ResponseFactory::makeError(501, srv);
}

}
