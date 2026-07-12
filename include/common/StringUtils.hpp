#ifndef WEBSERV_COMMON_STRING_UTILS_HPP
#define WEBSERV_COMMON_STRING_UTILS_HPP

#include "common/Types.hpp"
#include <string>

class StringUtils {
public:
	static std::string trim(const std::string& s);
	static std::string toLower(const std::string& s);
	static std::string toUpper(const std::string& s);
	static StringVec   split(const std::string& s, char delim);
	static StringVec   splitAny(const std::string& s, const std::string& delims);
	static bool        startsWith(const std::string& s, const std::string& prefix);
	static bool        endsWith(const std::string& s, const std::string& suffix);
	static bool        iequals(const std::string& a, const std::string& b);
	static std::string toString(long n);
	static long        toLong(const std::string& s, bool& ok);

private:
	StringUtils();
};

#endif
