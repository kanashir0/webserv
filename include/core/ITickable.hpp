#ifndef WEBSERV_CORE_ITICKABLE_HPP
#define WEBSERV_CORE_ITICKABLE_HPP


class ITickable {
public:
	virtual ~ITickable() {}

	virtual void onTick() = 0;
};


#endif
