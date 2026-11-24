"""The design kit and declared content regions: one list every design tool reads (ratchet, ui_audit, migrations).

KIT: files that ARE the components. Their raw ImGui calls and hand-drawn shapes are the component's own drawing,
so they are never counted as violations. Everything under src/app/ui/design/ plus the four kit files that live
outside it for historical reasons (internal/plans/ui-violator-cleanup.md, Block 1).

Content regions: drawing that is the picture, not the chrome around it (a scope trace, a canvas, a grid). Marked in
source as

    // ui-content-begin: <reason>
    ...
    // ui-content-end

Lints skip the region's hand shapes, hand text and layout calls; numbers inside still go through ui_audit (they must
be tokens). Every region is listed with its reason by `python3 tools/design/content_report.py`, and the ratchet
stores the region count per file, so a new region is a reviewed change to ratchet.json, never a silent exemption.
"""
import re

KIT = ("src/app/ui/design/",
       "src/app/ui/ModWidgets.cpp",      # knob, mod dot, pin: the one Knob
       "src/app/ui/ParamWidgets.cpp",    # param-table row drawing (Ui::DrawParams)
       "src/app/ui/Theme.cpp",           # the ImGui style itself
       "src/app/bodies/AudioBodyKit.cpp")  # AudioKnobRow, sections, readout strip

CONTENT_BEGIN = re.compile(r"//\s*ui-content-begin:\s*(\S.*?)\s*$", re.M)
CONTENT_REGION = re.compile(r"//\s*ui-content-begin:[^\n]*\n.*?//\s*ui-content-end\b", re.S)


def is_kit(rel):
    return rel.startswith(KIT)


def content_out(text):
    """the file with every declared content region blanked (line count kept, so line numbers still match)"""
    return CONTENT_REGION.sub(lambda m: "\n" * m.group(0).count("\n"), text)


def content_regions(text):
    """[(line, reason, body)] for each declared region; an unclosed begin is returned with body None"""
    out = []
    for m in CONTENT_BEGIN.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        end = re.search(r"//\s*ui-content-end\b", text[m.end():])
        out.append((line, m.group(1), text[m.end():m.end() + end.start()] if end else None))
    return out
