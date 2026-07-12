#include "http/Response.hpp"
#include "common/HttpStatus.hpp"
#include "common/StringUtils.hpp"
#include <sstream>


Response::Response() : status_(200), headers_(), body_() {}
Response::Response(int status) : status_(status), headers_(), body_() {}
Response::~Response() {}

void Response::setStatus(int code) { status_ = code; }

void Response::setHeader(const std::string& key, const std::string& value) {
	headers_[key] = value;
}

void Response::setBody(const std::string& body) {
	body_ = body;
	headers_["Content-Length"] = StringUtils::toString(static_cast<long>(body_.size()));
}

void Response::appendBody(const std::string& chunk) {
	body_ += chunk;
	headers_["Content-Length"] = StringUtils::toString(static_cast<long>(body_.size()));
}

void Response::setCookie(const std::string& /*name*/,
                         const std::string& /*value*/,
                         const std::string& /*options*/) {
	// TODO Membro 3: montar Set-Cookie header com possibilidade de multiplos
}

int                Response::status()  const { return status_; }
const HeaderMap&   Response::headers() const { return headers_; }
const std::string& Response::body()    const { return body_; }

std::string Response::toString() const {
	std::ostringstream oss;
	oss << "HTTP/1.1 " << status_ << " " << statusReason(status_) << "\r\n";
	for (HeaderMap::const_iterator it = headers_.begin(); it != headers_.end(); ++it) {
		oss << it->first << ": " << it->second << "\r\n";
	}
	oss << "\r\n";
	oss << body_;
	return oss.str();
}

