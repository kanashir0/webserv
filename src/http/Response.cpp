#include "http/Response.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/StringUtils.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>

static std::string httpDate() {
	static const char* days[]   = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
	                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	std::time_t now = std::time(0);
	std::tm*    utc = std::gmtime(&now);
	if (utc == 0) {
		return std::string();
	}

	std::ostringstream oss;
	oss << days[utc->tm_wday] << ", "
	    << std::setw(2) << std::setfill('0') << utc->tm_mday << " "
	    << months[utc->tm_mon] << " " << (utc->tm_year + 1900) << " "
	    << std::setw(2) << std::setfill('0') << utc->tm_hour << ":"
	    << std::setw(2) << std::setfill('0') << utc->tm_min << ":"
	    << std::setw(2) << std::setfill('0') << utc->tm_sec << " GMT";
	return oss.str();
}

static bool isValidHeaderKey(const std::string& key) {
	static const std::string separators = "()<>@,;:\\\"/[]?={}";
	if (key.empty()) {
		return false;
	}
	for (std::string::size_type i = 0; i < key.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(key[i]);
		if (c <= 32 || c >= 127 ||
		    separators.find(static_cast<char>(c)) != std::string::npos) {
			return false;
		}
	}
	return true;
}

static std::string sanitizeHeaderValue(const std::string& value) {
	std::string sanitized;
	sanitized.reserve(value.size());
	for (std::string::size_type i = 0; i < value.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(value[i]);
		if (c >= 32 && c != 127) {
			sanitized += static_cast<char>(c);
		} else if (!sanitized.empty() && sanitized[sanitized.size() - 1] != ' ') {
			sanitized += ' ';
		}
	}
	return StringUtils::trim(sanitized);
}

Response::Response() : status_(200), headers_(), cookies_(), body_() {}
Response::Response(int status) : status_(status), headers_(), cookies_(), body_() {}
Response::~Response() {}

void Response::setStatus(int code) { status_ = code; }

void Response::setHeader(const std::string& key, const std::string& value) {
	if (!isValidHeaderKey(key)) {
		LOG_WARN("Response::setHeader: nome de header invalido, descartado: \"" + key + "\"");
		return;
	}
	if (StringUtils::iequals(key, "Content-Length")) {
		LOG_WARN("Response::setHeader: Content-Length e gerido por setBody(), descartado");
		return;
	}
	if (StringUtils::iequals(key, "Set-Cookie")) {
		cookies_.push_back(sanitizeHeaderValue(value));
		return;
	}
	headers_[key] = sanitizeHeaderValue(value);
}

void Response::setBody(const std::string& body) {
	body_ = body;
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
	std::string head = "HTTP/1.1 " + StringUtils::toString(status_) + " " +
	                   statusReason(status_) + "\r\n";
	if (headers_.find("Date") == headers_.end()) {
		head += "Date: " + httpDate() + "\r\n";
	}
	for (HeaderMap::const_iterator it = headers_.begin(); it != headers_.end(); ++it) {
		head += it->first + ": " + it->second + "\r\n";
	}
	for (StringVec::const_iterator it = cookies_.begin(); it != cookies_.end(); ++it) {
		head += "Set-Cookie: " + *it + "\r\n";
	}
	head += "\r\n";

	head.reserve(head.size() + body_.size());
	head += body_;
	return head;
}

