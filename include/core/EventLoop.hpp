#ifndef WEBSERV_CORE_EVENT_LOOP_HPP
#define WEBSERV_CORE_EVENT_LOOP_HPP

#include "core/IPollable.hpp"
#include <vector>


class EventLoop {
public:
	EventLoop();
	~EventLoop();

	void add(IPollable* pollable);
	void remove(IPollable* pollable);

	void runOnce(int timeoutMs);
	void run();
	void stop();

	bool isRunning() const;

private:
	std::vector<IPollable*> pollables_;
	bool                    running_;

	void reapClosed();

	EventLoop(const EventLoop&);
	EventLoop& operator=(const EventLoop&);
};


#endif
