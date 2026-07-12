#ifndef WEBSERV_COMMON_TYPES_HPP
#define WEBSERV_COMMON_TYPES_HPP

#include <string>
#include <map>
#include <vector>


struct CaseInsensitiveLess {
	bool operator()(const std::string& a, const std::string& b) const;
};

typedef std::map<std::string, std::string, CaseInsensitiveLess> HeaderMap;
typedef std::vector<std::string>                                StringVec;


#endif
