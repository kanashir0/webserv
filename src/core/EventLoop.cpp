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

void EventLoop::runOnce(int /*timeoutMs*/) {
	// TODO Membro 1: montar pollfd[], poll(), despachar onReadable/onWritable/onHangup
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
	// TODO Membro 1: remover pollables com wantsClose() == true e deletar
}

