#include "session/Session.hpp"


Session::Session() : id_(), data_(), expiresAt_(0) {}
Session::Session(const std::string& id) : id_(id), data_(), expiresAt_(0) {}
Session::~Session() {}

const std::string& Session::id() const { return id_; }

bool Session::has(const std::string& key) const {
	return data_.find(key) != data_.end();
}

std::string Session::get(const std::string& key) const {
	std::map<std::string, std::string>::const_iterator it = data_.find(key);
	if (it == data_.end()) return std::string();
	return it->second;
}

void Session::set(const std::string& key, const std::string& value) {
	data_[key] = value;
}

void Session::erase(const std::string& key) { data_.erase(key); }

std::time_t Session::expiresAt() const { return expiresAt_; }

void Session::touch(int ttlSeconds) {
	expiresAt_ = std::time(0) + ttlSeconds;
}

bool Session::expired(std::time_t now) const {
	return expiresAt_ != 0 && now >= expiresAt_;
}

