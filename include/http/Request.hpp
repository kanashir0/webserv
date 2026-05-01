#ifndef WEBSERV_HTTP_REQUEST_HPP
#define WEBSERV_HTTP_REQUEST_HPP

#include "common/Types.hpp"
#include <string>

namespace ws {

class RequestParser;

class Request {
public:
	Request();
	~Request();

	const std::string& method()  const;
	const std::string& uri()     const;
	const std::string& path()    const;
	const std::string& query()   const;
	const std::string& version() const;
	const HeaderMap&   headers() const;
	const std::string& body()    const;

	std::string header(const std::string& name) const;
	bool        hasHeader(const std::string& name) const;
	std::string cookie(const std::string& name) const;
	bool        keepAlive() const;

	friend class RequestParser;

private:
	std::string method_;
	std::string uri_;
	std::string path_;
	std::string query_;
	std::string version_;
	std::string body_;
	HeaderMap   headers_;
};

}

#endif
