#!/usr/bin/env bash
# Roda webserv sob valgrind para deteccao de leaks.
set -u
CONF="${1:-conf/default.conf}"
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes ./webserv "$CONF"
