#ifndef WEBSERV_CGI_CGI_STDIN_PUMP_HPP
#define WEBSERV_CGI_CGI_STDIN_PUMP_HPP

#include "core/IPollable.hpp"
#include "common/FileDescriptor.hpp"
#include <string>
#include <ctime>


class CgiHandler;

// O stdin e o stdout do script precisam ser vigiados ao mesmo tempo: um script
// que ecoa o body enche o proprio stdout e para de ler enquanto o servidor
// ainda esta escrevendo. Como cada IPollable cobre um fd so, o CgiHandler fica
// com o stdout e a escrita do body vive aqui, como um segundo pollable.
class CgiStdinPump : public IPollable {
public:
	CgiStdinPump(CgiHandler& owner, int writeFd, const std::string& body);
	~CgiStdinPump();

	// O CgiHandler terminou antes da escrita acabar: fecha o stdin do script.
	void detachOwner();

	int   fd() const;
	short interest() const;
	void  onReadable();
	void  onWritable();
	void  onHangup();
	bool  wantsClose() const;
	void  checkTimeout(std::time_t now, std::time_t timeout);

private:
	CgiHandler*        owner_;
	FileDescriptor     pipe_;
	const std::string* body_;
	std::size_t        offset_;

	void finish();

	CgiStdinPump(const CgiStdinPump&);
	CgiStdinPump& operator=(const CgiStdinPump&);
};


#endif
