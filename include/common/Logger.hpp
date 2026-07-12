#ifndef WEBSERV_COMMON_LOGGER_HPP
#define WEBSERV_COMMON_LOGGER_HPP

#include <string>


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


#define LOG_DEBUG(msg) ::Logger::instance().log(::Logger::DEBUG, msg)
#define LOG_INFO(msg)  ::Logger::instance().log(::Logger::INFO,  msg)
#define LOG_WARN(msg)  ::Logger::instance().log(::Logger::WARN,  msg)
#define LOG_ERROR(msg) ::Logger::instance().log(::Logger::ERROR, msg)

#endif
