#include "common/StringUtils.hpp"
#include <cctype>
#include <sstream>
#include <cstdlib>

std::string StringUtils::trim(const std::string& s) {
	std::string::size_type a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
	return s.substr(a, b - a);
}

std::string StringUtils::toLower(const std::string& s) {
	std::string r = s;
	for (std::string::size_type i = 0; i < r.size(); ++i)
		r[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(r[i])));
	return r;
}

std::string StringUtils::toUpper(const std::string& s) {
	std::string r = s;
	for (std::string::size_type i = 0; i < r.size(); ++i)
		r[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(r[i])));
	return r;
}

StringVec StringUtils::split(const std::string& s, char delim) {
	StringVec out;
	std::string::size_type start = 0, pos;
	while ((pos = s.find(delim, start)) != std::string::npos) {
		out.push_back(s.substr(start, pos - start));
		start = pos + 1;
	}
	out.push_back(s.substr(start));
	return out;
}

StringVec StringUtils::splitAny(const std::string& s, const std::string& delims) {
	StringVec out;
	std::string cur;
	for (std::string::size_type i = 0; i < s.size(); ++i) {
		if (delims.find(s[i]) != std::string::npos) {
			if (!cur.empty()) { out.push_back(cur); cur.clear(); }
		} else {
			cur += s[i];
		}
	}
	if (!cur.empty()) out.push_back(cur);
	return out;
}

bool StringUtils::startsWith(const std::string& s, const std::string& prefix) {
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool StringUtils::endsWith(const std::string& s, const std::string& suffix) {
	return s.size() >= suffix.size()
	    && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool StringUtils::iequals(const std::string& a, const std::string& b) {
	if (a.size() != b.size()) return false;
	for (std::string::size_type i = 0; i < a.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(a[i]))
		 != std::tolower(static_cast<unsigned char>(b[i]))) return false;
	}
	return true;
}

std::string StringUtils::toString(long n) {
	std::ostringstream oss;
	oss << n;
	return oss.str();
}

long StringUtils::toLong(const std::string& s, bool& ok) {
	char* end = 0;
	const char* c = s.c_str();
	long v = std::strtol(c, &end, 10);
	ok = (end != c && *end == '\0');
	return v;
}
