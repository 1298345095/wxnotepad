"""Match owned, mapped toplevels in a Weston scene-graph snapshot."""
import re


def _mapped_toplevel_sections(scene, process, surface=None, *, title=None):
    process_pattern = re.escape(str(process))
    surface_pattern = r"\d+" if surface is None else re.escape(str(surface))
    identity = rf"\tView \d+ \(role xdg_toplevel, PID {process_pattern}, surface ID {surface_pattern}, "
    if title is None:
        identity += "top-level window '"
    layered = False
    for section in re.split(r"(?=^Layer |^\tView )", scene, flags=re.MULTILINE):
        if section.startswith("Layer "):
            layered = True
        if not layered or not re.match(identity, section):
            continue
        if title is not None and f"top-level window '{title}' of " not in section.splitlines()[0]:
            continue
        if not re.search(r"\[(?:view|surface) is (?:not mapped!|not part of any layer|under parent view layer)\]", section):
            yield section


def mapped_toplevel(scene, process, surface=None, *, title=None):
    return next(_mapped_toplevel_sections(scene, process, surface, title=title), None) is not None


def mapped_toplevel_position(scene, process, surface):
    for section in _mapped_toplevel_sections(scene, process, surface):
        position = re.search(r"^\s*position: \((-?\d+), (-?\d+)\)", section, flags=re.MULTILINE)
        if position:
            return tuple(map(int, position.groups()))
    return None
