#ifndef WEBSERV_COMMON_LOGGER_HPP
#define WEBSERV_COMMON_LOGGER_HPP

#include <string>

namespace ws {

class Logger {
public:
	enum Level { DEBUG, INFO, WARN, ERROR };

	static Logger& instance();

	void setLevel(Level level);
	void log(Level level, const std::string& msg);

private:
	Logger();
	~Logger();
	Logger(const Logger&);
	Logger& operator=(const Logger&);

	Level level_;
};

}

#define LOG_DEBUG(msg) ::ws::Logger::instance().log(::ws::Logger::DEBUG, msg)
#define LOG_INFO(msg)  ::ws::Logger::instance().log(::ws::Logger::INFO,  msg)
#define LOG_WARN(msg)  ::ws::Logger::instance().log(::ws::Logger::WARN,  msg)
#define LOG_ERROR(msg) ::ws::Logger::instance().log(::ws::Logger::ERROR, msg)

#endif
