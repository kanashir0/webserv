#ifndef WEBSERV_CGI_CGI_HANDLER_HPP
#define WEBSERV_CGI_CGI_HANDLER_HPP

#include "core/IPollable.hpp"
#include "common/FileDescriptor.hpp"
#include "http/Request.hpp"
#include "http/Response.hpp"
#include "config/LocationConfig.hpp"
#include "config/ServerConfig.hpp"
#include <string>
#include <ctime>
#include <sys/types.h>

namespace ws {

class EventLoop;

class CgiHandler : public IPollable {
public:
	CgiHandler(const Request& req,
	           const LocationConfig& loc,
	           const ServerConfig& srv,
	           const std::string& interpreter,
	           const std::string& scriptPath);
	~CgiHandler();

	void start(EventLoop& loop);

	bool     finished() const;
	Response takeResponse();

	int   fd() const;
	short interest() const;
	void  onReadable();
	void  onWritable();
	void  onHangup();
	bool  wantsClose() const;

	bool  timedOut(int timeoutSec) const;
	void  kill();

private:
	const Request&        req_;
	const LocationConfig& loc_;
	const ServerConfig&   srv_;
	std::string           interpreter_;
	std::string           scriptPath_;

	pid_t          pid_;
	FileDescriptor stdoutPipe_;
	FileDescriptor stdinPipe_;
	std::string    output_;
	std::size_t    stdinOffset_;
	bool           done_;
	bool           wantsClose_;
	std::time_t    startedAt_;

	CgiHandler(const CgiHandler&);
	CgiHandler& operator=(const CgiHandler&);
};

}

#endif
