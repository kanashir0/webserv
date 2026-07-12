#ifndef WEBSERV_HTTP_REQUEST_PARSER_HPP
#define WEBSERV_HTTP_REQUEST_PARSER_HPP

#include "http/Request.hpp"
#include <string>
#include <cstddef>


class RequestParser {
public:
	enum State {
		METHOD,
		URI,
		VERSION,
		HEADER,
		BODY_LENGTH,
		BODY_CHUNKED,
		DONE,
		ERROR
	};

	enum FeedResult {
		NEED_MORE,
		COMPLETE,
		BAD_REQUEST,
		URI_TOO_LONG,
		BODY_TOO_LARGE,
		HTTP_VERSION_UNSUPPORTED
	};

	RequestParser();
	~RequestParser();

	void       reset();
	FeedResult feed(const char* data, std::size_t n, std::size_t maxBody);

	bool        complete() const;
	const Request& current() const;
	Request     take();

	int   errorStatus() const;
	State state() const;

private:
	State        state_;
	Request      building_;
	std::string  buf_;
	std::size_t  bytesRead_;
	std::size_t  contentLength_;
	bool         chunked_;
	int          errorStatus_;

	FeedResult parseRequestLine();
	FeedResult parseHeaders();
	FeedResult parseBodyByLength(std::size_t maxBody);
	FeedResult parseBodyChunked(std::size_t maxBody);
	void       splitUri();

	RequestParser(const RequestParser&);
	RequestParser& operator=(const RequestParser&);
};


#endif
