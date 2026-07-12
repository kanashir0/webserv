#ifndef WEBSERV_CORE_IPOLLABLE_HPP
#define WEBSERV_CORE_IPOLLABLE_HPP


class IPollable {
public:
	virtual ~IPollable() {}

	virtual int   fd() const = 0;
	virtual short interest() const = 0;

	virtual void onReadable() = 0;
	virtual void onWritable() = 0;
	virtual void onHangup()   = 0;

	virtual bool wantsClose() const = 0;
};


#endif
