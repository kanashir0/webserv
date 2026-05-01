#ifndef WEBSERV_COMMON_STRING_UTILS_HPP
#define WEBSERV_COMMON_STRING_UTILS_HPP

#include "common/Types.hpp"
#include <string>

namespace ws {
namespace str {

std::string trim(const std::string& s);
std::string toLower(const std::string& s);
std::string toUpper(const std::string& s);
StringVec   split(const std::string& s, char delim);
StringVec   splitAny(const std::string& s, const std::string& delims);
bool        startsWith(const std::string& s, const std::string& prefix);
bool        endsWith(const std::string& s, const std::string& suffix);
bool        iequals(const std::string& a, const std::string& b);
std::string toString(long n);
long        toLong(const std::string& s, bool& ok);

}
}

#endif
