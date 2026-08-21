#include "cgi/CgiHandler.hpp"
#include "cgi/CgiEnv.hpp"
#include "cgi/CgiStdinPump.hpp"
#include "core/Client.hpp"
#include "core/EventLoop.hpp"
#include "http/PathResolver.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>


static const int         CGI_TIMEOUT_SEC = 10;
static const std::size_t CGI_CHUNK_SIZE  = 4096;

static std::string directoryOf(const std::string& path) {
	std::string::size_type slash = path.find_last_of('/');
	return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

static std::string fileNameOf(const std::string& path) {
	std::string::size_type slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


CgiHandler::CgiHandler(Client& client,
                       const Request& req,
                       const LocationConfig& loc,
                       const ServerConfig& srv,
                       const std::string& interpreter,
                       const std::string& scriptPath)
	: client_(&client)
	, req_(req)
	, loc_(loc)
	, srv_(srv)
	// runChild() faz chdir() para o diretorio do script: caminhos relativos
	// vindos do config precisam ser fixados antes disso.
	, interpreter_(PathResolver::toAbsolute(interpreter))
	, scriptPath_(PathResolver::toAbsolute(scriptPath))
	, pid_(-1)
	, stdinPump_(0)
	, stdoutPipe_(-1)
	, output_()
	, phase_(FINISHED)
	, startedAt_(0)
{}

CgiHandler::~CgiHandler() {
	releaseStdinPump();  // o pump sobreviveria apontando para um objeto morto
	reapChild();
}

void CgiHandler::runChild(int inPipe[2], int outPipe[2]) {
	if (::dup2(inPipe[0], STDIN_FILENO) < 0 || ::dup2(outPipe[1], STDOUT_FILENO) < 0) {
		::_exit(1);
	}
	::close(inPipe[0]);
	::close(inPipe[1]);
	::close(outPipe[0]);
	::close(outPipe[1]);

	// Scripts costumam abrir arquivos com caminho relativo a si mesmos.
	if (::chdir(directoryOf(scriptPath_).c_str()) != 0) {
		::_exit(1);
	}

	CgiEnv      env(req_, loc_, srv_, scriptPath_);
	std::string script = fileNameOf(scriptPath_);

	char* argv[3];
	argv[0] = const_cast<char*>(interpreter_.c_str());
	argv[1] = const_cast<char*>(script.c_str());
	argv[2] = 0;

	::execve(interpreter_.c_str(), argv, env.asEnvp());
	::_exit(1);
}

bool CgiHandler::start(EventLoop& loop) {
	int in[2];
	int out[2];

	if (::pipe(in) != 0) {
		LOG_ERROR("CgiHandler: pipe() falhou para \"" + scriptPath_ + "\"");
		return false;
	}
	if (::pipe(out) != 0) {
		LOG_ERROR("CgiHandler: pipe() falhou para \"" + scriptPath_ + "\"");
		FileDescriptor discardIn(in[0]), discardOut(in[1]);
		return false;
	}

	pid_ = ::fork();
	if (pid_ < 0) {
		LOG_ERROR("CgiHandler: fork() falhou para \"" + scriptPath_ + "\"");
		FileDescriptor a(in[0]), b(in[1]), c(out[0]), d(out[1]);
		return false;
	}
	if (pid_ == 0) {
		runChild(in, out);
		::_exit(1);
	}

	FileDescriptor childStdin(in[0]);
	FileDescriptor childStdout(out[1]);
	FileDescriptor parentStdin(in[1]);
	stdoutPipe_.reset(out[0]);

	if (::fcntl(parentStdin.get(), F_SETFL, O_NONBLOCK) < 0 ||
	    ::fcntl(stdoutPipe_.get(), F_SETFL, O_NONBLOCK) < 0) {
		LOG_ERROR("CgiHandler: fcntl(O_NONBLOCK) falhou para \"" + scriptPath_ + "\"");
		return false;
	}

	startedAt_ = std::time(0);
	phase_     = READING_OUTPUT;
	loop.add(this);

	// Sem body o script ve EOF de imediato e nao ha nada para bombear.
	if (!req_.body().empty()) {
		stdinPump_ = new CgiStdinPump(*this, parentStdin.release(), req_.body());
		loop.add(stdinPump_);
	}
	return true;
}

int CgiHandler::fd() const { return stdoutPipe_.get(); }

short CgiHandler::interest() const {
	return phase_ == READING_OUTPUT ? POLLIN : 0;
}

void CgiHandler::onWritable() {}

void CgiHandler::onStdinClosed() { stdinPump_ = 0; }

void CgiHandler::releaseStdinPump() {
	if (stdinPump_ == 0) {
		return;
	}
	CgiStdinPump* pump = stdinPump_;
	stdinPump_ = 0;
	pump->detachOwner();  // fecha o stdin do script e desarma o ponteiro de volta
}

ssize_t CgiHandler::readChunk() {
	char    buffer[CGI_CHUNK_SIZE];
	ssize_t received = ::read(stdoutPipe_.get(), buffer, sizeof(buffer));
	if (received > 0) {
		output_.append(buffer, static_cast<std::size_t>(received));
	}
	return received;
}

void CgiHandler::onReadable() {
	if (phase_ != READING_OUTPUT) {
		return;
	}
	if (readChunk() <= 0) {
		deliver(buildResponse());
	}
}

void CgiHandler::onHangup() {
	if (phase_ == READING_OUTPUT && readChunk() > 0) {
		return;
	}
	deliver(buildResponse());
}

Response CgiHandler::buildResponse() {
	Response resp = ResponseFactory::makeFromCgi(output_, srv_, &loc_);
	if (resp.status() != HTTP_BAD_GATEWAY) {
		return resp;
	}
	// O script roda mesmo sem existir no disco (ver Router::prepareCgi). Quando
	// ele nao devolve nada aproveitavel e o alvo tambem nao existe, o 404 conta
	// a verdade melhor que um 502 generico -- e o que acontece, por exemplo, com
	// um ".py" inexistente entregue ao interpretador python.
	if (::access(scriptPath_.c_str(), F_OK) != 0) {
		return ResponseFactory::makeError(HTTP_NOT_FOUND, srv_, &loc_);
	}
	return resp;
}

void CgiHandler::checkTimeout(std::time_t now, std::time_t /*timeout*/) {
	if (phase_ == FINISHED || now - startedAt_ <= CGI_TIMEOUT_SEC) {
		return;
	}
	LOG_ERROR("CgiHandler: timeout em \"" + scriptPath_ + "\"");
	deliver(ResponseFactory::makeError(HTTP_GATEWAY_TIMEOUT, srv_, &loc_));
}

bool CgiHandler::wantsClose() const {
	return phase_ == FINISHED;
}

void CgiHandler::detachClient() {
	client_ = 0;
	shutdownChild();
}

void CgiHandler::shutdownChild() {
	releaseStdinPump();
	stdoutPipe_.reset();
	phase_ = FINISHED;
	reapChild();
}

void CgiHandler::deliver(const Response& resp) {
	shutdownChild();
	if (client_ != 0) {
		client_->onCgiComplete(resp);
		client_ = 0;
	}
}

void CgiHandler::reapChild() {
	if (pid_ <= 0) {
		return;
	}
	int status;
	if (::waitpid(pid_, &status, WNOHANG) == 0) {
		::kill(pid_, SIGKILL);
		::waitpid(pid_, &status, 0);
	}
	pid_ = -1;
}
