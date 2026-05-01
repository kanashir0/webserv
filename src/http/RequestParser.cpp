#include "http/RequestParser.hpp"

namespace ws {

RequestParser::RequestParser()
	: state_(METHOD)
	, building_()
	, buf_()
	, bytesRead_(0)
	, contentLength_(0)
	, chunked_(false)
	, errorStatus_(0)
{}

RequestParser::~RequestParser() {}

void RequestParser::reset() {
	state_         = METHOD;
	building_      = Request();
	buf_.clear();
	bytesRead_     = 0;
	contentLength_ = 0;
	chunked_       = false;
	errorStatus_   = 0;
}

RequestParser::FeedResult RequestParser::feed(const char* data,
                                              std::size_t n,
                                              std::size_t /*maxBody*/) {
	// TODO Membro 2: alimentar a state machine
	buf_.append(data, n);
	return NEED_MORE;
}

bool                 RequestParser::complete() const { return state_ == DONE; }
const Request&       RequestParser::current() const  { return building_; }
Request              RequestParser::take()           { return building_; }
int                  RequestParser::errorStatus() const { return errorStatus_; }
RequestParser::State RequestParser::state() const    { return state_; }

RequestParser::FeedResult RequestParser::parseRequestLine() { return NEED_MORE; }
RequestParser::FeedResult RequestParser::parseHeaders()     { return NEED_MORE; }
RequestParser::FeedResult RequestParser::parseBodyByLength(std::size_t) { return NEED_MORE; }
RequestParser::FeedResult RequestParser::parseBodyChunked(std::size_t)  { return NEED_MORE; }
void                      RequestParser::splitUri()         {}

}
