#include "http/Router.hpp"
#include "http/ResponseFactory.hpp"
#include "session/SessionStore.hpp"

namespace ws {

Router::Router(const std::vector<ServerConfig>& configs, SessionStore& sessions)
	: configs_(configs)
	, sessions_(sessions)
	, getH_()
	, postH_()
	, deleteH_()
{}

Router::~Router() {}

Response Router::route(const Request& /*req*/, const ServerConfig& vhost) {
	// TODO Membro 3: matchLocation + dispatch para handler correto
	return ResponseFactory::makeError(501, vhost);
}

bool Router::methodAllowed(const std::string& /*method*/, const LocationConfig& /*loc*/) const {
	return true;
}

void Router::attachSessionCookie(const Request& /*req*/, Response& /*resp*/) {
	// TODO Membro 3
}

}
