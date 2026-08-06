#ifndef WEBSERV_CORE_CLIENT_HPP
#define WEBSERV_CORE_CLIENT_HPP

#include "core/IPollable.hpp"
#include "common/FileDescriptor.hpp"
#include "common/StringUtils.hpp"
#include "config/ServerConfig.hpp"
#include "http/Request.hpp"
#include "http/RequestParser.hpp"
#include "http/Response.hpp"
#include <poll.h>
#include <sys/socket.h>
#include <string>
#include <vector>
#include <ctime>
#include <cerrno>

class Router;
class SessionStore;

class Client : public IPollable {
	public:
		enum State {
			DONE,
			READING_HEADERS,
			WRITING_RESPONSE
		};

		Client(int fd,
		       std::vector<ServerConfig>& vhosts,
		       Router& router,
		       SessionStore& sessions);
		~Client();

		int   fd() const;
		short interest() const;

		void onReadable();
		void onWritable();
		void onHangup();

		bool wantsClose() const;

		State state() const;
		std::time_t lastActivity() const;

		void checkTimeout(std::time_t now, std::time_t timeout);

	private:
		FileDescriptor fd_;
		State          state_;

		RequestParser  parser_;
		Request        request_;
		Response       response_;

		std::string outBuffer_;
		size_t      outOffset_;
		std::time_t lastActivity_;
		bool        wantsClose_;
		bool        responseSerialized_;
		bool		closeAfterWrite_;

		std::vector<ServerConfig>&       vhosts_;
		Router&                          router_;
		SessionStore&                    sessions_;

		const ServerConfig& matchVirtualHost() const;
		void                buildErrorResponse(int code);
		bool                tryConsumeResidual();

		Client(const Client&);
		Client& operator=(const Client&);
};


#endif
