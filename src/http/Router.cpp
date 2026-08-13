#include "http/Router.hpp"
#include "http/PathResolver.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/StringUtils.hpp"
#include "session/SessionStore.hpp"
#include <sys/stat.h>


CgiTarget::CgiTarget() : loc(0), interpreter(), scriptPath() {}

bool CgiTarget::active() const { return loc != 0; }


// A location lista extensoes (".py") e o interpretador de cada uma.
static bool findCgiInterpreter(const std::string& decodedPath,
                               const LocationConfig& loc,
                               std::string& interpreter) {
	for (std::map<std::string, std::string>::const_iterator it = loc.cgi.begin();
	     it != loc.cgi.end(); ++it) {
		if (!it->first.empty() && StringUtils::endsWith(decodedPath, it->first)) {
			interpreter = it->second;
			return true;
		}
	}
	return false;
}

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

Response Router::route(const Request& req, const ServerConfig& vhost, CgiTarget& cgi) {
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
		// O CGI atende qualquer metodo permitido na location, entao a checagem
		// vem antes do despacho por metodo.
		std::string decodedPath;
		std::string interpreter;
		if (!loc->cgi.empty() &&
		    PathResolver::percentDecode(req.path(), decodedPath) &&
		    findCgiInterpreter(decodedPath, *loc, interpreter)) {
			return prepareCgi(req, *loc, vhost, interpreter, cgi);
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

// Valida o script e preenche o alvo. A Response so importa quando algo falha:
// com cgi.active() verdadeiro quem chama descarta o valor devolvido.
Response Router::prepareCgi(const Request& req,
                            const LocationConfig& loc,
                            const ServerConfig& vhost,
                            const std::string& interpreter,
                            CgiTarget& cgi) {
	std::string fsPath;
	int         status = PathResolver::resolve(req.path(), loc, vhost, fsPath);
	if (status != HTTP_OK) {
		return ResponseFactory::makeError(status, vhost);
	}

	struct stat info;
	if (stat(fsPath.c_str(), &info) != 0) {
		return ResponseFactory::makeError(HTTP_NOT_FOUND, vhost);
	}
	if (!S_ISREG(info.st_mode)) {
		return ResponseFactory::makeError(HTTP_FORBIDDEN, vhost);
	}

	cgi.loc         = &loc;
	cgi.interpreter = interpreter;
	cgi.scriptPath  = fsPath;
	return Response();
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
