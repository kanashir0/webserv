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

	if (fds.empty())
		return;

	int return_poll = poll(&fds[0], fds.size(), timeoutMs);
	if (return_poll < 0) {
		if (errno == EINTR)
			return;
		throw std::runtime_error(std::string("POLL FAILED: ") + std::strerror(errno));
	}

	for (int i = 0; fds.size(); i++) {
		IPollable* p = pollables_[i];
		short revents = fds[i].revents;

		if (revents & (POLLHUP | POLLERR))
			p->onHangup();
		else if (revents & POLLIN)
			p->onReadable();
		else if (revents & POLLOUT)
			p->onWritable();
	}
}

void EventLoop::run() {
	running_ = true;
	while (running_) {
		runOnce(1000);
		reapClosed();
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

