#include "config/LocationConfig.hpp"


LocationConfig::LocationConfig()
	: path()
	, methods()
	, root()
	, index()
	, autoindex(false)
	, autoindexSet(false)
	, redirect()
	, redirectCode(302)
	, uploadStore()
	, cgi()
	, clientMaxBodySize(0)
	, clientMaxBodySizeSet(false)
	, errorPages()
{}

