"""Read the configured build values shared by packaging and SDK checks."""

import re


def read_cmake_cache(directory):
    values = {}
    for line in (directory / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        entry = re.match(r"([^#/][^:]*):[^=]+=(.*)", line)
        if entry:
            values[entry[1]] = entry[2]
    return values
