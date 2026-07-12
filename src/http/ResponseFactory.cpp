#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/StringUtils.hpp"

Response ResponseFactory::makeError(int code, const ServerConfig& /*cfg*/) {
	Response r(code);
	std::string body = StringUtils::toString(code) + " " + statusReason(code);
	r.setHeader("Content-Type", "text/plain");
	r.setBody(body);
	return r;
}

Response ResponseFactory::makeRedirect(const std::string& url, int code) {
	Response r(code);
	r.setHeader("Location", url);
	r.setBody("");
	return r;
}

Response ResponseFactory::makeFile(const std::string& /*fsPath*/, const std::string& /*mime*/) {
	// TODO Membro 3
	return Response(200);
}

Response ResponseFactory::makeAutoindex(const std::string& /*fsPath*/, const std::string& /*uriPath*/) {
	// TODO Membro 3
	return Response(200);
}

Response ResponseFactory::makeFromCgi(const std::string& /*rawCgiOutput*/) {
	// TODO Membro 3
	return Response(200);
}
