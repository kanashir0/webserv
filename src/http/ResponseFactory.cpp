#include "http/ResponseFactory.hpp"
#include "http/PathResolver.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/StringUtils.hpp"
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

static int readRegularFile(const std::string& path, std::string& outContent) {
	if (path.empty()) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	struct stat st;
	if (stat(path.c_str(), &st) != 0) {
		return HTTP_NOT_FOUND;
	}
	if (!S_ISREG(st.st_mode) || access(path.c_str(), R_OK) != 0) {
		return HTTP_FORBIDDEN;
	}
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in.is_open()) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	std::ostringstream buf;
	buf << in.rdbuf();
	if (in.bad()) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	outContent = buf.str();
	return HTTP_OK;
}

static const std::string* findRootForUri(const std::string& uriPath, const ServerConfig& cfg) {
	const std::string*     bestRoot   = 0;
	std::string::size_type bestLength = 0;
	for (std::vector<LocationConfig>::const_iterator it = cfg.locations.begin();
	     it != cfg.locations.end(); ++it) {
		if (it->path.empty() || it->root.empty()) {
			continue;
		}
		if (uriPath.compare(0, it->path.size(), it->path) != 0) {
			continue;
		}
		if (bestRoot == 0 || it->path.size() > bestLength) {
			bestRoot   = &it->root;
			bestLength = it->path.size();
		}
	}
	if (bestRoot == 0 && !cfg.root.empty()) {
		return &cfg.root;
	}
	return bestRoot;
}

static bool loadConfiguredErrorPage(int code, const ServerConfig& cfg, std::string& outBody) {
	std::map<int, std::string>::const_iterator it = cfg.errorPages.find(code);
	if (it == cfg.errorPages.end() || it->second.empty()) {
		return false;
	}
	const std::string& pageUri = it->second;

	const std::string* root = findRootForUri(pageUri, cfg);
	if (root != 0 && readRegularFile(PathResolver::joinPath(*root, pageUri), outBody) == HTTP_OK &&
	    !outBody.empty()) {
		return true;
	}
	if (readRegularFile(pageUri, outBody) == HTTP_OK && !outBody.empty()) {
		return true;
	}

	LOG_ERROR("makeError: error page configurada nao pode ser carregada (codigo " +
	          StringUtils::toString(static_cast<long>(code)) + "): \"" + pageUri + "\"");
	outBody.clear();
	return false;
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

static Response buildErrorResponse(int code, const std::string& body) {
	Response r(code);
	r.setHeader("Content-Type", "text/html; charset=utf-8");
	r.setBody(body);
	return r;
}

Response ResponseFactory::makeError(int code, const ServerConfig& cfg) {
	std::string body;
	if (!loadConfiguredErrorPage(code, cfg, body)) {
		body = makeBuiltinErrorPage(code);
	}
	return buildErrorResponse(code, body);
}

static bool isSupportedRedirectCode(int code) {
	return code == HTTP_MOVED_PERMANENTLY || code == HTTP_FOUND;
}

Response ResponseFactory::makeRedirect(const std::string& url, int code) {
	if (!isSupportedRedirectCode(code)) {
		LOG_ERROR("makeRedirect: codigo de redirecionamento nao suportado: " +
		          StringUtils::toString(static_cast<long>(code)));
		return buildErrorResponse(HTTP_INTERNAL_SERVER_ERROR,
		                          makeBuiltinErrorPage(HTTP_INTERNAL_SERVER_ERROR));
	}
	if (url.empty()) {
		LOG_ERROR("makeRedirect: URL de destino vazia (codigo " +
		          StringUtils::toString(static_cast<long>(code)) + ")");
		return buildErrorResponse(HTTP_INTERNAL_SERVER_ERROR,
		                          makeBuiltinErrorPage(HTTP_INTERNAL_SERVER_ERROR));
	}
	Response redirectResponse(code);
	redirectResponse.setHeader("Location", url);
	redirectResponse.setBody("");
	return redirectResponse;
}

Response ResponseFactory::makeFile(const std::string& fsPath,
                                   const std::string& mime,
                                   const ServerConfig& cfg) {
	std::string fileContent;
	int status = readRegularFile(fsPath, fileContent);
	if (status != HTTP_OK) {
		LOG_DEBUG("makeFile: " + StringUtils::toString(static_cast<long>(status)) +
		          " para \"" + fsPath + "\"");
		return makeError(status, cfg);
	}
	Response r(HTTP_OK);
	if (!mime.empty()) {
		r.setHeader("Content-Type", mime);
	}
	r.setBody(fileContent);
	return r;
}

static std::string escapeHtml(const std::string& text) {
	std::string escaped;
	escaped.reserve(text.size());
	for (std::string::size_type i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == '&') {
			escaped += "&amp;";
		} else if (c == '<') {
			escaped += "&lt;";
		} else if (c == '>') {
			escaped += "&gt;";
		} else if (c == '"') {
			escaped += "&quot;";
		} else if (c == '\'') {
			escaped += "&#39;";
		} else {
			escaped += c;
		}
	}
	return escaped;
}

static std::string withTrailingSlash(const std::string& uriPath) {
	if (uriPath.empty()) {
		return "/";
	}
	if (uriPath[uriPath.size() - 1] == '/') {
		return uriPath;
	}
	return uriPath + "/";
}

