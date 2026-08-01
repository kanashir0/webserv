#ifndef WEBSERV_SESSION_SESSION_STORE_HPP
#define WEBSERV_SESSION_SESSION_STORE_HPP

#include "session/Session.hpp"
#include "core/ITickable.hpp"
#include <string>
#include <map>


class SessionStore : public ITickable {
public:
	SessionStore();
	~SessionStore();

	Session& getOrCreate(const std::string& id);
	Session* find(const std::string& id);
	void     drop(const std::string& id);
	void     gc();
	void     onTick();

	void setTtlSeconds(int ttl);
	int  ttlSeconds() const;

private:
	std::map<std::string, Session> store_;
	int                            ttlSeconds_;

	std::string generateId();

	SessionStore(const SessionStore&);
	SessionStore& operator=(const SessionStore&);
};


#endif
