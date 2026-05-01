#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/StringUtils.hpp"

namespace ws {
namespace ResponseFactory {

Response makeError(int code, const ServerConfig& /*cfg*/) {
	Response r(code);
	std::string body = str::toString(code) + " " + statusReason(code);
	r.setHeader("Content-Type", "text/plain");
	r.setBody(body);
	return r;
}

Response makeRedirect(const std::string& url, int code) {
	Response r(code);
	r.setHeader("Location", url);
	r.setBody("");
	return r;
}

Response makeFile(const std::string& /*fsPath*/, const std::string& /*mime*/) {
	// TODO Membro 3
	return Response(200);
}

Response makeAutoindex(const std::string& /*fsPath*/, const std::string& /*uriPath*/) {
	// TODO Membro 3
	return Response(200);
}

Response makeFromCgi(const std::string& /*rawCgiOutput*/) {
	// TODO Membro 3
	return Response(200);
}

}
}
