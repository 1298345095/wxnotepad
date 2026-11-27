"""Explicit source-language settings for existing English process baselines."""

import re


def english_settings(text):
    """Retain the fixture's settings and choose the original English LANGID."""
    section = re.search(r"(?im)^\[Settings2\][ \t]*$", text)
    if section:
        return text[: section.end()] + "\nUILanguage=1033" + text[section.end() :]
    return "[Settings2]\nUILanguage=1033\n" + text
