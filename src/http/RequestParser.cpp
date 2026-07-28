#include "http/RequestParser.hpp"
#include "common/HttpStatus.hpp"
#include "common/StringUtils.hpp"
#include <cctype>


// Teto de bytes acumulados antes do body (URI 8K + headers 8K).
static const std::size_t kMaxPreBodyBytes = 16384;
static const std::size_t kMaxUriLength    = 8192;

// tchar (RFC 7230 3.2.6)
static bool isToken(const std::string& s) {
	static const std::string extra = "!#$%&'*+-.^_`|~";
	if (s.empty()) return false;
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		if (!std::isalnum(static_cast<unsigned char>(s[i]))
		 && extra.find(s[i]) == std::string::npos) return false;
	}
	return true;
}

static bool hasCtl(const std::string& s) {
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (std::iscntrl(c) || c == ' ') return true;
	}
	return false;
}

static RequestParser::FeedResult errorToResult(int status) {
	switch (status) {
		case HTTP_URI_TOO_LONG:          return RequestParser::URI_TOO_LONG;
		case HTTP_PAYLOAD_TOO_LARGE:     return RequestParser::BODY_TOO_LARGE;
		case HTTP_VERSION_NOT_SUPPORTED: return RequestParser::HTTP_VERSION_UNSUPPORTED;
		default:                         return RequestParser::BAD_REQUEST;
	}
}

RequestParser::RequestParser()
	: state_(METHOD)
	, building_()
	, buf_()
	, bytesRead_(0)
	, contentLength_(0)
	, chunked_(false)
	, errorStatus_(0)
{}

RequestParser::~RequestParser() {}

void RequestParser::reset() {
	take();       // rearma o estado de parsing
	buf_.clear(); // ... e descarta o buffer
}

RequestParser::FeedResult RequestParser::feed(const char* data,
                                              std::size_t n,
                                              std::size_t maxBody) {
	if (state_ == ERROR) return errorToResult(errorStatus_);
	if (n) buf_.append(data, n); // n == 0: só processa o que já está no buffer

	for (;;) {
		if (state_ == DONE)  return COMPLETE;
		if (state_ == ERROR) return errorToResult(errorStatus_);

		// backstop só para HEADER; METHOD tem o 414 do parseRequestLine
		if (state_ == HEADER && buf_.size() > kMaxPreBodyBytes) {
			state_       = ERROR;
			errorStatus_ = HTTP_BAD_REQUEST;
			return BAD_REQUEST;
		}

		State      before = state_;
		FeedResult r      = NEED_MORE;

		switch (state_) {
			case METHOD:
			case URI:
			case VERSION:      r = parseRequestLine();         break;
			case HEADER:       r = parseHeaders();             break;
			case BODY_LENGTH:  r = parseBodyByLength(maxBody); break;
			case BODY_CHUNKED: r = parseBodyChunked(maxBody);  break;
			case DONE:
			case ERROR:        break; // inalcançável; silencia -Wswitch
		}

		if (r != NEED_MORE)   return r;         // COMPLETE ou erro
		if (state_ == before) return NEED_MORE; // não progrediu: faltam bytes
	}
}

bool                 RequestParser::complete() const { return state_ == DONE; }
const Request&       RequestParser::current() const  { return building_; }

Request RequestParser::take() {
	Request out    = building_;
	state_         = METHOD;
	building_      = Request();
	bytesRead_     = 0;
	contentLength_ = 0;
	chunked_       = false;
	errorStatus_   = 0;
	return out; // buf_ preservado: pode conter a próxima request (pipelining)
}

int                  RequestParser::errorStatus() const { return errorStatus_; }
RequestParser::State RequestParser::state() const    { return state_; }

RequestParser::FeedResult RequestParser::parseRequestLine() {
	std::string::size_type eol = buf_.find("\r\n");
	if (eol == std::string::npos) {
		if (buf_.size() > kMaxUriLength + 64) { // linha passou do teto sem fechar
			state_ = ERROR; errorStatus_ = HTTP_URI_TOO_LONG;
			return URI_TOO_LONG;
		}
		return NEED_MORE;
	}

	const std::string line = buf_.substr(0, eol);
	buf_.erase(0, eol + 2);

	// exatamente 3 tokens separados por um SP
	std::string::size_type sp1 = line.find(' ');
	std::string::size_type sp2 = (sp1 == std::string::npos)
	                           ? std::string::npos : line.find(' ', sp1 + 1);
	if (sp1 == std::string::npos || sp2 == std::string::npos
	 || line.find(' ', sp2 + 1) != std::string::npos) {
		state_ = ERROR; errorStatus_ = HTTP_BAD_REQUEST;
		return BAD_REQUEST;
	}

	const std::string method  = line.substr(0, sp1);
	const std::string uri     = line.substr(sp1 + 1, sp2 - sp1 - 1);
	const std::string version = line.substr(sp2 + 1);

	if (uri.size() > kMaxUriLength) {
		state_ = ERROR; errorStatus_ = HTTP_URI_TOO_LONG;
		return URI_TOO_LONG;
	}
	if (!isToken(method) || uri.empty() || uri[0] != '/' || hasCtl(uri)) {
		state_ = ERROR; errorStatus_ = HTTP_BAD_REQUEST;
		return BAD_REQUEST;
	}
	if (version != "HTTP/1.1" && version != "HTTP/1.0") {
		state_ = ERROR;
		if (StringUtils::startsWith(version, "HTTP/")) {
			errorStatus_ = HTTP_VERSION_NOT_SUPPORTED;
			return HTTP_VERSION_UNSUPPORTED;
		}
		errorStatus_ = HTTP_BAD_REQUEST;
		return BAD_REQUEST;
	}

	building_.method_  = method;
	building_.uri_     = uri;
	building_.version_ = version;
	splitUri();
	state_ = HEADER;
	return NEED_MORE; // progresso sinalizado pela mudança de state_
}
RequestParser::FeedResult RequestParser::parseHeaders()     { return NEED_MORE; }
RequestParser::FeedResult RequestParser::parseBodyByLength(std::size_t) { return NEED_MORE; }
RequestParser::FeedResult RequestParser::parseBodyChunked(std::size_t)  { return NEED_MORE; }

void RequestParser::splitUri() {
	std::string::size_type q = building_.uri_.find('?');
	if (q == std::string::npos) {
		building_.path_ = building_.uri_;
		building_.query_.clear();
	} else {
		building_.path_  = building_.uri_.substr(0, q);
		building_.query_ = building_.uri_.substr(q + 1);
	}
}

