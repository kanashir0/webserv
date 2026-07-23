#ifndef WEBSERV_HTTP_RESPONSE_HPP
#define WEBSERV_HTTP_RESPONSE_HPP

#include "common/Types.hpp"
#include <string>


class Response {
public:
	Response();
	explicit Response(int status);
	~Response();

	void setStatus(int code);
	void setHeader(const std::string& key, const std::string& value);
	void setBody(const std::string& body);
	void appendBody(const std::string& chunk);
	void setCookie(const std::string& name,
	               const std::string& value,
	               const std::string& options = "Path=/");

	int                status() const;
	const HeaderMap&   headers() const;
	const std::string& body() const;

	std::string toString() const;

private:
	int         status_;
	HeaderMap   headers_;
	StringVec   cookies_;
	std::string body_;
};


#endif
