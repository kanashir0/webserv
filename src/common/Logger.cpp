#include "common/Logger.hpp"
#include <iostream>


Logger::Logger() : level_(INFO) {}
Logger::~Logger() {}

Logger& Logger::instance() {
	static Logger inst;
	return inst;
}

void Logger::setLevel(Level level) { level_ = level; }

void Logger::log(Level level, const std::string& msg) {
	if (level < level_) return;
	const char* tag = "INFO";
	switch (level) {
		case DEBUG: tag = "DEBUG"; break;
		case INFO:  tag = "INFO";  break;
		case WARN:  tag = "WARN";  break;
		case ERROR: tag = "ERROR"; break;
	}
	std::ostream& out = (level >= WARN) ? std::cerr : std::cout;
	out << "[" << tag << "] " << msg << std::endl;
}

