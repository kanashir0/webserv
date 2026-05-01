#ifndef WEBSERV_SESSION_SESSION_HPP
#define WEBSERV_SESSION_SESSION_HPP

#include <string>
#include <map>
#include <ctime>

namespace ws {

class Session {
public:
	Session();
	explicit Session(const std::string& id);
	~Session();

	const std::string& id() const;

	bool        has(const std::string& key) const;
	std::string get(const std::string& key) const;
	void        set(const std::string& key, const std::string& value);
	void        erase(const std::string& key);

	std::time_t expiresAt() const;
	void        touch(int ttlSeconds);
	bool        expired(std::time_t now) const;

private:
	std::string                        id_;
	std::map<std::string, std::string> data_;
	std::time_t                        expiresAt_;
};

}

#endif
