#include "http/handlers/DeleteHandler.hpp"
#include "http/ResponseFactory.hpp"


DeleteHandler::DeleteHandler() {}
DeleteHandler::~DeleteHandler() {}

Response DeleteHandler::handle(const Request& /*req*/,
                               const LocationConfig& /*loc*/,
                               const ServerConfig& srv) {
	return ResponseFactory::makeError(501, srv);
}

