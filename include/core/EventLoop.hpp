#ifndef WEBSERV_CORE_EVENT_LOOP_HPP
#define WEBSERV_CORE_EVENT_LOOP_HPP

#include "core/IPollable.hpp"
#include "common/Logger.hpp"
#include <iostream>
#include <cstring>
#include <poll.h>
#include <cerrno>
#include <vector>
#include <sstream>
#include <csignal>

extern volatile sig_atomic_t g_shutdown;

class ListeningSockets;

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
	std::vector<ListeningSockets*> listeners_;
	bool                    running_;

	void reapClosed();

	EventLoop(const EventLoop&);
	EventLoop& operator=(const EventLoop&);
};


#endif
