"""Canonical destination boundary for reports selected by CLI callers."""


def report_path(path, directory):
    resolved = path.resolve()
    if not resolved.is_relative_to(directory):
        raise ValueError(f'Report path must stay within the working directory: {path}')
    return resolved
