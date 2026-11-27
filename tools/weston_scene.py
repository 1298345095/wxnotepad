"""Match owned, mapped toplevels in a Weston scene-graph snapshot."""

import re


def _mapped_view_sections(scene):
    layered = False
    for section in re.split(r"(?=^Layer |^\tView )", scene, flags=re.MULTILINE):
        if section.startswith("Layer "):
            layered = True
        if (
            layered
            and section.startswith("\tView ")
            and not re.search(
                r"\[(?:view|surface) is (?:not mapped!|not part of any layer|under parent view layer)\]", section
            )
        ):
            yield section


def _mapped_toplevel_sections(scene, process, surface=None, *, title=None):
    process_pattern = re.escape(str(process))
    surface_pattern = r"\d+" if surface is None else re.escape(str(surface))
    identity = rf"\tView \d+ \(role xdg_toplevel, PID {process_pattern}, surface ID {surface_pattern}, "
    if title is None:
        identity += "top-level window '"
    for section in _mapped_view_sections(scene):
        if not re.match(identity, section):
            continue
        if title is not None and f"top-level window '{title}' of " not in section.splitlines()[0]:
            continue
        yield section


def mapped_toplevel(scene, process, surface=None, *, title=None):
    return next(_mapped_toplevel_sections(scene, process, surface, title=title), None) is not None


def input_ready_toplevel(scene, process, surface):
    # Weston's startup fade captures input even while translucent. Its layer
    # can coexist with the mapped target until the real curtain is destroyed.
    curtain = r"\tView \d+ \(role \(null\), PID 0, surface ID 0, desktop shell fade surface, "
    return mapped_toplevel(scene, process, surface) and not any(
        re.match(curtain, section) for section in _mapped_view_sections(scene)
    )


def mapped_toplevel_position(scene, process, surface):
    for section in _mapped_toplevel_sections(scene, process, surface):
        position = re.search(r"^\s*position: \((-?\d+), (-?\d+)\)", section, flags=re.MULTILINE)
        if position:
            return tuple(map(int, position.groups()))
    return None
