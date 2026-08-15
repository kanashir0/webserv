#!/usr/bin/env python3
# Test fixture: crashes with an uncaught exception after writing a partial,
# header-less response. The server must answer 502 Bad Gateway.
import sys

sys.stdout.write("this is not a valid CGI header block\n")
sys.stdout.flush()

raise RuntimeError("intentional CGI failure")
