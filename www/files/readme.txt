This directory has no index file on purpose.

The `/files` location sets `autoindex on`, so requesting /files/ makes webserv
generate a directory listing instead of returning 403 Forbidden.
