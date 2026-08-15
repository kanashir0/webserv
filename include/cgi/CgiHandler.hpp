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


class EventLoop;
class Client;

class CgiHandler : public IPollable {
public:
	CgiHandler(Client& client,
	           const Request& req,
	           const LocationConfig& loc,
	           const ServerConfig& srv,
	           const std::string& interpreter,
	           const std::string& scriptPath);
	~CgiHandler();

	bool start(EventLoop& loop);

	void detachClient();

	int   fd() const;
	short interest() const;
	void  onReadable();
	void  onWritable();
	void  onHangup();
	bool  wantsClose() const;
	void  checkTimeout(std::time_t now, std::time_t timeout);

private:
	enum Phase {
		WRITING_INPUT,   // enviando o body da requisicao para o stdin do script
		READING_OUTPUT,  // lendo o stdout do script
		FINISHED         // resposta entregue; pronto para o reapClosed()
	};

	Client*               client_;
	const Request&        req_;
	const LocationConfig& loc_;
	const ServerConfig&   srv_;
	std::string           interpreter_;
	std::string           scriptPath_;

	pid_t          pid_;
	FileDescriptor stdinPipe_;
	FileDescriptor stdoutPipe_;
	std::string    output_;
	std::size_t    stdinOffset_;
	Phase          phase_;
	std::time_t    startedAt_;

	void    runChild(int inPipe[2], int outPipe[2]);
	ssize_t readChunk();
	void    stopWritingInput();
	void    shutdownChild();
	void    deliver(const Response& resp);
	void    reapChild();

	CgiHandler(const CgiHandler&);
	CgiHandler& operator=(const CgiHandler&);
};


#endif
