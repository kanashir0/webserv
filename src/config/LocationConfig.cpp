#include "config/LocationConfig.hpp"

namespace ws {

LocationConfig::LocationConfig()
	: path()
	, methods()
	, root()
	, index()
	, autoindex(false)
	, redirect()
	, redirectCode(302)
	, uploadStore()
	, cgi()
{}

}
