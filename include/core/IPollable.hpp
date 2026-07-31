#ifndef WEBSERV_CORE_IPOLLABLE_HPP
#define WEBSERV_CORE_IPOLLABLE_HPP

#include <ctime>


class IPollable {
public:
	virtual ~IPollable() {}

	virtual int   fd() const = 0;
	virtual short interest() const = 0;

	virtual void onReadable() = 0;
	virtual void onWritable() = 0;
	virtual void onHangup()   = 0;

	virtual bool wantsClose() const = 0;

	virtual void checkTimeout(std::time_t now, std::time_t timeout) = 0;
};


#endif
