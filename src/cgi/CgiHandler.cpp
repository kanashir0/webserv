#include "cgi/CgiHandler.hpp"
#include "core/EventLoop.hpp"
#include <ctime>
#include <signal.h>


CgiHandler::CgiHandler(const Request& req,
                       const LocationConfig& loc,
                       const ServerConfig& srv,
                       const std::string& interpreter,
                       const std::string& scriptPath)
	: req_(req)
	, loc_(loc)
	, srv_(srv)
	, interpreter_(interpreter)
	, scriptPath_(scriptPath)
	, pid_(-1)
	, stdoutPipe_(-1)
	, stdinPipe_(-1)
	, output_()
	, stdinOffset_(0)
	, done_(false)
	, wantsClose_(false)
	, startedAt_(0)
{}

CgiHandler::~CgiHandler() {}

void CgiHandler::start(EventLoop& /*loop*/) {
	// TODO Membro 1: pipe(), fork(), dup2(), execve(); registrar pipes no loop
	startedAt_ = std::time(0);
}

bool     CgiHandler::finished() const  { return done_; }
Response CgiHandler::takeResponse()    { return Response(200); }

int   CgiHandler::fd() const          { return stdoutPipe_.get(); }
short CgiHandler::interest() const    { return 0; }
void  CgiHandler::onReadable()        {}
void  CgiHandler::onWritable()        {}
void  CgiHandler::onHangup()          { wantsClose_ = true; }
bool  CgiHandler::wantsClose() const  { return wantsClose_; }

bool CgiHandler::timedOut(int timeoutSec) const {
	return startedAt_ != 0 && (std::time(0) - startedAt_) > timeoutSec;
}

void CgiHandler::kill() {
	if (pid_ > 0) ::kill(pid_, SIGKILL);
}

