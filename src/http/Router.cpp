#include "http/Router.hpp"
#include "http/PathResolver.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/StringUtils.hpp"
#include "session/SessionStore.hpp"
#include <sys/stat.h>


static const char* const kSessionPath   = "/session";
static const char* const kSessionCookie = "sid";


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
			Response r = ResponseFactory::makeError(HTTP_METHOD_NOT_ALLOWED, vhost, loc);
			r.setHeader("Allow", allowHeaderFor(*loc));
			return r;
		}
		// Bonus: rota interna, servida sem tocar no disco.
		if (req.path() == kSessionPath) {
			return handleSession(req);
		}
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
		Response r = ResponseFactory::makeError(HTTP_METHOD_NOT_ALLOWED, vhost, loc);
		r.setHeader("Allow", "GET, POST, DELETE");
		return r;
	} catch (const std::exception& e) {
		LOG_ERROR("Router::route: excecao capturada: " + std::string(e.what()));
	} catch (...) {
		LOG_ERROR("Router::route: excecao desconhecida capturada");
	}
	return ResponseFactory::makeError(HTTP_INTERNAL_SERVER_ERROR, vhost);
}

Response Router::prepareCgi(const Request& req,
                            const LocationConfig& loc,
                            const ServerConfig& vhost,
                            const std::string& interpreter,
                            CgiTarget& cgi) {
	std::string fsPath;
	int         status = PathResolver::resolve(req.path(), loc, vhost, fsPath);
	if (status != HTTP_OK) {
		return ResponseFactory::makeError(status, vhost, &loc);
	}

	struct stat info;
	if (stat(fsPath.c_str(), &info) != 0) {
		return ResponseFactory::makeError(HTTP_NOT_FOUND, vhost, &loc);
	}
	if (!S_ISREG(info.st_mode)) {
		return ResponseFactory::makeError(HTTP_FORBIDDEN, vhost, &loc);
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

Response Router::handleSession(const Request& req) {
	const std::string incoming = req.cookie(kSessionCookie);
	Session*          existing = incoming.empty() ? 0 : sessions_.find(incoming);
	const bool        isNew    = (existing == 0);

	// Cookie ausente, desconhecido ou ja expirado pelo GC: comeca de novo.
	Session& session = isNew ? sessions_.getOrCreate("") : *existing;
	if (!isNew) {
		session.touch(sessions_.ttlSeconds());
	}

	bool ok    = false;
	long visits = StringUtils::toLong(session.get("visits"), ok);
	visits = (ok ? visits : 0) + 1;
	session.set("visits", StringUtils::toString(visits));

	const std::string visitsText = StringUtils::toString(visits);
	std::string body =
		"<!DOCTYPE html>\r\n"
		"<html lang=\"en\">\r\n"
		"<head>\r\n"
		"<meta charset=\"utf-8\">\r\n"
		"<title>Session demo</title>\r\n"
		"<link rel=\"stylesheet\" href=\"/style.css\">\r\n"
		"</head>\r\n"
		"<body>\r\n"
		"<main id=\"galaxy\"></main>\r\n"
		"<h1>Session demo</h1>\r\n"
		"<p>Session id: <code>" + session.id() + "</code></p>\r\n"
		"<p>This is visit number <strong>" + visitsText + "</strong>.</p>\r\n"
		"<p>" + std::string(isNew
			? "A new session was created and sent to you as a cookie."
			: "Your browser sent an existing session cookie back.") + "</p>\r\n"
		"<p>Reload the page to see the counter go up, or clear the "
		"<code>sid</code> cookie to start a new session.</p>\r\n"
		"<p><a href=\"/\">Back to the index</a></p>\r\n"
		"<script src=\"/main.js\"></script>\r\n"
		"</body>\r\n"
		"</html>\r\n";

	Response resp(HTTP_OK);
	resp.setHeader("Content-Type", "text/html; charset=utf-8");
	if (isNew) {
		resp.setCookie(kSessionCookie, session.id(), "Path=/; HttpOnly");
	}
	resp.setBody(body);
	return resp;
}
