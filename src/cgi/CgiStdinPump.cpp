#include "cgi/CgiStdinPump.hpp"
#include "cgi/CgiHandler.hpp"
#include <poll.h>
#include <unistd.h>


static const std::size_t PUMP_CHUNK_SIZE = 65536;

CgiStdinPump::CgiStdinPump(CgiHandler& owner, int writeFd, const std::string& body)
	: owner_(&owner)
	, pipe_(writeFd)
	, body_(&body)
	, offset_(0)
{}

CgiStdinPump::~CgiStdinPump() {}

int CgiStdinPump::fd() const { return pipe_.get(); }

short CgiStdinPump::interest() const {
	return pipe_.valid() ? POLLOUT : 0;
}

void CgiStdinPump::onReadable() {}

void CgiStdinPump::onWritable() {
	if (!pipe_.valid()) {
		return;
	}
	std::size_t remaining = body_->size() - offset_;
	if (remaining > PUMP_CHUNK_SIZE) {
		remaining = PUMP_CHUNK_SIZE;
	}

	ssize_t sent = ::write(pipe_.get(), body_->data() + offset_, remaining);
	if (sent <= 0) {
		finish();  // o script fechou o stdin antes de ler tudo
		return;
	}
	offset_ += static_cast<std::size_t>(sent);
	if (offset_ >= body_->size()) {
		finish();
	}
}

void CgiStdinPump::onHangup() { finish(); }

bool CgiStdinPump::wantsClose() const { return !pipe_.valid(); }

void CgiStdinPump::checkTimeout(std::time_t, std::time_t) {}

void CgiStdinPump::detachOwner() {
	owner_ = 0;
	body_  = 0;
	pipe_.reset();
}

// O EventLoop destroi este objeto assim que wantsClose() vira true, entao o
// CgiHandler precisa largar o ponteiro no mesmo instante.
void CgiStdinPump::finish() {
	pipe_.reset();
	body_ = 0;
	if (owner_ != 0) {
		CgiHandler* owner = owner_;
		owner_ = 0;
		owner->onStdinClosed();
	}
}
