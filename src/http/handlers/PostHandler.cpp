#include "http/handlers/PostHandler.hpp"
#include "http/PathResolver.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include "common/StringUtils.hpp"
#include <ctime>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>


static bool findCgiInterpreter(const std::string& decodedPath,
                               const LocationConfig& loc,
                               std::string& interpreter) {
	for (std::map<std::string, std::string>::const_iterator it = loc.cgi.begin();
	     it != loc.cgi.end(); ++it) {
		if (!it->first.empty() && StringUtils::endsWith(decodedPath, it->first)) {
			interpreter = it->second;
			return true;
		}
	}
	return false;
}

static std::string basenameOf(const std::string& s) {
	std::string::size_type cut = s.find_last_of("/\\");
	return cut == std::string::npos ? s : s.substr(cut + 1);
}

static std::string sanitizeFilename(const std::string& rawName) {
	std::string name = basenameOf(rawName);
	if (name.empty() || name == "." || name == "..") {
		return std::string();
	}
	for (std::string::size_type i = 0; i < name.size(); ++i) {
		unsigned char c = static_cast<unsigned char>(name[i]);
		if (c < 32 || c == 127) {
			return std::string();
		}
	}
	return name;
}

static std::string generatedName() {
	static unsigned long counter = 0;
	++counter;
	return "upload-" + StringUtils::toString(static_cast<long>(std::time(0))) +
	       "-" + StringUtils::toString(static_cast<long>(counter));
}

static std::string filenameFromUri(const std::string& decodedPath, const std::string& locPath) {
	if (decodedPath.empty() || decodedPath[decodedPath.size() - 1] == '/') {
		return std::string();
	}
	std::string prefix = locPath;
	if (prefix.size() > 1 && prefix[prefix.size() - 1] == '/') {
		prefix.erase(prefix.size() - 1);
	}
	if (decodedPath == prefix) {
		return std::string();
	}
	return basenameOf(decodedPath);
}

static bool isMultipart(const std::string& contentType) {
	return StringUtils::startsWith(StringUtils::toLower(contentType), "multipart/form-data");
}

static bool extractBoundary(const std::string& contentType, std::string& boundary) {
	std::string::size_type pos = StringUtils::toLower(contentType).find("boundary=");
	if (pos == std::string::npos) {
		return false;
	}
	std::string value = contentType.substr(pos + 9);
	std::string::size_type semicolon = value.find(';');
	if (semicolon != std::string::npos) {
		value = value.substr(0, semicolon);
	}
	value = StringUtils::trim(value);
	if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"') {
		value = value.substr(1, value.size() - 2);
	}
	boundary = value;
	return !value.empty();
}

static bool firstMultipartPart(const std::string& body, const std::string& boundary,
                               std::string& filename, std::string& content) {
	const std::string      delimiter = "--" + boundary;
	std::string::size_type start     = body.find(delimiter);
	if (start == std::string::npos) {
		return false;
	}
	std::string::size_type afterDelim = start + delimiter.size();
	if (body.compare(afterDelim, 2, "--") == 0) {
		return false;
	}
	if (body.compare(afterDelim, 2, "\r\n") != 0) {
		return false;
	}
	std::string::size_type headerStart = afterDelim + 2;
	std::string::size_type headerEnd   = body.find("\r\n\r\n", headerStart);
	if (headerEnd == std::string::npos) {
		return false;
	}
	std::string partHeaders = body.substr(headerStart, headerEnd - headerStart);
	std::string::size_type f = StringUtils::toLower(partHeaders).find("filename=\"");
	if (f != std::string::npos) {
		std::string::size_type valueStart = f + 10;
		std::string::size_type valueEnd   = partHeaders.find('"', valueStart);
		if (valueEnd != std::string::npos) {
			filename = partHeaders.substr(valueStart, valueEnd - valueStart);
		}
	}
	std::string::size_type contentStart = headerEnd + 4;
	std::string::size_type contentEnd   = body.find("\r\n" + delimiter, contentStart);
	if (contentEnd == std::string::npos) {
		return false;
	}
	content = body.substr(contentStart, contentEnd - contentStart);
	return true;
}

static bool writeFile(const std::string& dest, const std::string& content) {
	std::ofstream out(dest.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out.write(content.data(), static_cast<std::streamsize>(content.size()));
	bool ok = out.good();
	out.close();
	return ok;
}


PostHandler::PostHandler() {}
PostHandler::~PostHandler() {}

Response PostHandler::handle(const Request& req,
                             const LocationConfig& loc,
                             const ServerConfig& srv) {
	std::string decodedPath;
	if (!PathResolver::percentDecode(req.path(), decodedPath)) {
		return ResponseFactory::makeError(HTTP_BAD_REQUEST, srv);
	}
	std::string interpreter;
	if (findCgiInterpreter(decodedPath, loc, interpreter)) {
		return handleCgi(req, loc, srv, interpreter);
	}
	return handleUpload(req, loc, srv);
}

Response PostHandler::handleUpload(const Request& req,
                                   const LocationConfig& loc,
                                   const ServerConfig& srv) {
	if (loc.uploadStore.empty()) {
		LOG_ERROR("PostHandler: location \"" + loc.path + "\" sem upload_store");
		return ResponseFactory::makeError(HTTP_INTERNAL_SERVER_ERROR, srv);
	}
	struct stat info;
	if (stat(loc.uploadStore.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) ||
	    access(loc.uploadStore.c_str(), W_OK | X_OK) != 0) {
		LOG_ERROR("PostHandler: upload_store inacessivel: \"" + loc.uploadStore + "\"");
		return ResponseFactory::makeError(HTTP_INTERNAL_SERVER_ERROR, srv);
	}

	std::string decodedPath;
	if (!PathResolver::percentDecode(req.path(), decodedPath)) {
		return ResponseFactory::makeError(HTTP_BAD_REQUEST, srv);
	}

	std::string filename;
	std::string content;
	const std::string contentType = req.header("Content-Type");
	if (isMultipart(contentType)) {
		std::string boundary;
		if (!extractBoundary(contentType, boundary) ||
		    !firstMultipartPart(req.body(), boundary, filename, content)) {
			LOG_WARN("PostHandler: multipart/form-data malformado");
			return ResponseFactory::makeError(HTTP_BAD_REQUEST, srv);
		}
	} else {
		content = req.body();
	}

	if (filename.empty()) {
		filename = filenameFromUri(decodedPath, loc.path);
	}
	filename = sanitizeFilename(filename);
	if (filename.empty()) {
		filename = generatedName();
	}

	const std::string dest = PathResolver::joinPath(loc.uploadStore, filename);
	if (!writeFile(dest, content)) {
		LOG_ERROR("PostHandler: falha ao gravar \"" + dest + "\"");
		return ResponseFactory::makeError(HTTP_INTERNAL_SERVER_ERROR, srv);
	}

	std::string publicBase = loc.path;
	if (publicBase.empty() || publicBase[publicBase.size() - 1] != '/') {
		publicBase += "/";
	}
	Response r(HTTP_CREATED);
	r.setHeader("Content-Location", publicBase + PathResolver::encodeSegment(filename));
	r.setBody("");
	return r;
}

Response PostHandler::handleCgi(const Request& /*req*/,
                                const LocationConfig& /*loc*/,
                                const ServerConfig& srv,
                                const std::string& /*interpreter*/) {
	// TODO Membro 3 + Membro 1 (E05-T03/E06): integracao assincrona com CgiHandler
	return ResponseFactory::makeError(HTTP_NOT_IMPLEMENTED, srv);
}
