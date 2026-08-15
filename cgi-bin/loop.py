#!/usr/bin/env python3
# Test fixture: never terminates and never writes anything.
# The server must give up on its own and answer 504 Gateway Timeout.

while True:
    pass
