#include "session/SessionStore.hpp"
#include <cstdlib>
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

void SessionStore::onTick() { gc(); }

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

// 32 digitos hex a partir do relogio, de um contador e do rand() semeado no
// main(). Nao e criptografico — suficiente para a demonstracao de sessao, mas
// nao para autenticacao real.
std::string SessionStore::generateId() {
	static const char     hexDigits[] = "0123456789abcdef";
	static unsigned long  counter     = 0;
	++counter;

	unsigned long seed = static_cast<unsigned long>(std::time(0))
	                   ^ (counter * 2654435761UL);

	std::string id;
	id.reserve(32);
	for (int i = 0; i < 32; ++i) {
		seed = seed * 1103515245UL + 12345UL
		     + static_cast<unsigned long>(std::rand());
		id += hexDigits[(seed >> 16) & 0x0F];
	}
	return id;
}

