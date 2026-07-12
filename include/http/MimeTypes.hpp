#ifndef WEBSERV_HTTP_MIME_TYPES_HPP
#define WEBSERV_HTTP_MIME_TYPES_HPP

#include <string>

class MimeTypes {
public:
	static std::string fromExtension(const std::string& ext);
	static std::string fromPath(const std::string& path);

private:
	MimeTypes();
};

#endif
