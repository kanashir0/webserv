#ifndef WEBSERV_COMMON_HTTP_STATUS_HPP
#define WEBSERV_COMMON_HTTP_STATUS_HPP

#include <string>

namespace ws {

enum HttpStatus {
	HTTP_OK                    = 200,
	HTTP_CREATED               = 201,
	HTTP_NO_CONTENT            = 204,
	HTTP_MOVED_PERMANENTLY     = 301,
	HTTP_FOUND                 = 302,
	HTTP_BAD_REQUEST           = 400,
	HTTP_FORBIDDEN             = 403,
	HTTP_NOT_FOUND             = 404,
	HTTP_METHOD_NOT_ALLOWED    = 405,
	HTTP_REQUEST_TIMEOUT       = 408,
	HTTP_LENGTH_REQUIRED       = 411,
	HTTP_PAYLOAD_TOO_LARGE     = 413,
	HTTP_URI_TOO_LONG          = 414,
	HTTP_INTERNAL_SERVER_ERROR = 500,
	HTTP_NOT_IMPLEMENTED       = 501,
	HTTP_VERSION_NOT_SUPPORTED = 505
};

const char* statusReason(int code);

}

#endif
