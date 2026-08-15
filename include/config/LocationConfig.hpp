#ifndef WEBSERV_CONFIG_LOCATION_CONFIG_HPP
#define WEBSERV_CONFIG_LOCATION_CONFIG_HPP

#include "common/Types.hpp"
#include <string>
#include <map>
#include <cstddef>


// Campos que existem tambem no ServerConfig sao herdados quando a location nao
// os declara. Como `false` e `0` sao valores legitimos, quem nao pode usar uma
// sentinela carrega um booleano `...Set` dizendo se a diretiva apareceu.
// `root` e `index` dispensam a flag: string vazia nunca e um valor valido.
struct LocationConfig {
	std::string                        path;
	StringVec                          methods;
	std::string                        root;
	std::string                        index;
	bool                               autoindex;
	bool                               autoindexSet;
	std::string                        redirect;
	int                                redirectCode;
	std::string                        uploadStore;
	std::map<std::string, std::string> cgi;
	std::size_t                        clientMaxBodySize; // 0 = ilimitado
	bool                               clientMaxBodySizeSet;
	std::map<int, std::string>         errorPages;        // vazio = herda

	LocationConfig();
};


#endif
