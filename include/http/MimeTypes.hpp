#ifndef WEBSERV_HTTP_MIME_TYPES_HPP
#define WEBSERV_HTTP_MIME_TYPES_HPP

#include <string>

namespace ws {
namespace mime {

std::string fromExtension(const std::string& ext);
std::string fromPath(const std::string& path);

}
}

#endif
