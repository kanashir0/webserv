#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/StringUtils.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>

static bool readRegularFile(const std::string& path, std::string& outContent) {
	if (path.empty()) {
		return false;
	}
	struct stat st;
	if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return false;
	}
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in.is_open()) {
		return false;
	}
	std::ostringstream buf;
	buf << in.rdbuf();
	if (in.bad()) {
		return false;
	}
	outContent = buf.str();
	return true;
}

static std::string joinPath(const std::string& root, const std::string& rel) {
	if (root.empty()) {
		return rel;
	}
	std::string joined = root;
	bool rootHasSlash = (joined[joined.size() - 1] == '/');
	bool relHasSlash  = (!rel.empty() && rel[0] == '/');
	if (rootHasSlash && relHasSlash) {
		joined.erase(joined.size() - 1);
	} else if (!rootHasSlash && !relHasSlash) {
		joined += '/';
	}
	joined += rel;
	return joined;
}

static bool loadConfiguredErrorPage(int code, const ServerConfig& cfg, std::string& outBody) {
	std::map<int, std::string>::const_iterator it = cfg.errorPages.find(code);
	if (it == cfg.errorPages.end() || it->second.empty()) {
		return false;
	}
	const std::string& pageUri = it->second;
	const LocationConfig* loc = cfg.findLocation(pageUri);
	if (loc && !loc->root.empty() &&
		readRegularFile(joinPath(loc->root, pageUri), outBody)) {
		return true;
	}
	return readRegularFile(pageUri, outBody);
}

static std::string makeBuiltinErrorPage(int code) {
	std::string title = StringUtils::toString(code) + " " + statusReason(code);
	return "<html>\r\n"
		   "<head><title>" + title + "</title></head>\r\n"
		   "<body>\r\n"
		   "<h1>" + title + "</h1>\r\n"
		   "<hr>\r\n"
		   "<p>webserv</p>\r\n"
		   "</body>\r\n"
		   "</html>\r\n";
}

Response ResponseFactory::makeError(int code, const ServerConfig& cfg) {
	Response r(code);
	std::string body;
	if (!loadConfiguredErrorPage(code, cfg, body)) {
		body = makeBuiltinErrorPage(code);
	}
	r.setHeader("Content-Type", "text/html; charset=utf-8");
	r.setBody(body);
	return r;
}

Response ResponseFactory::makeRedirect(const std::string& url, int code) {
	Response r(code);
	r.setHeader("Location", url);
	r.setBody("");
	return r;
}

Response ResponseFactory::makeFile(const std::string& /*fsPath*/, const std::string& /*mime*/) {
	// TODO Membro 3
	return Response(200);
}

Response ResponseFactory::makeAutoindex(const std::string& /*fsPath*/, const std::string& /*uriPath*/) {
	// TODO Membro 3
	return Response(200);
}

Response ResponseFactory::makeFromCgi(const std::string& /*rawCgiOutput*/) {
	// TODO Membro 3
	return Response(200);
}
