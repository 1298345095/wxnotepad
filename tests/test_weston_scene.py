"""Keep surface activation and title-based dialog mapping on the same scene rules."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from weston_scene import mapped_toplevel


def view(pid=42, surface=7, title="Save changes?", role="xdg_toplevel", marker=""):
    return (f"\tView 1 (role {role}, PID {pid}, surface ID {surface}, "
            f"top-level window '{title}' of app 'wxnotepad')\n"
            f"\t\tposition: (0, 0)\n\t\t{marker}\n")


class WestonScene(unittest.TestCase):
    def test_both_identity_forms_match_owned_mapped_view(self):
        title = "Save [draft]'s changes?"
        scene = "Layer 0x1 (pos 0):\n" + view(title=title)
        self.assertTrue(mapped_toplevel(scene, 42, 7))
        self.assertTrue(mapped_toplevel(scene, "42", "7"))
        self.assertTrue(mapped_toplevel(scene, 42, title=title))
        self.assertTrue(mapped_toplevel(scene, "42", title=title))
        self.assertFalse(mapped_toplevel(scene, 43, 7))
        self.assertFalse(mapped_toplevel(scene, 42, 8))
        self.assertFalse(mapped_toplevel(scene, 42, title="Save"))

    def test_identity_values_cannot_expand_into_regex_patterns(self):
        scene = "Layer 0x1 (pos 0):\n" + view()
        for process in ("4.", r"4\d", "42|43", ".*"):
            with self.subTest(process=process):
                self.assertFalse(mapped_toplevel(scene, process, 7))
                self.assertFalse(mapped_toplevel(scene, process, title="Save changes?"))
        for surface in (".", r"\d", "7|8", ".*"):
            with self.subTest(surface=surface):
                self.assertFalse(mapped_toplevel(scene, 42, surface))

    def test_views_outside_layers_and_popup_roles_do_not_match(self):
        for scene in (view(), view() + "Layer 0x1 (pos 0):\n",
                      "Layer 0x1 (pos 0):\n" + view(role="xdg_popup")):
            with self.subTest(scene=scene):
                self.assertFalse(mapped_toplevel(scene, 42, 7))
                self.assertFalse(mapped_toplevel(scene, 42, title="Save changes?"))

    def test_all_unmapped_markers_apply_to_both_callers(self):
        for subject in ("view", "surface"):
            for status in ("not mapped!", "not part of any layer", "under parent view layer"):
                scene = "Layer 0x1 (pos 0):\n" + view(marker=f"[{subject} is {status}]")
                with self.subTest(subject=subject, status=status):
                    self.assertFalse(mapped_toplevel(scene, 42, 7))
                    self.assertFalse(mapped_toplevel(scene, 42, title="Save changes?"))

    def test_other_views_cannot_supply_identity_or_unmapped_status(self):
        scene = ("Layer 0x1 (pos 0):\n" + view(surface=7, marker="[view is not mapped!]") +
                 view(surface=8) + view(pid=43, marker="[surface is not mapped!]"))
        self.assertFalse(mapped_toplevel(scene, 42, 7))
        self.assertTrue(mapped_toplevel(scene, 42, 8))
        self.assertTrue(mapped_toplevel(scene, 42, title="Save changes?"))
        self.assertFalse(mapped_toplevel(scene, 43, title="Save changes?"))


if __name__ == "__main__":
    unittest.main()
