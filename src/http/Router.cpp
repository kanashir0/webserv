#include "http/Router.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/StringUtils.hpp"
#include "session/SessionStore.hpp"


// RFC 7231 6.5.5: 405 deve listar os metodos permitidos no header Allow.
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

Router::Router(const std::vector<ServerConfig>& configs, SessionStore& sessions)
	: configs_(configs)
	, sessions_(sessions)
	, getH_()
	, postH_()
	, deleteH_()
{}

Router::~Router() {}

Response Router::route(const Request& req, const ServerConfig& vhost) {
	// Contrato com o M1: route() nunca deixa excecao escapar -- o Client nao tem
	// como se recuperar no meio do loop de eventos. Erro interno vira 500.
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
		// Metodo valido na whitelist mas sem handler (PUT, HEAD...): 405 com os
		// metodos que o servidor de fato implementa.
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

// Whitelist do location. Lista vazia libera tudo (como o nginx, que so restringe
// quando limit_except e declarado) -- e tambem o que mantem o servidor testavel
// enquanto o ConfigParser nao preenche methods.
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
