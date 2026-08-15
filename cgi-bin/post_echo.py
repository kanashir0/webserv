#!/usr/bin/env python3
import os, sys

length = int(os.environ.get("CONTENT_LENGTH", "0") or 0)
body = sys.stdin.read(length) if length > 0 else ""

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
sys.stdout.write("Received {} bytes\n".format(length))
sys.stdout.write(body)
