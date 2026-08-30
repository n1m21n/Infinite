#!/usr/bin/env python3
"""Static consistency check for Infinite's connection logic.

What may connect to what is decided in five places, and only two of them are
generic. This checks that a node declaring a cable is actually reachable
through the hand-maintained ones.

  src/main.cpp:
    InputCountFor        - how many input pins a node draws
    CableFor             - which ImageCable* an image input slot maps to
    IsInputSlotCompatible- what a slot accepts (generic fallbacks at the end)
    WireInputSlot        - performs the connection
    DisconnectAllTo      - clears every cable pointing at a deleted node
  src/core/INode.h (generic - nothing to maintain):
    AudioInputSlot / NoteInputSlot / GeometryInputSlot / ModulatorInputSlot

Checks:
  1. every class with an ImageCable member is named in CableFor (or is
     covered by a base class listed in BASE_COVERED)
  2. every class named in CableFor is also named in InputCountFor, or takes
     its pin count from the generic probe (audio/note/modulator) - a node
     with a cable and no pin is unreachable
  3. every class with an AudioCable/NoteCable member overrides the matching
     generic slot accessor, since that is how audio/note dispatch finds it
  4. reports which nodes are special-cased in IsInputSlotCompatible and
     WireInputSlot, for review - absence there is legitimate (both end in
     generic fallbacks), so it is informational, not a failure

Exit code 0 if checks 1-3 are clean.
"""
import os, re, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
MAIN = os.path.join(ROOT, "src", "main.cpp")

# Classes whose image input is resolved through a base class already listed in
# CableFor. Each entry asserts "the dynamic_cast in CableFor matches this type
# too". Adding one is a deliberate act - verify the inheritance first.
BASE_COVERED = {
    "ViewportNode": "derives from NullNode, which CableFor and InputCountFor both name",
}


def block(src, header, end="\n   }"):
    i = src.index(header)
    return src[i:src.index(end, i)]


def classes_with(member_type, headers):
    """{class name: (file, count)} for classes declaring a member of that type."""
    out = {}
    for h in headers:
        s = open(h, encoding="utf-8", errors="replace").read()
        decls = [(m.start(), m.group(1)) for m in re.finditer(r"\bclass\s+(\w+)\s*(?:final\s*)?:", s)]
        for m in re.finditer(r"\b%s\s+(\w+)\s*;" % member_type, s):
            owner = None
            for pos, name in decls:
                if pos < m.start():
                    owner = name
                else:
                    break
            if owner:
                f, n = out.get(owner, (os.path.basename(h), 0))
                out[owner] = (f, n + 1)
    return out


def main():
    src = open(MAIN, encoding="utf-8", errors="replace").read()
    headers = sorted(glob.glob(os.path.join(ROOT, "src", "nodes", "*.h")))

    cable_for = block(src, "ImageCable* CableFor")
    input_count = block(src, "int InputCountFor")
    compat = block(src, "bool IsInputSlotCompatible", "\n   }")
    wire = block(src, "void WireInputSlot", "\n   }")

    named = lambda blk: set(re.findall(r"dynamic_cast<(\w+)\*>", blk))
    in_cable, in_count = named(cable_for), named(input_count)
    failures = 0

    # --- 1 ------------------------------------------------------------------
    img = classes_with("ImageCable", headers)
    missing = [c for c in sorted(img) if c not in in_cable and c not in BASE_COVERED]
    print("=== classes with an ImageCable member but absent from CableFor ===")
    for c in missing:
        print(f"  {c:28} {img[c][0]} ({img[c][1]} cable(s)) - its pin will accept a link and drop it")
    print("  (none)" if not missing else "")
    failures += len(missing)

    # --- 2 ------------------------------------------------------------------
    # A node in CableFor whose pin count comes from the generic audio/note/
    # modulator probe rather than an explicit InputCountFor row is fine; the
    # unsafe case is an image-only node with no row at all.
    no_pins = [c for c in sorted(in_cable)
               if c not in in_count and c not in ("Render3DNode",)]
    print("=== named in CableFor but with no InputCountFor row ===")
    for c in no_pins:
        print(f"  {c:28} check it gets pins from the generic audio/note/modulator probe")
    print("  (none)" if not no_pins else "")
    failures += len(no_pins)

    # --- 3 ------------------------------------------------------------------
    print("=== audio/note cable members without the generic slot accessor ===")
    bad = []
    for member, accessor in (("AudioCable", "AudioInputSlot"), ("NoteCable", "NoteInputSlot")):
        owners = classes_with(member, headers)
        for c in sorted(owners):
            f = os.path.join(ROOT, "src", "nodes", owners[c][0])
            s = open(f, encoding="utf-8", errors="replace").read()
            if accessor not in s:
                bad.append(f"  {c:28} {owners[c][0]} declares {member} but nothing in that file overrides {accessor}")
    for b in bad:
        print(b)
    print("  (none)" if not bad else "")
    failures += len(bad)

    # --- 4 (informational) ---------------------------------------------------
    print("=== special-cased in IsInputSlotCompatible (everything else falls "
          "through to the generic image/geometry/modulator rules) ===")
    print("  " + ", ".join(sorted(named(compat))))
    print("=== special-cased in WireInputSlot ===")
    print("  " + ", ".join(sorted(named(wire))))

    print(f"\nCABLELOGICSWEEP {len(in_cable)} image-input types, {len(in_count)} pin-count rows, "
          f"{failures} problem(s): {'OK' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
