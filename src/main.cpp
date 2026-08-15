#include "config/ConfigParser.hpp"
#include "core/Server.hpp"
#include "http/Router.hpp"
#include "common/Logger.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <csignal>
#include <cstdlib>
#include <ctime>

volatile sig_atomic_t g_shutdown;

void signalHandler(int) {
	g_shutdown = 1;
}

int main(int argc, char** argv) {
	std::signal(SIGINT, signalHandler);
	std::signal(SIGTERM, signalHandler);
	std::signal(SIGPIPE, SIG_IGN);
	// Semeia os identificadores de sessao (bonus). Nao e uma fonte segura.
	std::srand(static_cast<unsigned int>(std::time(0)));

	std::string confPath = (argc >= 2) ? argv[1] : "conf/default.conf";

	try {
		ConfigParser parser;
		std::vector<ServerConfig> configs = parser.parseFile(confPath);

		SessionStore sessions;
		Router       router(sessions);
		Server       server(configs, router, sessions);

		LOG_INFO("webserv starting");
		server.start();
	} catch (const ConfigParser::ParseError& e) {
		std::cerr << "[ERROR] " << confPath << ":" << e.line() << ": " << e.what() << std::endl;
		return 1;
	} catch (const std::exception& e) {
		std::cerr << "fatal: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
