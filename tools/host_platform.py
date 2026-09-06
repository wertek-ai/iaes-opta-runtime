"""Windows needs Winsock named explicitly; other hosts have sockets in libc.

A pre-script rather than a build flag, because a flag would have to be either
wrong on Windows or unlinkable on GNU/Linux.
"""

# SPDX-License-Identifier: MIT
import sys

Import("env")  # noqa: F821  (PlatformIO injects this)

if sys.platform.startswith("win"):
    env.Append(LIBS=["ws2_32"])
