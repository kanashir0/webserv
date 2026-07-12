#include "http/Request.hpp"
#include "common/StringUtils.hpp"


Request::Request()
	: method_()
	, uri_()
	, path_()
	, query_()
	, version_()
	, body_()
	, headers_()
{}

Request::~Request() {}

const std::string& Request::method()  const { return method_; }
const std::string& Request::uri()     const { return uri_; }
const std::string& Request::path()    const { return path_; }
const std::string& Request::query()   const { return query_; }
const std::string& Request::version() const { return version_; }
const HeaderMap&   Request::headers() const { return headers_; }
const std::string& Request::body()    const { return body_; }

std::string Request::header(const std::string& name) const {
	HeaderMap::const_iterator it = headers_.find(name);
	if (it == headers_.end()) return std::string();
	return it->second;
}

bool Request::hasHeader(const std::string& name) const {
	return headers_.find(name) != headers_.end();
}

std::string Request::cookie(const std::string& /*name*/) const {
	// TODO Membro 3: parsear header "Cookie" e retornar valor
	return std::string();
}

bool Request::keepAlive() const {
	std::string c = header("Connection");
	if (StringUtils::iequals(c, "close")) return false;
	if (version_ == "HTTP/1.0") return StringUtils::iequals(c, "keep-alive");
	return true;
}

