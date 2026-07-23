#include "http/Response.hpp"
#include "common/HttpStatus.hpp"
#include "common/StringUtils.hpp"
#include <sstream>

namespace {
	std::string sanitizeHeaderValue(const std::string& value) {
		std::string sanitized;
		sanitized.reserve(value.size());
		for (std::string::size_type i = 0; i < value.size(); ++i) {
			char c = value[i];
			if (c == '\r' || c == '\n' || c == ':') {
				if (sanitized.empty() || sanitized[sanitized.size() - 1] != ' ') {
					sanitized += ' ';
				}
			} else {
				sanitized += c;
			}
		}
		while (!sanitized.empty() && sanitized[sanitized.size() - 1] == ' ') {
			sanitized.erase(sanitized.size() - 1);
		}
		return sanitized;
	}
}

Response::Response() : status_(200), headers_(), cookies_(), body_() {}
Response::Response(int status) : status_(status), headers_(), cookies_(), body_() {}
Response::~Response() {}

void Response::setStatus(int code) { status_ = code; }

void Response::setHeader(const std::string& key, const std::string& value) {
	std::string sanitized = sanitizeHeaderValue(value);
	headers_[key] = sanitized;
}

void Response::setBody(const std::string& body) {
	body_ = body;
	headers_["Content-Length"] = StringUtils::toString(static_cast<long>(body_.size()));
}

void Response::appendBody(const std::string& chunk) {
	body_ += chunk;
	headers_["Content-Length"] = StringUtils::toString(static_cast<long>(body_.size()));
}

void Response::setCookie(const std::string& name,
                         const std::string& value,
                         const std::string& options) {
	std::string cookie = sanitizeHeaderValue(name + "=" + value);
	if (!options.empty()) {
		cookie += "; " + sanitizeHeaderValue(options);
	}
	cookies_.push_back(cookie);
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
	for (StringVec::const_iterator it = cookies_.begin(); it != cookies_.end(); ++it) {
		oss << "Set-Cookie: " << *it << "\r\n";
	}
	oss << "\r\n";
	oss << body_;
	return oss.str();
}

