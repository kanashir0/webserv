#!/usr/bin/env python3
import os, sys

sys.stdout.write("Content-Type: text/plain\r\n\r\n")
for k in sorted(os.environ):
    sys.stdout.write("{}={}\n".format(k, os.environ[k]))
