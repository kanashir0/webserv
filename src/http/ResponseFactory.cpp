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
	outContent.resize(static_cast<std::size_t>(st.st_size));
	if (st.st_size > 0) {
		in.read(&outContent[0], static_cast<std::streamsize>(st.st_size));
		if (in.bad()) {
			outContent.clear();
			return HTTP_INTERNAL_SERVER_ERROR;
		}
		// Arquivo encolheu entre o stat e o read: mantem so o que veio.
		outContent.resize(static_cast<std::size_t>(in.gcount()));
	}
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

static const std::string* findErrorPageUri(int code, const ServerConfig& cfg,
                                           const LocationConfig* loc) {
	if (loc != 0) {
		std::map<int, std::string>::const_iterator it = loc->errorPages.find(code);
		if (it != loc->errorPages.end() && !it->second.empty()) {
			return &it->second;
		}
	}
	std::map<int, std::string>::const_iterator it = cfg.errorPages.find(code);
	if (it != cfg.errorPages.end() && !it->second.empty()) {
		return &it->second;
	}
	return 0;
}

static bool loadConfiguredErrorPage(int code, const ServerConfig& cfg,
                                    const LocationConfig* loc, std::string& outBody) {
	const std::string* configured = findErrorPageUri(code, cfg, loc);
	if (configured == 0) {
		return false;
	}
	const std::string& pageUri = *configured;

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

Response ResponseFactory::makeError(int code, const ServerConfig& cfg,
                                    const LocationConfig* loc) {
	std::string body;
	if (!loadConfiguredErrorPage(code, cfg, loc, body)) {
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
                                   const ServerConfig& cfg,
                                   const LocationConfig* loc) {
	std::string fileContent;
	int status = readRegularFile(fsPath, fileContent);
	if (status != HTTP_OK) {
		LOG_DEBUG("makeFile: " + StringUtils::toString(static_cast<long>(status)) +
		          " para \"" + fsPath + "\"");
		return makeError(status, cfg, loc);
	}
	Response r(HTTP_OK);
	if (!mime.empty()) {
		r.setHeader("Content-Type", mime);
	}
	r.setBody(fileContent);
	return r;
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
                                        const ServerConfig& cfg,
                                        const LocationConfig* loc) {
	int status = classifyDirectory(fsPath);
	if (status != HTTP_OK) {
		LOG_DEBUG("makeAutoindex: " + StringUtils::toString(static_cast<long>(status)) +
		          " para \"" + fsPath + "\"");
		return makeError(status, cfg, loc);
	}
	DIR* directory = opendir(fsPath.c_str());
	if (directory == 0) {
		LOG_ERROR("makeAutoindex: opendir falhou em \"" + fsPath + "\"");
		return makeError(HTTP_INTERNAL_SERVER_ERROR, cfg, loc);
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
	const std::string escapedBase = StringUtils::escapeHtml(baseUri);
	const std::size_t estimatedSize = 512 + entryNames.size() * 96;

	std::string page;
	page.reserve(estimatedSize);
	page +=
		"<!DOCTYPE html>\r\n"
		"<html>\r\n"
		"<head>\r\n"
		"<meta charset=\"utf-8\">\r\n"
		"<title>Index of " + escapedBase + "</title>\r\n"
		"<link rel=\"stylesheet\" href=\"/style.css\">\r\n"
		"</head>\r\n"
		"<body>\r\n"
		"<header class=\"hero\">\r\n"
		"<h1>Index of " + escapedBase + "</h1>\r\n"
		"</header>\r\n"
		"<section>\r\n"
		"<ul>\r\n";

	if (baseUri != "/") {
		page += "<li><a href=\"" + StringUtils::escapeHtml(baseUri + "../") + "\">../</a></li>\r\n";
	}
	for (StringVec::const_iterator it = entryNames.begin(); it != entryNames.end(); ++it) {
		const std::string& displayName = *it;
		bool isDirectory = (displayName[displayName.size() - 1] == '/');
		std::string bareName = isDirectory
			? displayName.substr(0, displayName.size() - 1)
			: displayName;
		std::string href = baseUri + PathResolver::encodeSegment(bareName) + (isDirectory ? "/" : "");
		page += "<li><a href=\"" + StringUtils::escapeHtml(href) + "\">" + StringUtils::escapeHtml(displayName) + "</a></li>\r\n";
	}

	page +=
		"</ul>\r\n"
		"</section>\r\n"
		"<p class=\"back\"><a href=\"/\">&larr; Back to the index</a></p>\r\n"
		"</body>\r\n"
		"</html>\r\n";

	Response r(HTTP_OK);
	r.setHeader("Content-Type", "text/html; charset=utf-8");
	r.setBody(page);
	return r;
}

Response ResponseFactory::makeFromCgi(const std::string& rawCgiOutput, const ServerConfig& cfg,
                                      const LocationConfig* loc) {
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
			return makeError(HTTP_BAD_GATEWAY, cfg, loc);
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
			return makeError(HTTP_BAD_GATEWAY, cfg, loc);
		}
		std::string name  = StringUtils::trim(line.substr(0, colon));
		std::string value = StringUtils::trim(line.substr(colon + 1));

		if (StringUtils::iequals(name, "Status")) {
			// Formato: "404 Not Found" ou apenas "404".
			bool ok = false;
			long code = StringUtils::toLong(value.substr(0, value.find(' ')), ok);
			if (!ok || code < 100 || code > 599) {
				LOG_WARN("makeFromCgi: Status CGI invalido: \"" + value + "\"");
				return makeError(HTTP_BAD_GATEWAY, cfg, loc);
			}
			resp.setStatus(static_cast<int>(code));
		} else {
			resp.setHeader(name, value);
		}
	}

	resp.setBody(rawCgiOutput.substr(bodyStart));
	return resp;
}
