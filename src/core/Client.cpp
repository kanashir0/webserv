#include "core/Client.hpp"
#include "http/Router.hpp"
#include "http/ResponseFactory.hpp"
#include "session/SessionStore.hpp"
#include <ctime>

namespace ws {

Client::Client(int fd,
               const std::vector<ServerConfig>& vhosts,
               Router& router,
               SessionStore& sessions)
	: fd_(fd)
	, state_(READING_HEADERS)
	, parser_()
	, request_()
	, response_()
	, outBuffer_()
	, outOffset_(0)
	, lastActivity_(std::time(0))
	, wantsClose_(false)
	, vhosts_(vhosts)
	, router_(router)
	, sessions_(sessions)
{}

Client::~Client() {}

int   Client::fd() const         { return fd_.get(); }
short Client::interest() const   { return 0; }
void  Client::onReadable()       { lastActivity_ = std::time(0); }
void  Client::onWritable()       { lastActivity_ = std::time(0); }
void  Client::onHangup()         { wantsClose_ = true; }
bool  Client::wantsClose() const { return wantsClose_; }

Client::State Client::state() const          { return state_; }
std::time_t   Client::lastActivity() const   { return lastActivity_; }

const ServerConfig& Client::matchVirtualHost() const {
	// TODO Membro 1: usar Host header + porta para escolher vhost
	return vhosts_.front();
}

void Client::buildErrorResponse(int code) {
	response_ = ResponseFactory::makeError(code, matchVirtualHost());
}

}
