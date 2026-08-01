#include "http/Router.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/StringUtils.hpp"
#include "session/SessionStore.hpp"


static std::string allowHeaderFor(const LocationConfig& loc) {
	if (loc.methods.empty()) {
		return "GET, POST, DELETE";
	}
	std::string allow;
	for (StringVec::const_iterator it = loc.methods.begin(); it != loc.methods.end(); ++it) {
		if (!allow.empty()) {
			allow += ", ";
		}
		allow += StringUtils::toUpper(*it);
	}
	return allow;
}

Router::Router(SessionStore& sessions)
	: sessions_(sessions)
	, getH_()
	, postH_()
	, deleteH_()
{}

Router::~Router() {}

Response Router::route(const Request& req, const ServerConfig& vhost) {
	try {
		const LocationConfig* loc = vhost.findLocation(req.path());
		if (loc == 0) {
			return ResponseFactory::makeError(HTTP_NOT_FOUND, vhost);
		}
		if (!loc->redirect.empty()) {
			return ResponseFactory::makeRedirect(loc->redirect, loc->redirectCode);
		}
		if (!methodAllowed(req.method(), *loc)) {
			Response r = ResponseFactory::makeError(HTTP_METHOD_NOT_ALLOWED, vhost);
			r.setHeader("Allow", allowHeaderFor(*loc));
			return r;
		}
		if (req.method() == "GET") {
			return getH_.handle(req, *loc, vhost);
		}
		if (req.method() == "POST") {
			return postH_.handle(req, *loc, vhost);
		}
		if (req.method() == "DELETE") {
			return deleteH_.handle(req, *loc, vhost);
		}
		Response r = ResponseFactory::makeError(HTTP_METHOD_NOT_ALLOWED, vhost);
		r.setHeader("Allow", "GET, POST, DELETE");
		return r;
	} catch (const std::exception& e) {
		LOG_ERROR("Router::route: excecao capturada: " + std::string(e.what()));
	} catch (...) {
		LOG_ERROR("Router::route: excecao desconhecida capturada");
	}
	return ResponseFactory::makeError(HTTP_INTERNAL_SERVER_ERROR, vhost);
}

bool Router::methodAllowed(const std::string& method, const LocationConfig& loc) const {
	if (loc.methods.empty()) {
		return true;
	}
	for (StringVec::const_iterator it = loc.methods.begin(); it != loc.methods.end(); ++it) {
		if (StringUtils::iequals(method, *it)) {
			return true;
		}
	}
	return false;
}

void Router::attachSessionCookie(const Request& /*req*/, Response& /*resp*/) {
	// TODO Membro 3 (bonus): so com a parte obrigatoria fechada
}
