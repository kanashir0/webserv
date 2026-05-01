#include "config/ConfigParser.hpp"
#include "core/Server.hpp"
#include "http/Router.hpp"
#include "common/Logger.hpp"
#include <iostream>
#include <vector>
#include <string>

int main(int argc, char** argv) {
	std::string confPath = (argc >= 2) ? argv[1] : "conf/default.conf";

	try {
		ws::ConfigParser parser;
		std::vector<ws::ServerConfig> configs = parser.parseFile(confPath);

		ws::SessionStore sessions;
		ws::Router       router(configs, sessions);
		ws::Server       server(configs, router);

		LOG_INFO("webserv starting (skeleton, no real I/O yet)");
		server.start();
		server.loop().run();
	} catch (const std::exception& e) {
		std::cerr << "fatal: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
