#include "core/EventLoop.hpp"

EventLoop::EventLoop() : pollables_(), running_(false) {}
EventLoop::~EventLoop() {}

void EventLoop::add(IPollable* pollable) {
	if (pollable) pollables_.push_back(pollable);
}

void EventLoop::remove(IPollable* pollable) {
	for (std::vector<IPollable*>::iterator it = pollables_.begin();
		 it != pollables_.end(); ++it) {
		if (*it == pollable) { pollables_.erase(it); return; }
	}
}

void EventLoop::runOnce(int timeoutMs) {
	std::vector<pollfd> fds;
	fds.reserve(pollables_.size());

	for (std::vector<IPollable*>::iterator it = pollables_.begin();
		 it != pollables_.end(); it++) {
		IPollable* p = *it;

		pollfd fd;
		fd.fd = p->fd();
		fd.events = p->interest();
		fd.revents = 0;
		fds.push_back(fd);
	}

	pollfd* fdsData = fds.empty() ? NULL : &fds[0];
	int return_poll = poll(fdsData, fds.size(), timeoutMs);
	if (return_poll < 0) {
		if (errno == EINTR)
			return;
		throw std::runtime_error(std::string("POLL FAILED: ") + std::strerror(errno));
	}

	for (std::size_t i = 0; i < fds.size(); i++) {
		IPollable* p = pollables_[i];
		short revents = fds[i].revents;

		if (revents & (POLLHUP | POLLERR))
			p->onHangup();
		if (revents & POLLIN)
			p->onReadable();
		if (revents & POLLOUT)
			p->onWritable();
	}

	reapClosed();
}

void EventLoop::run() {
	running_ = true;
	while (running_) {
		runOnce(1000);
	}
}

void EventLoop::stop() { running_ = false; }
bool EventLoop::isRunning() const { return running_; }

void EventLoop::reapClosed() {
	for (std::vector<IPollable*>::iterator it = pollables_.begin();
		 it != pollables_.end();) {
		IPollable* p = *it;

		if (p->wantsClose() == true) {
			std::ostringstream oss;
			oss << "DELETE FD " << p->fd();
			LOG_INFO(oss.str());
			it = pollables_.erase(it);
			delete p;
		}
		else
			it++;
	}
}
