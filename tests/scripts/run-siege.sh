#!/usr/bin/env bash
# Stress test rapido. Requer `siege` instalado.
set -u
HOST="${1:-localhost:8080}"
siege -b -c 50 -t 30s "http://$HOST/"