static int classifyDirectory(const std::string& fsPath) {
	if (fsPath.empty()) {
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	struct stat entryInfo;
	if (stat(fsPath.c_str(), &entryInfo) != 0) {
		return HTTP_NOT_FOUND;
	}
	if (!S_ISDIR(entryInfo.st_mode)) {
		return HTTP_FORBIDDEN;
	}

	if (access(fsPath.c_str(), R_OK | X_OK) != 0) {
		return HTTP_FORBIDDEN;
	}
	return HTTP_OK;
}

static bool isDirectoryEntry(const std::string& parentPath, const std::string& entryName) {
	struct stat entryInfo;
	if (stat(PathResolver::joinPath(parentPath, entryName).c_str(), &entryInfo) != 0) {
		return false;
	}
	return S_ISDIR(entryInfo.st_mode);
}

Response ResponseFactory::makeAutoindex(const std::string& fsPath,
                                        const std::string& uriPath,
                                        const ServerConfig& cfg) {
	int status = classifyDirectory(fsPath);
	if (status != HTTP_OK) {
		LOG_DEBUG("makeAutoindex: " + StringUtils::toString(static_cast<long>(status)) +
		          " para \"" + fsPath + "\"");
		return makeError(status, cfg);
	}
	DIR* directory = opendir(fsPath.c_str());
	if (directory == 0) {
		LOG_ERROR("makeAutoindex: opendir falhou em \"" + fsPath + "\"");
		return makeError(HTTP_INTERNAL_SERVER_ERROR, cfg);
	}

	StringVec entryNames;
	for (struct dirent* entry = readdir(directory); entry != 0; entry = readdir(directory)) {
		std::string entryName(entry->d_name);
		if (entryName == "." || entryName == "..") {
			continue;
		}
		if (isDirectoryEntry(fsPath, entryName)) {
			entryName += "/";
		}
		entryNames.push_back(entryName);
	}
	closedir(directory);
	std::sort(entryNames.begin(), entryNames.end());

	const std::string baseUri     = withTrailingSlash(uriPath);
	const std::string escapedBase = escapeHtml(baseUri);

	std::string page =
		"<!DOCTYPE html>\r\n"
		"<html>\r\n"
		"<head>\r\n"
		"<meta charset=\"utf-8\">\r\n"
		"<title>Index of " + escapedBase + "</title>\r\n"
		"</head>\r\n"
		"<body>\r\n"
		"<h1>Index of " + escapedBase + "</h1>\r\n"
		"<hr>\r\n"
		"<ul>\r\n";

	if (baseUri != "/") {
		page += "<li><a href=\"" + escapeHtml(baseUri + "../") + "\">../</a></li>\r\n";
	}
	for (StringVec::const_iterator it = entryNames.begin(); it != entryNames.end(); ++it) {
		const std::string& displayName = *it;
		bool isDirectory = (displayName[displayName.size() - 1] == '/');
		std::string bareName = isDirectory
			? displayName.substr(0, displayName.size() - 1)
			: displayName;
		std::string href = baseUri + PathResolver::encodeSegment(bareName) + (isDirectory ? "/" : "");
		page += "<li><a href=\"" + escapeHtml(href) + "\">" + escapeHtml(displayName) + "</a></li>\r\n";
	}

	page +=
		"</ul>\r\n"
		"<hr>\r\n"
		"<p>webserv</p>\r\n"
		"</body>\r\n"
		"</html>\r\n";

	Response r(HTTP_OK);
	r.setHeader("Content-Type", "text/html; charset=utf-8");
	r.setBody(page);
	return r;
}

// RFC 3875 §6: saida CGI = headers, linha em branco, body. O terminador pode
// ser \r\n\r\n ou \n\n dependendo do script; aceitamos os dois.
Response ResponseFactory::makeFromCgi(const std::string& rawCgiOutput) {
	std::string::size_type headerEnd = rawCgiOutput.find("\r\n\r\n");
	std::string::size_type bodyStart;
	std::string            lineSep;
	if (headerEnd != std::string::npos) {
		bodyStart = headerEnd + 4;
		lineSep   = "\r\n";
	} else {
		headerEnd = rawCgiOutput.find("\n\n");
		if (headerEnd == std::string::npos) {
			LOG_ERROR("makeFromCgi: saida CGI sem separador de headers");
			return Response(HTTP_BAD_GATEWAY);
		}
		bodyStart = headerEnd + 2;
		lineSep   = "\n";
	}

	Response resp(HTTP_OK);
	std::string            headerBlock = rawCgiOutput.substr(0, headerEnd);
	std::string::size_type lineStart   = 0;
	while (lineStart < headerBlock.size()) {
		std::string::size_type lineEnd = headerBlock.find(lineSep, lineStart);
		std::string line = (lineEnd == std::string::npos)
			? headerBlock.substr(lineStart)
			: headerBlock.substr(lineStart, lineEnd - lineStart);
		lineStart = (lineEnd == std::string::npos)
			? headerBlock.size()
			: lineEnd + lineSep.size();

		std::string::size_type colon = line.find(':');
		if (colon == std::string::npos || colon == 0) {
			LOG_WARN("makeFromCgi: header CGI malformado: \"" + line + "\"");
			return Response(HTTP_BAD_GATEWAY);
		}
		std::string name  = StringUtils::trim(line.substr(0, colon));
		std::string value = StringUtils::trim(line.substr(colon + 1));

		if (StringUtils::iequals(name, "Status")) {
			// Formato: "404 Not Found" ou apenas "404".
			bool ok = false;
			long code = StringUtils::toLong(value.substr(0, value.find(' ')), ok);
			if (!ok || code < 100 || code > 599) {
				LOG_WARN("makeFromCgi: Status CGI invalido: \"" + value + "\"");
				return Response(HTTP_BAD_GATEWAY);
			}
			resp.setStatus(static_cast<int>(code));
		} else {
			resp.setHeader(name, value);
		}
	}

	resp.setBody(rawCgiOutput.substr(bodyStart));
	return resp;
}
