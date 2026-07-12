#include "session/SessionStore.hpp"
#include <ctime>


SessionStore::SessionStore() : store_(), ttlSeconds_(3600) {}
SessionStore::~SessionStore() {}

Session& SessionStore::getOrCreate(const std::string& id) {
	std::string key = id.empty() ? generateId() : id;
	std::map<std::string, Session>::iterator it = store_.find(key);
	if (it == store_.end()) {
		Session s(key);
		s.touch(ttlSeconds_);
		it = store_.insert(std::make_pair(key, s)).first;
	} else {
		it->second.touch(ttlSeconds_);
	}
	return it->second;
}

Session* SessionStore::find(const std::string& id) {
	std::map<std::string, Session>::iterator it = store_.find(id);
	if (it == store_.end()) return 0;
	return &it->second;
}

void SessionStore::drop(const std::string& id) { store_.erase(id); }

void SessionStore::gc() {
	std::time_t now = std::time(0);
	std::map<std::string, Session>::iterator it = store_.begin();
	while (it != store_.end()) {
		if (it->second.expired(now)) {
			std::map<std::string, Session>::iterator del = it++;
			store_.erase(del);
		} else {
			++it;
		}
	}
}

void SessionStore::setTtlSeconds(int ttl) { ttlSeconds_ = ttl; }
int  SessionStore::ttlSeconds() const     { return ttlSeconds_; }

std::string SessionStore::generateId() {
	// TODO Membro 3: gerar ID criptograficamente robusto (hex de /dev/urandom)
	static unsigned long counter = 0;
	++counter;
	std::string id = "sess-";
	for (int i = 0; i < 16; ++i) {
		unsigned long v = (counter * 2654435761UL + i * 97UL) & 0xFu;
		id += static_cast<char>(v < 10 ? ('0' + v) : ('a' + (v - 10)));
	}
	return id;
}

