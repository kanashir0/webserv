#include "cgi/CgiHandler.hpp"
#include "cgi/CgiEnv.hpp"
#include "core/Client.hpp"
#include "core/EventLoop.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "common/Logger.hpp"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>


// Timeout proprio do CGI: o do EventLoop mede conexao ociosa e e longo demais
// para segurar um cliente esperando por um script travado.
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
	, interpreter_(interpreter)
	, scriptPath_(scriptPath)
	, pid_(-1)
	, stdinPipe_(-1)
	, stdoutPipe_(-1)
	, output_()
	, stdinOffset_(0)
	, phase_(FINISHED)
	, startedAt_(0)
{}

CgiHandler::~CgiHandler() {
	reapChild();
}

void CgiHandler::runChild(int inPipe[2], int outPipe[2]) {
	if (::dup2(inPipe[0], STDIN_FILENO) < 0 || ::dup2(outPipe[1], STDOUT_FILENO) < 0) {
		::_exit(1);
	}
	// O filho precisa largar as quatro pontas originais: enquanto ele mantiver
	// a ponta de escrita do proprio stdin aberta, nunca veria o EOF.
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
		// runChild so retorna se algo muito errado acontecer; sem este _exit o
		// filho cairia no codigo do pai e voltaria para o event loop.
		::_exit(1);
	}

	// Pai: fica com a escrita do stdin e a leitura do stdout; as outras duas
	// pontas sao do filho e fecham ao sair deste escopo (RAII).
	FileDescriptor childStdin(in[0]);
	FileDescriptor childStdout(out[1]);
	stdinPipe_.reset(in[1]);
	stdoutPipe_.reset(out[0]);

	if (::fcntl(stdinPipe_.get(), F_SETFL, O_NONBLOCK) < 0 ||
	    ::fcntl(stdoutPipe_.get(), F_SETFL, O_NONBLOCK) < 0) {
		LOG_ERROR("CgiHandler: fcntl(O_NONBLOCK) falhou para \"" + scriptPath_ + "\"");
		return false;
	}

	startedAt_ = std::time(0);
	if (req_.body().empty()) {
		stdinPipe_.reset();  // sem body: o script ve EOF de imediato
		phase_ = READING_OUTPUT;
	} else {
		phase_ = WRITING_INPUT;
	}

	loop.add(this);
	return true;
}

int CgiHandler::fd() const {
	return phase_ == WRITING_INPUT ? stdinPipe_.get() : stdoutPipe_.get();
}

short CgiHandler::interest() const {
	if (phase_ == WRITING_INPUT)  return POLLOUT;
	if (phase_ == READING_OUTPUT) return POLLIN;
	return 0;
}

void CgiHandler::onWritable() {
	if (phase_ != WRITING_INPUT) {
		return;
	}
	const std::string& body      = req_.body();
	std::size_t        remaining = body.size() - stdinOffset_;
	if (remaining > CGI_CHUNK_SIZE) {
		remaining = CGI_CHUNK_SIZE;
	}

	ssize_t sent = ::write(stdinPipe_.get(), body.data() + stdinOffset_, remaining);
	if (sent <= 0) {
		stopWritingInput();  // script fechou o stdin antes de ler tudo
		return;
	}
	stdinOffset_ += static_cast<std::size_t>(sent);
	if (stdinOffset_ >= body.size()) {
		stopWritingInput();
	}
}

void CgiHandler::stopWritingInput() {
	stdinPipe_.reset();  // fechar a ponta de escrita e o EOF do script
	phase_ = READING_OUTPUT;
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
		deliver(ResponseFactory::makeFromCgi(output_, srv_));
	}
}

void CgiHandler::onHangup() {
	if (phase_ == WRITING_INPUT) {
		stopWritingInput();
		return;
	}
	// O POLLHUP do stdout chega junto com o que ainda estiver no buffer do
	// kernel: uma leitura por evento, encerrando so quando ela devolver 0.
	if (phase_ == READING_OUTPUT && readChunk() > 0) {
		return;
	}
	deliver(ResponseFactory::makeFromCgi(output_, srv_));
}

void CgiHandler::checkTimeout(std::time_t now, std::time_t /*timeout*/) {
	if (phase_ == FINISHED || now - startedAt_ <= CGI_TIMEOUT_SEC) {
		return;
	}
	LOG_ERROR("CgiHandler: timeout em \"" + scriptPath_ + "\"");
	deliver(ResponseFactory::makeError(HTTP_GATEWAY_TIMEOUT, srv_));
}

bool CgiHandler::wantsClose() const {
	return phase_ == FINISHED;
}

void CgiHandler::detachClient() {
	client_ = 0;
	shutdownChild();
}

void CgiHandler::shutdownChild() {
	stdinPipe_.reset();
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
		// Script ainda vivo. SIGKILL nao pode ser ignorado, entao a espera
		// seguinte retorna de imediato.
		::kill(pid_, SIGKILL);
		::waitpid(pid_, &status, 0);
	}
	pid_ = -1;
}
