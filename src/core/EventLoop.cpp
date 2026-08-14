#include "core/EventLoop.hpp"

EventLoop::EventLoop() : pollables_(), tickHandler_(0), running_(false) {}
EventLoop::~EventLoop() {
	for (std::vector<IPollable*>::iterator it = pollables_.begin();
	     it != pollables_.end(); ++it) {
		delete *it;
	}
}

void EventLoop::setTickHandler(ITickable* handler) { tickHandler_ = handler; }

void EventLoop::add(IPollable* pollable) {
	if (pollable) pollables_.push_back(pollable);
}

void EventLoop::runOnce(int timeoutMs, int timeoutSec) {
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
		// Consultar errno aqui e permitido: a proibicao do subject vale apenas
		// apos read/recv/write/send. poll() interrompido por sinal nao e erro.
		if (errno == EINTR)
			return;
		throw std::runtime_error(std::string("POLL FAILED: ") + std::strerror(errno));
	}

	time_t now = std::time(NULL);
	for (std::vector<IPollable*>::iterator it = pollables_.begin();
		 it != pollables_.end(); it++) {
		(*it)->checkTimeout(now, timeoutSec);
	}

	for (std::size_t i = 0; i < fds.size(); i++) {
		IPollable* p = pollables_[i];
		short revents = fds[i].revents;

		if (revents & POLLIN)
			p->onReadable();
		if (revents & POLLOUT)
			p->onWritable();
		if (revents & (POLLHUP | POLLERR))
			p->onHangup();
	}
}

void EventLoop::run() {
	running_ = true;
	while (running_) {
		runOnce(1000, 60);
		reapClosed();

		if (tickHandler_)
			tickHandler_->onTick();

		if (g_shutdown)
			stop();
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

