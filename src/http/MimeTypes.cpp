#include "http/MimeTypes.hpp"
#include "common/StringUtils.hpp"

std::string MimeTypes::fromExtension(const std::string& ext) {
	std::string e = StringUtils::toLower(ext);
	if (e == "html" || e == "htm") return "text/html";
	if (e == "css")                return "text/css";
	if (e == "js")                 return "application/javascript";
	if (e == "json")               return "application/json";
	if (e == "txt")                return "text/plain";
	if (e == "png")                return "image/png";
	if (e == "jpg" || e == "jpeg") return "image/jpeg";
	if (e == "gif")                return "image/gif";
	if (e == "svg")                return "image/svg+xml";
	if (e == "ico")                return "image/x-icon";
	if (e == "pdf")                return "application/pdf";
	return "application/octet-stream";
}

std::string MimeTypes::fromPath(const std::string& path) {
	std::string::size_type dot = path.find_last_of('.');
	if (dot == std::string::npos) return "application/octet-stream";
	return fromExtension(path.substr(dot + 1));
}
