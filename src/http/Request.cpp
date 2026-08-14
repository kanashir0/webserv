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

// RFC 6265 §4.2.1: Cookie: name=value; name2=value2
std::string Request::cookie(const std::string& name) const {
	const std::string      header = this->header("Cookie");
	std::string::size_type start  = 0;

	while (start < header.size()) {
		std::string::size_type end  = header.find(';', start);
		std::string            pair = (end == std::string::npos)
			? header.substr(start)
			: header.substr(start, end - start);

		std::string::size_type eq = pair.find('=');
		if (eq != std::string::npos
		 && StringUtils::trim(pair.substr(0, eq)) == name) {
			return StringUtils::trim(pair.substr(eq + 1));
		}

		if (end == std::string::npos) break;
		start = end + 1;
	}
	return std::string();
}

bool Request::keepAlive() const {
	std::string c = header("Connection");
	if (StringUtils::iequals(c, "close")) return false;
	if (version_ == "HTTP/1.0") return StringUtils::iequals(c, "keep-alive");
	return true;
}

