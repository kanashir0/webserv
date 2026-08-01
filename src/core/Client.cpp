#include "core/Client.hpp"
#include "http/Router.hpp"
#include "http/ResponseFactory.hpp"
#include "session/SessionStore.hpp"
#include <ctime>

Client::Client(int fd,
               std::vector<ServerConfig>& vhosts,
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
	, responseSerialized_(false)
	, closeAfterWrite_(false)
	, vhosts_(vhosts)
	, router_(router)
	, sessions_(sessions)
{}

Client::~Client() {}

int   Client::fd() const         {
	return fd_.get();
}

short Client::interest() const   {
	switch (state_) {
		case READING_HEADERS:
		case READING_BODY:
			return POLLIN;

		case ROUTING:
			return 0;

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

	if (ret == 0) {
		wantsClose_ = true;
		state_ = DONE;
		return;
	}
	if (ret < 0)
		return;

	RequestParser::FeedResult result = parser_.feed(buffer, ret, matchVirtualHost().clientMaxBodySize);

	if (result == RequestParser::NEED_MORE)
		return;
	if (result == RequestParser::COMPLETE) {
		request_ = parser_.take();
		response_ = router_.route(request_, matchVirtualHost());
		state_ = WRITING_RESPONSE;
	}
	if (result == RequestParser::BAD_REQUEST) {
		buildErrorResponse(parser_.errorStatus());
		state_ = WRITING_RESPONSE;
	}
}

void  Client::onWritable()       {
	lastActivity_ = std::time(0);

	if (!responseSerialized_) {
		outBuffer_ = response_.toString();
		outOffset_ = 0;
		responseSerialized_ = true;
	}

	size_t remaining = outBuffer_.size() - outOffset_;

	ssize_t bytes_sent = send(fd_.get(), outBuffer_.c_str() + outOffset_, remaining, 0);

	if (bytes_sent < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
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

	if (request_.keepAlive()) {
		parser_.reset();
		outBuffer_.clear();
		outOffset_ = 0;
		state_ = READING_HEADERS;
	} else {
		wantsClose_ = true;
		state_ = DONE;
	}
	responseSerialized_ = false;
}

void  Client::onHangup()         { wantsClose_ = true; }
bool  Client::wantsClose() const { return wantsClose_; }

Client::State Client::state() const          { return state_; }
std::time_t   Client::lastActivity() const   { return lastActivity_; }

const ServerConfig& Client::matchVirtualHost() const {
	std::string host = request_.header("Host");

	for (size_t i = 0; i < vhosts_.size(); i++) {
		StringVec serverName = vhosts_[i].getServerNames();
		for (size_t j = 0; j < serverName.size(); j++) {
			if (host == serverName[j])
				return vhosts_[i];
		}
	}
	return vhosts_.front();
}

void Client::buildErrorResponse(int code) {
	response_ = ResponseFactory::makeError(code, matchVirtualHost());
}

void Client::checkTimeout(std::time_t now, std::time_t timeout) {
	if (now - lastActivity_ <= timeout)
		return;

	buildErrorResponse(408);
	closeAfterWrite_ = true;
	state_ = WRITING_RESPONSE;
}


