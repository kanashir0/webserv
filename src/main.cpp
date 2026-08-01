#include "config/ConfigParser.hpp"
#include "core/Server.hpp"
#include "http/Router.hpp"
#include "common/Logger.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <signal.h>

volatile sig_atomic_t g_shutdown;

void signalHandler(int) {
	g_shutdown = 1;
}

int main(int argc, char** argv) {
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);

	std::string confPath = (argc >= 2) ? argv[1] : "conf/default.conf";

	try {
		ConfigParser parser;
		std::vector<ServerConfig> configs = parser.parseFile(confPath);

		SessionStore sessions;
		Router       router(sessions);
		Server       server(configs, router);

		LOG_INFO("webserv starting (skeleton, no real I/O yet)");
		// std::vector<ServerConfig> configue;
		// configue.push_back(ServerConfig());
		// Server       servidor(configue, router);
		server.start();
	} catch (const std::exception& e) {
		std::cerr << "fatal: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
