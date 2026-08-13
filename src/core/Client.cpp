#include "core/Client.hpp"
#include "core/EventLoop.hpp"
#include "cgi/CgiHandler.hpp"
#include "http/Router.hpp"
#include "http/ResponseFactory.hpp"
#include "common/HttpStatus.hpp"
#include "session/SessionStore.hpp"
#include <ctime>

Client::Client(int fd,
               std::vector<ServerConfig>& vhosts,
               Router& router,
               SessionStore& sessions,
               EventLoop& loop)
	: fd_(fd)
	, state_(READING_HEADERS)
	, parser_()
	, request_()
	, response_()
	, outBuffer_()
	, outOffset_(0)
	, lastActivity_(std::time(0))
	, wantsClose_(false)
	, responseSerialized_(false)
	, closeAfterWrite_(false)
	, vhosts_(vhosts)
	, router_(router)
	, sessions_(sessions)
	, loop_(loop)
	, cgi_(0)
{}

Client::~Client() {
	// O script sobreviveria ao cliente e escreveria numa Request ja destruida.
	if (cgi_ != 0) {
		cgi_->detachClient();
	}
}

int   Client::fd() const         {
	return fd_.get();
}

short Client::interest() const   {
	switch (state_) {
		case READING_HEADERS:
			return POLLIN;

		case WRITING_RESPONSE:
			return POLLOUT;

		default:
			return DONE;
	}
}

void  Client::onReadable()       {
	lastActivity_ = std::time(0);

	char buffer[4096];

	ssize_t ret = recv(fd_.get(), buffer, sizeof(buffer), 0);

	if (ret <= 0) {
		wantsClose_ = true;
		state_ = DONE;
		return;
	}

	RequestParser::FeedResult result = parser_.feed(buffer, ret, matchVirtualHost().clientMaxBodySize);

	if (result == RequestParser::NEED_MORE)
		return;
	if (result == RequestParser::COMPLETE) {
		request_ = parser_.take();
		dispatch();
	} else { // BAD_REQUEST, URI_TOO_LONG, BODY_TOO_LARGE, VERSION_UNSUPPORTED
		buildErrorResponse(parser_.errorStatus());
		closeAfterWrite_ = true; // parser em estado de erro: sem keep-alive
		state_ = WRITING_RESPONSE;
	}
}

void  Client::onWritable()       {
	lastActivity_ = std::time(0);

	if (!responseSerialized_) {
		// Quem manda na semantica da conexao e o servidor, nao o handler nem o
		// script CGI: o header e escrito aqui, ja sabendo se vamos fechar.
		response_.setHeader("Connection", willClose() ? "close" : "keep-alive");
		outBuffer_ = response_.toString();
		outOffset_ = 0;
		responseSerialized_ = true;
	}

	size_t remaining = outBuffer_.size() - outOffset_;

	ssize_t bytes_sent = send(fd_.get(), outBuffer_.c_str() + outOffset_, remaining, 0);

	if (bytes_sent < 0) {
		wantsClose_ = true;
		state_ = DONE;
		return;
	}

	outOffset_ +=  bytes_sent;

	if (outOffset_ < outBuffer_.size())
		return;

	if (closeAfterWrite_) {
		wantsClose_ = true;
		state_ = DONE;
		return;
	}

	responseSerialized_ = false;

	if (request_.keepAlive()) {
		outBuffer_.clear();
		outOffset_ = 0;
		if (!tryConsumeResidual())
			return;
		state_ = READING_HEADERS;
	} else {
		wantsClose_ = true;
		state_ = DONE;
	}
}

void  Client::onHangup()         { wantsClose_ = true; }
bool  Client::wantsClose() const { return wantsClose_; }

Client::State Client::state() const          { return state_; }
std::time_t   Client::lastActivity() const   { return lastActivity_; }

const ServerConfig& Client::matchVirtualHost() const {
	std::string hostPort = request_.header("Host");

	std::string host = hostPort.substr(0, hostPort.find(":"));

	for (size_t i = 0; i < vhosts_.size(); i++) {
		StringVec serverName = vhosts_[i].getServerNames();
		for (size_t j = 0; j < serverName.size(); j++) {
			if (StringUtils::iequals(host, serverName[j]))
				return vhosts_[i];
		}
	}
	return vhosts_.front();
}

void Client::buildErrorResponse(int code) {
	response_ = ResponseFactory::makeError(code, matchVirtualHost());
}

// Roteia a requisicao ja completa. Quando o alvo e um script CGI a resposta
// nao sai daqui: o CgiHandler assume o poll() e devolve via onCgiComplete().
void Client::dispatch() {
	const ServerConfig& vhost = matchVirtualHost();

	CgiTarget cgi;
	response_ = router_.route(request_, vhost, cgi);
	if (!cgi.active()) {
		state_ = WRITING_RESPONSE;
		return;
	}

	CgiHandler* handler = new CgiHandler(*this, request_, *cgi.loc, vhost,
	                                     cgi.interpreter, cgi.scriptPath);
	if (!handler->start(loop_)) {
		delete handler;
		buildErrorResponse(HTTP_INTERNAL_SERVER_ERROR);
		state_ = WRITING_RESPONSE;
		return;
	}
	cgi_   = handler;
	state_ = WAITING_CGI;
}

// closeAfterWrite_ cobre os casos em que a conexao nao pode continuar (erro de
// parser, timeout); fora deles vale a negociacao do proprio request.
bool Client::willClose() const {
	return closeAfterWrite_ || !request_.keepAlive();
}

void Client::onCgiComplete(const Response& resp) {
	cgi_                = 0;
	response_           = resp;
	responseSerialized_ = false;
	lastActivity_       = std::time(0);
	state_              = WRITING_RESPONSE;
}

void Client::checkTimeout(std::time_t now, std::time_t timeout) {
	// Esperando o CGI nao e conexao ociosa: o CgiHandler tem timeout proprio.
	if (state_ == WAITING_CGI)
		return;
	if (now - lastActivity_ <= timeout)
		return;

	buildErrorResponse(408);
	closeAfterWrite_ = true;
	state_ = WRITING_RESPONSE;
}

bool Client::tryConsumeResidual() {
	RequestParser::FeedResult result = parser_.feed(NULL, 0, matchVirtualHost().clientMaxBodySize);
	if (result == RequestParser::NEED_MORE)
		return true;
	else if (result == RequestParser::COMPLETE) {
		request_ = parser_.take();
		dispatch();
		return false;
	}

	buildErrorResponse(parser_.errorStatus());
	closeAfterWrite_ = true;
	state_ = WRITING_RESPONSE;
	return false;
}


