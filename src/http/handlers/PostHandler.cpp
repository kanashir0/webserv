#include "http/handlers/PostHandler.hpp"
#include "http/ResponseFactory.hpp"


PostHandler::PostHandler() {}
PostHandler::~PostHandler() {}

Response PostHandler::handle(const Request& /*req*/,
                             const LocationConfig& /*loc*/,
                             const ServerConfig& srv) {
	return ResponseFactory::makeError(501, srv);
}

Response PostHandler::handleUpload(const Request& /*req*/,
                                   const LocationConfig& /*loc*/,
                                   const ServerConfig& srv) {
	return ResponseFactory::makeError(501, srv);
}

Response PostHandler::handleCgi(const Request& /*req*/,
                                const LocationConfig& /*loc*/,
                                const ServerConfig& srv,
                                const std::string& /*interpreter*/) {
	return ResponseFactory::makeError(501, srv);
}

