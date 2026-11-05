"""Chapter 29 of the Node Field Guide: Using the App (panels, menus, dropdowns, right-click menus, settings).

Screenshots live in docs/ui_screenshots/. The window, menu, panel, settings and dialog shots are captured by
tools/manual/ui_shots_offscreen.py (an off-screen run of the real app, 1600x1000 pt window, 2x; boxes for those are
in points). The Performance and Timeline detail shots are older captures from tools/manual/ui_shots.py (1440 pt window). Wide panels are cropped to the part being explained so the text stays legible at page
width; crops are never rescaled up or recoloured (BRAND.md section 10.1).
"""
import html
import os
import tempfile

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHOTS = os.path.join(ROOT, "docs", "ui_screenshots")
_TMP = tempfile.mkdtemp(prefix="ui_chapter_")
THUMB = 1400.0  # boxes below are in a 1400-px-wide view of the 1440pt window; scaled to the 2x capture


def esc(s):
    return html.escape(s, quote=False)


def fig(name, caption, box=None, width="100%"):
    path = os.path.join(SHOTS, name + ".png")
    if box:
        with Image.open(path) as im:
            k = im.size[0] / (1600.0 if im.size[0] == 3200 else THUMB)
            out = os.path.join(_TMP, f"{name}_{'_'.join(map(str, box))}.png")
            im.crop(tuple(int(v * k) for v in box)).save(out)
            path = out
    return (f'<figure class="fig" style="display:block;width:{width};margin:0 0 12px"><div class="shot" style="display:block;padding:8px">'
            f'<img src="file://{path}" style="width:100%;display:block"></div>'
            f'<figcaption>{esc(caption)}</figcaption></figure>')


def row(*figs):
    return '<div style="display:flex;gap:12px;align-items:flex-start">' + "".join(
        f'<div style="flex:{w}">{f}</div>' for f, w in figs) + "</div>"


def table(head, rows):
    t = '<table class="t"><tr>' + "".join(f"<th>{esc(h)}</th>" for h in head) + "</tr>"
    for r in rows:
        t += "<tr>" + "".join(f"<td>{c}</td>" if i else f"<td><b>{esc(c)}</b></td>" for i, c in enumerate(r)) + "</tr>"
    return t + "</table>"


def page(*parts):
    return '<div style="break-before:page"></div>' + "".join(parts)


def html_chapter(P):
    b = []
    b.append('<div class="kick">Chapter</div><div class="hand" style="font-size:40px;line-height:1">Twenty-nine</div>'
             '<h1>Using the App: Panels, Menus &amp; Settings</h1>'
             '<p class="lead">Everything around the nodes: the panels on the right, the strips along the bottom, the right-click menus, '
             'the dropdowns and the settings. Every picture here is a real screenshot of the app, taken by a script.</p>')
    b.append("<p>These are the questions people ask most: what does the library do, what is the modulation matrix, how do I build a "
             "performance page, how do clips and tracks work, what is in the right-click menu. Each section shows the panel big, "
             "then explains every column, icon and control in it.</p>")

    b.append("<h2>The window at a glance</h2>")
    b.append(fig("app_overview", "The whole window: menus and transport on top, the node canvas in the middle, the panel rail down the right edge."))
    b.append(fig("app_overview", "The top bar: logo, File, Edit and Menu, play and rewind, tempo, time signature, key and scale, metronome, the audio switch and the mode badge.", box=(0, 0, 1210, 46)))
    b.append(table(["Control", "What it does"], [
        ("Play / Rewind", "Starts and pauses the shared clock that drives sequencers, LFOs and the arrangement; rewind returns to the start."),
        ("BPM (120.0)", "Tempo. Double-click to type an exact value or drag to change it. Every tempo-synced node follows it."),
        ("Time signature (4 / 4)", "Beats per bar and the note value of a beat. It sets the bar lines in the arrangement."),
        ("Key and scale (C Major)", "The global key. Scale-aware nodes snap to it. The dropdown lists Major, Natural / Harmonic / Melodic Minor, the church modes, "
                                    "Major and Minor Pentatonic, Blues, Whole Tone and Chromatic."),
        ("Metronome", "Click track on or off."),
        ("Start Audio / Stop Audio", "Starts or stops the audio engine. While it is stopped you will hear nothing."),
        ("Timeline / Session / Studio badge", "Shows which of them is driving playback right now."),
        ("fps, ms, cpu", "Frame rate, frame time and audio load, at the far right when the window is wide enough. Hover the audio readout for dropout (xrun) counts."),
    ]))
    b.append(row((fig("app_overview", "Rail, top.", box=(1560, 40, 1600, 440))
                  + fig("app_overview", "Bottom.", box=(1560, 915, 1600, 1000)), 1),
                 (table(["Rail button", "Opens"], [
                     ("Library", "Every node, Field program, sample, media file and plugin."),
                     ("Viewport", "Live preview cards of any image node."),
                     ("Mod Matrix", "Every modulation route in one table."),
                     ("Performance", "Your own control surface of knobs, faders and pads."),
                     ("Timeline", "The arrangement: clips over time."),
                     ("Session", "Clip launcher: tracks across, scenes down."),
                     ("Studio", "Sample Studio: record, slice and export a take."),
                     ("3D", "3D View: the scene, its camera and gizmos."),
                     ("Piano Roll", "Note editing and Generate."),
                     ("Terminal", "A real shell inside the app, with the infinite command."),
                     ("History (bottom)", "Every edit, newest first."),
                     ("Settings (bottom)", "The Settings window."),
                 ]), 12)))

    b.append(page("<h2>Menus</h2>", "<p>The three menus at the top left. File handles patches, Edit handles undo and clipboard, "
                  "Menu holds the windows, help and the show/set tools.</p>",
                  row((fig("menu_file", "File", box=(0, 0, 400, 400)), 1), (fig("menu_edit", "Edit", box=(0, 0, 400, 410)), 1)),
                  row((fig("menu_menu", "Menu", box=(0, 0, 520, 350)), 1),
                      (fig("menu_panels", "Menu, Panels: open any panel and set where it docks.", box=(0, 0, 620, 350)), 1))))

    b.append(page("<h2>Library panel</h2>",
                  "<p>The top button of the panel rail opens the library: every node, sample, media file and plugin you can drop on the canvas. "
                  "Click an entry, or drag it onto the canvas.</p>",
                  row((fig("panel_library_big", "Library, Modules tab: search box, Sort, order toggle, Filter, then the alphabetical list.", box=(1190, 40, 1600, 1000)), 1),
                      (table(["Element", "What it does"], [
                          ("Tabs", "Modules (every node type), Field (Field-language nodes and examples), Samples and Media (your own files), Plugins (VST3 and AU instruments and effects)."),
                          ("Search", "Filters the list as you type."),
                          ("Sort", "Orders the list: favourites first, alphabetical and so on. The button beside it flips ascending and descending."),
                          ("Filter", "Narrows the list by category, for example only effects or only synths."),
                          ("List", "Click to add at the centre of the view, or drag to a spot. Star an entry to make it a favourite."),
                      ]), 1)),
                  row((fig("library_sort_dropdown", "Sort dropdown", box=(1190, 40, 1600, 560)), 1),
                      (fig("library_filter_dropdown", "Filter dropdown", box=(1190, 40, 1600, 560)), 1)),
                  row((fig("library_field", "Field tab", box=(1190, 40, 1600, 1000)), 1),
                      (fig("library_plugins", "Plugins tab", box=(1190, 40, 1600, 1000)), 1))))

    b.append(page("<h2>Viewport panel</h2>",
                  "<p>A live preview of any image node, next to the canvas. Right-click a node and choose <b>Open in viewport panel</b> (or select it and press Shift+V). "
                  "Each card follows its node in real time and has a close button at its top right. Add several cards to watch different stages of a chain at once.</p>",
                  row((fig("panel_viewport_cards", "No cards yet: the panel says how to add one.", box=(1230, 40, 1600, 1000)), 1),
                      (fig("rc_node", "Right-click a node for its menu.", box=(300, 300, 620, 520)), 2))))

    b.append(page("<h2>Modulation Matrix</h2>",
                  "<p>Every modulation in the patch in one table: which source drives which parameter, and how. Drag the splitter above the panel to make it taller. "
                  "The table is shown in two halves so each column is readable.</p>",
                  fig("panel_modmatrix_big", "Left half: Source, Destination, Parameter, Value, Confidence.", box=(0, 790, 820, 1000)),
                  fig("panel_modmatrix_big", "Right half: Lo, Hi, Inv, Curve, Signal and the delete button.", box=(800, 790, 1600, 1000)),
                  table(["Column", "What it means"], [
                      ("Colour bar", "The cable colour of the source, so you can match a row to the cable on the canvas."),
                      ("Source", "The node that produces the control signal: macro xy, random, an LFO, an envelope and so on."),
                      ("Destination", "The node that receives it."),
                      ("Parameter", "Which parameter on the destination is being moved."),
                      ("Value", "The live, current value of that parameter after modulation."),
                      ("Confidence", "Used by prediction sources to report how sure they are. It shows -- for ordinary sources."),
                      ("Lo / Hi", "The range the parameter travels between. Click a number to type an exact limit. Narrow it for subtle movement."),
                      ("Inv", "Inverts the mapping: when the source goes up, the parameter goes down."),
                      ("Curve", "The response curve between source and parameter, drawn as a small graph."),
                      ("Signal", "A tiny live scope of the incoming signal, so you can see it moving."),
                      ("x", "Removes the route. The cable on the canvas disappears with it."),
                  ])))

    b.append(page("<h2>Performance panel</h2>",
                  "<p>Build your own control surface from knobs, faders, pads and switches, then play it live. It opens in <b>Edit Mode</b>; "
                  "switch off Edit Mode (the button then reads <b>Perform</b>) to lock the layout and play it. Drag the splitter up for more room.</p>",
                  row((fig("panel_perf", "The Performance panel docked at the right, in Edit Mode, with the showcase page.", box=(1180, 40, 1600, 1000)), 1),
                      (fig("perf_edit", "Edit Mode: knobs, faders, number boxes and a slider on the Showcase page.", box=(1190, 60, 1555, 440)), 2)),
                  table(["Toolbar", "What it does"], [
                      ("Edit Mode / Perform", "Edit Mode lets you add, move, resize and rename controls. Perform locks the layout so a stray drag cannot move anything."),
                      ("+ Add Control", "Opens the control list below and places the chosen control on the next free cell."),
                      ("Page tabs, +", "Pages group controls into screens (here one page, Showcase). The + button adds a page."),
                  ]),
                  row((fig("perf_add_control", "+ Add Control menu: ten control types, with their grid size in brackets.", box=(1190, 60, 1580, 440)), 1),
                      (table(["Control", "Use it for"], [
                          ("Knob (1x1)", "Any single parameter."),
                          ("Vertical Fader (1x2)", "Levels and mixes."),
                          ("Horizontal Slider (2x1)", "Long throws, crossfades."),
                          ("Toggle (1x1)", "On/off switches and bypass."),
                          ("XY Pad (2x2)", "Two parameters with one finger."),
                          ("Momentary Trigger / Bang (1x1)", "Fires a one-shot while held."),
                          ("Digital Number Box (1x1)", "Type or drag an exact value."),
                          ("Radio Selector (2x1)", "Pick one of several steps, 1 to 8."),
                          ("Bipolar Pan Knob (1x1)", "Centre-detent knob for pan or detune."),
                          ("Step Gate Ribbon (3x1)", "Eight on/off steps for a gate pattern."),
                      ]), 1)),
                  row((fig("perf_control_menu2", "Right-click a control: Rename, Type, Assign Parameter, Clear Destination, MIDI CC Learn, Color Tint.", box=(1190, 60, 1555, 440)), 1),
                      (fig("perf_perform", "The same page in Perform mode: the toolbar shrinks to Perform and the page tabs.", box=(1190, 60, 1555, 440)), 1),
                      (table(["Right-click item", "What it does"], [
                      ("Rename", "Change the label shown on the control."),
                      ("Type", "Swap the control for another type without losing its assignment."),
                      ("Assign Parameter...", "Bind the control to a node parameter, then move the control to move the parameter. The current target is shown greyed below it."),
                      ("Clear Destination", "Unbinds the control."),
                      ("MIDI CC Learn", "Move a knob on your MIDI controller to bind it to this control."),
                      ("Color Tint", "Colour-code the control so groups are easy to find on stage."),
                  ]), 2))))

    b.append(page("<h2>Arrangement panel</h2>",
                  "<p>The timeline. It sequences clips of nodes over time so a patch becomes a piece with a start and an end. "
                  "Each icon in the toolbar is explained below; hover any of them in the app for its tooltip.</p>",
                  fig("panel_timeline", "The Timeline docked at the bottom: monitor, tracks, clips and markers.", box=(0, 760, 1600, 1000)),
                  fig("tl_overview", "The Timeline made tall: toolbar, monitor, track list, markers (Intro, Groove), ruler, clips and the Inspector.", box=(0, 60, 1550, 270)),
                  fig("tl_overview", "Right end of the toolbar: Render, Audio and the Inspector pencil.", box=(1380, 62, 1548, 96), width="28%"),
                  table(["Toolbar item", "What it does"], [
                      ("Viewport monitor", "Shows or hides the preview of the timeline output at the left of the panel. Right-click for its dock position."),
                      ("Play / Pause (Space)", "Plays the timeline from the playhead."),
                      ("Return to Start (Enter)", "Jumps the playhead to the beginning."),
                      ("Bars / Time", "Switches the ruler between bars and beats and minutes and seconds."),
                      ("Tool (arrow)", "Selects the editing tool. See the dropdown below."),
                      ("Snap (magnet)", "Clip edges snap to the grid when on."),
                      ("1/4 (grid division)", "How fine the snap grid is. See the dropdown below."),
                      ("Loop", "Turns the loop region on or off. Shift-drag on the ruler to set the region."),
                      ("Flag (M)", "Drops a marker at the playhead."),
                      ("Row height", "Resets any resized tracks back to the default height."),
                      ("Render", "Renders the timeline to a video file (or a WAV when it holds only audio)."),
                      ("Audio", "Lets the timeline drive audio as well as picture."),
                      ("Pencil", "Opens or closes the Inspector."),
                  ]),
                  row((fig("tl_tool_dropdown", "Tool dropdown: Select (A), Range Selection (R), Trim (T), Pencil / Draw Clip (P), Cut (B), Hand (H), Zoom (Z).", box=(230, 60, 620, 270)), 1),
                      (fig("tl_grid_dropdown", "Snap grid dropdown: Off, 1 bar, 1/2, 1/4, 1/8, 1/16, each with a triplet (T).", box=(230, 60, 620, 330)), 1))))

    b.append(page("<h2>Tracks, clips and the inspector</h2>",
                  "<h3>Adding tracks</h3><p>Click <b>+ Add Track</b> and choose Video Track or Audio Track. Audio tracks also carry <b>S</b> (solo), <b>M</b> (mute), a pan knob and a level knob; "
                  "video tracks carry an opacity slider.</p>",
                  fig("tl_add_track_menu", "+ Add Track offers Video Track and Audio Track.", box=(340, 140, 700, 310), width="60%"),
                  "<h3>Adding a clip</h3><p>Right-click an empty spot on a track and choose <b>Add Clip</b>. The menu shows the bar and beat it will land on, snapped to the grid. "
                  "A clip with no node yet is shown hatched.</p>",
                  fig("tl_lane_menu", "Right-click a lane: Add Clip at the clicked bar and beat.", box=(340, 140, 900, 270), width="70%"),
                  "<h3>Assigning a clip to a node</h3><p>Three ways: right-click a node on the canvas and choose <b>Add to Timeline</b> (creates an assigned clip at the playhead), "
                  "choose <b>Assign Node...</b> in the clip's right-click menu, or use <b>Assign Different Node...</b> in the inspector. "
                  "An assigned clip takes the node's name and a thumbnail, and the viewport monitor shows its output as the playhead crosses it.",
                  fig("tl_clip_selected", "The Stage clip selected (outlined); the monitor on the left shows the timeline output.", box=(0, 60, 1290, 560), width="72%")))

    b.append(page("<h3>Right-clicking a clip</h3>",
                  row((fig("tl_clip_menu", "The clip menu.", box=(500, 130, 1000, 520)), 1),
                      (table(["Item", "What it does"], [
                          ("Rename (Cmd+R)", "Names the clip."),
                          ("Active", "Tick to play the clip; untick to mute it without deleting (it turns hatched)."),
                          ("Fade In / Fade Out", "Fade length in milliseconds."),
                          ("Opacity", "Clip brightness against what is beneath it."),
                          ("Compositing", "Blend mode used to combine this clip with lower tracks."),
                          ("Color Grade", "A submenu of colour-grade adjustments for the clip."),
                          ("Color Tint", "Colour of the clip block on the timeline."),
                          ("Assign Node...", "Pick the node this clip plays."),
                          ("Group (Cmd+G) / Ungroup (Cmd+Shift+G)", "Treat several clips as one block you can move together."),
                          ("Delete (Backspace)", "Removes the clip."),
                      ]), 1)),
                  "<h3>The Inspector</h3><p>The pencil icon opens a side panel for whatever is selected. Select a clip and it shows the clip; select a track and it shows the track.</p>",
                  row((fig("tl_clip_selected", "Video Clip: name, Active, Timing & Position, Compositing & Video, Color Tint, Source Node.", box=(1290, 100, 1545, 860)), 1),
                      (fig("tl_track_selected", "Video Track: name, Active, opacity, Track Tint, Duplicate, Delete.", box=(1290, 100, 1545, 520)), 1),
                      (table(["Inspector field", "What it does"], [
                      ("Name / Active", "Rename and enable or disable the clip or track."),
                      ("Start / Length", "Exact position and duration in bars.beats.ticks. Type a value for sample-accurate placement."),
                      ("Fade In / Fade Out", "Edge fades in milliseconds."),
                      ("Blend / Opacity / Bright / Contrast / Sat", "How the clip composites over tracks beneath it."),
                      ("Color Tint", "The swatches colour the clip block."),
                      ("Source Node", "The node and node type the clip plays."),
                      ("Assign Different Node... / Clear Source", "Swap the node or make the clip unassigned again."),
                  ]), 3))))

    b.append(page("<h3>Selecting several clips and grouping</h3>",
                  "<p>To move or colour several clips at once, press <b>R</b> for the Range Selection tool and drag a box over them, then press <b>A</b> to return to Select. "
                  "Clips grouped with Group (Cmd+G) move as one.</p>",
                  "<h3>Grouping tracks</h3><p>Right-click a track name. <b>Convert to Group</b> wraps the track in a collapsible folder so a whole stack of tracks can be folded away. "
                  "The same menu adds and renames tracks, deletes them, and offers <b>Render Track</b> to export just that track.</p>",
                  row((fig("tl_track_menu", "Track menu.", box=(340, 130, 640, 330)), 1),
                      (fig("tl_track_group", "After Convert to Group: Group 2 (1) holds Stage.", box=(340, 100, 1290, 260)), 2)),
                  "<h3>Rendering</h3><p><b>Render</b> opens the Render Timeline popup: the span (whole arrangement, loop, markers or a custom range), the video size and rate, "
                  "and the output name, folder and format.</p>",
                  fig("tl_render", "Render Timeline popup.", box=(1250, 70, 1600, 520), width="45%")))

    b.append(page("<h2>History panel</h2>",
                  "<p>Every edit you make is listed, newest at the top. Click any line to jump the patch back (or forward) to that moment; edits after it are kept until you make a new change.</p>",
                  row((fig("panel_history_big", "History before the first edit.", box=(1250, 40, 1600, 1000)), 1),
                      (table(["Entry", "Meaning"], [
                          ("Start", "The patch as it was loaded."),
                          ("Bypass", "A node was bypassed or enabled."),
                          ("Edit 2 settings", "Two or more parameters changed together."),
                          ("Edit timeline", "A change in the arrangement."),
                      ]), 1))))

    b.append(page("<h2>Right-click menus and the Add Node palette</h2>",
                  "<p>Right-click does different things depending on what is under the cursor.</p>",
                  row((fig("rc_node", "On an audio node: bypass, duplicate, add to Timeline or Session, help, delete. Image nodes add viewport and window items.", box=(300, 300, 620, 520)), 1),
                      (table(["Node menu item", "What it does"], [
                          ("Bypass Node (B)", "Passes the input straight through."),
                          ("Duplicate (Cmd+D)", "Copies the node and its settings."),
                          ("Hide params", "Hides the node's parameter controls so it takes less room on the canvas."),
                          ("Open in viewport panel", "Adds a live card to the Viewport panel."),
                          ("Add to Timeline", "Creates an assigned clip in the Arrangement."),
                          ("Add to Session", "Puts the node in a Session slot."),
                          ("Open in new window", "Pops the node's output into its own window."),
                          ("Reference in chat", "Opens the chat with the node named in the message."),
                          ("Help", "Opens this node's help."),
                          ("Delete Node (Backspace)", "Removes it and its cables."),
                      ]), 1)),
                  "<p>Right-click empty canvas, or press Shift+N, to open the <b>Add node</b> palette. Type to search, or walk the categories "
                  "(source, compositing, effects, 3d, notes, synths, audio effects, modulators, prediction, macros, utility). "
                  "The arrow means a submenu.</p>",
                  row((fig("rc_canvas_palette", "The Add node palette.", box=(400, 430, 720, 790)), 1),
                      (fig("rc_palette_submenu", "Hover a category to see its nodes: compositing shown.", box=(880, 500, 1360, 1000)), 1))))

    b.extend((page("<h2>Settings</h2>",
                  "<p>The gear at the bottom of the rail (or Cmd+0) opens Settings. It has nine pages.</p>",
                  row((fig("settings_appearance", "Appearance", box=(410, 200, 1190, 805)), 1), (fig("settings_canvas", "Canvas & Workspace", box=(410, 200, 1190, 805)), 1)),
                  row((fig("settings_audio", "Audio", box=(410, 200, 1190, 805)), 1), (fig("settings_general", "General & Performance", box=(410, 200, 1190, 805)), 1)),
                  row((fig("settings_field", "Field Language", box=(410, 200, 1190, 805)), 1), (fig("settings_globals", "Expression Globals", box=(410, 200, 1190, 805)), 1))),
             page(row((fig("settings_extensions", "Extensions: install extension packs.", box=(410, 200, 1190, 805)), 1), (fig("settings_plugins", "Plugins: VST3 folders and every scanned plugin.", box=(410, 200, 1190, 805)), 1)),
                  row((fig("settings_terminal", "Terminal: Command line &amp; agents (one-click setup, coding agent), the panel and the infinite command.", box=(410, 200, 1190, 805)), 1), ("", 1)))))

    b.append(page("<h2>Settings: Command line &amp; agents</h2>",
                  "<p>The Terminal page of Settings has one section, <b>Agents</b>: a row per coding agent (Claude Code, Codex, Antigravity) with its status and the button that moves it on, "
                  "the model each answers with, what each has used, the folders and commands agents may use, and the saved chats. The Chat chapter below explains every row.</p>"
                  "<p>Setting up an agent opens a Terminal tab and runs a short script. It puts the <code>infinite</code> command on your PATH for every terminal, checks the headless engine, makes sure Node.js is present, "
                  "fetches the Infinite MCP server and registers it with the agent. Each step checks first and is skipped when it is already done, so the button doubles as a repair. Password prompts (Homebrew, sudo) appear in the tab. "
                  "Infinite never asks for or stores an API key: the agent's own login runs in the browser. Nothing is installed until you press a button.</p>"
                  "<p>Setup is one time. It survives quitting, restarting and updating Infinite: the <code>infinite</code> command is a small launcher that finds the current Infinite, and the agent's registration points at that launcher. "
                  "Each time Infinite starts it quietly checks the steps. If everything holds, nothing shows. If something broke (Infinite moved, the agent was uninstalled, the line was removed from your shell profile), "
                  "a <b>Connection</b> row appears naming the step, with a <b>Repair</b> button that reruns only that step.</p>"))

    b.append(page("<h2>Chat</h2>",
                  "<p>The Chat panel is how you talk to Infinite. Say what you want and the patch changes while you watch. "
                  "It is the Terminal panel's chat face: the Terminal opens to it, and the Terminal button in its toolbar switches to a plain shell and back. "
                  "Short commands (add, connect, remove, bypass, undo, modulate, set) run at once inside Infinite with no agent. Anything else goes to the agent you picked, "
                  "which changes the open patch through the <code>infinite</code> command, so every change is listed on a card and can be undone.</p>",
                  "<h3>Set up an agent</h3>"
                  "<p>Infinite talks to three coding agents you install yourself: <b>Claude Code</b>, <b>Codex</b> and <b>Antigravity</b>. It never asks for or stores an API key; each agent signs in with its own account. "
                  "The first time the Terminal opens, a welcome asks which agent to connect and has one <b>Connect</b> button; <b>Skip, just a terminal</b> leaves the chat for later. "
                  "Settings, Terminal, Agents has one row per agent with its status and the one button that moves it on:</p>",
                  table(["Status", "Button", "What it means"], [
                      ("Not installed", "Set up", "The agent's program is missing. Set up opens a Terminal tab and types the vendor's own installer (Claude Code and Antigravity: the vendor's install script; Codex: <code>npm install -g @openai/codex</code>)."),
                      ("Needs setup", "Set up", "Installed, but the <code>infinite</code> command, the MCP link or the skills are not connected yet. Set up runs the one-click connection in a Terminal tab."),
                      ("Setting up… 2 of 5", "", "The setup script is running; passwords (Homebrew, sudo) are asked for in its tab."),
                      ("Setup did not finish", "Try again", "A setup step failed; the tab shows which. Fix it there, or run the step by hand, then try again."),
                      ("Not signed in", "Sign in", "Opens the agent's own sign-in. The page opens in your browser; the row reads <i>Waiting for sign-in…</i> with a Cancel button. A sign-in that is not finished in 3 minutes ends with <i>Sign-in timed out</i>."),
                      ("Signed out", "Sign in again", "The agent said its sign-in expired in the middle of a chat. Sign in again and ask again."),
                      ("Ready", "Sign out", "Installed, connected and signed in. Sign out is shown when the agent has a sign-out command."),
                  ]),
                  "<h3>When something fails</h3>",
                  table(["You see", "What to do"], [
                      ("<i>Finish in your browser…</i> and no page opened", "The sign-in address is shown in the row; open it by hand. If the page ends with a code (Antigravity), paste it into the field that appears and press Submit."),
                      ("<i>Sign in to Claude Code to continue.</i>", "Press Sign in in Settings, Terminal, Agents (or the chat's agent row), then ask again."),
                      ("<i>hit its usage limit</i>", "The agent's own plan is used up. Wait, or pick another connected agent in the model picker."),
                      ("<i>Could not reach the agent</i>", "No internet, or a proxy blocks it. Check the connection and press Try again."),
                      ("<i>needs its terms accepted once</i>", "Open the Terminal, start the agent and answer its first screen."),
                      ("<i>is not installed. Set it up again.</i>", "The program was removed. Press Set up."),
                      ("<i>It stopped responding, so the chat ended the turn</i>", "The agent went quiet for too long. Ask again; the earlier changes stay on their cards."),
                      ("Setup keeps failing", "Fallback: open the Terminal, install the agent by hand (the commands above), then press Connect again. Each setup step checks first, so rerunning only repairs what is broken."),
                  ]),
                  "<h3>The message box</h3>",
                  table(["Type", "What it does"], [
                      ("@", "Opens the node picker: type to filter the nodes in the open patch, Up/Down to move, Return or Tab to take one, Esc to close. <code>@cube</code> names a node (<code>@cube#2</code> when there are two), <code>@cube.detail</code> one of its controls. The agent receives its exact type, index and values."),
                      ("@claude, @codex, @antigravity", "Name an agent to have that agent answer this one message."),
                      ("/", "Opens the command list: type to filter, Return to run."),
                      ("Return / Shift+Return", "Return sends; Shift+Return starts a new line (the box grows to six lines, then scrolls). While an agent is working, Return adds the line to the queue; the Stop button ends the turn."),
                  ]),
                  table(["Command", "What it does"], [
                      ("/help", "What the chat can do, with a button per command."),
                      ("/shortcuts, /manuals, /languages, /about", "Answered by Infinite at once: the shortcut list, the three manuals, the built-in code languages, what Infinite is."),
                      ("/website, /github", "Open the website or the repository."),
                      ("/model <name>", "Switch the model, for example <code>/model opus</code>."),
                      ("/new", "Start a new chat."),
                      ("/undo", "Undo the last change."),
                      ("/explain [topic]", "The agent explains the open patch in plain words."),
                      ("/check", "Validates the patch and its code, one line per problem, with a button to fix what it can."),
                      ("/docs <question>", "Answers from the manuals, found with <code>infinite docs</code>, not from memory."),
                  ]),
                  "<h3>One model picker</h3>"
                  "<p>There is one picker, not an agent picker and a model picker. It lists every connected agent's models together, each under its agent's name. Choose a model and that agent answers from then on; "
                  "the avatar in front of each reply shows who spoke. An agent that is not ready does not appear until you set it up. "
                  "<code>/model</code> and Settings, Terminal, <i>Claude Code model</i> / <i>Codex model</i> / <i>Antigravity model</i> set the same thing. "
                  "Each agent's list comes from the agent itself, so a new model shows up when the agent offers it; <i>its default model</i> leaves the choice to the agent.</p>"
                  "<h3>Files, pictures and paste</h3>"
                  "<p>Press <b>+</b> (Attach files) or drop files on the panel: audio, video, images, GIFs, 3D models, MIDI, patches and code. Cmd/Ctrl+V pastes copied files or a copied picture; text on the clipboard still pastes as text. "
                  "A pasted picture is saved in the chats folder so the agent can read it. A file still being written is retried until it is complete.</p>"
                  "<p>Pictures show as thumbnails in your message and in replies; click one to open it <b>full size</b> over the app, fitted to the window. A click or Esc closes it. "
                  "Videos play in a card and stop when scrolled off screen. GIFs animate; a GIF over 16 MB shows its first frame only. Audio shows a player with a waveform, a folder button (Show in folder) and a waveform button.</p>"
                  "<h3>Change cards</h3>"
                  "<p>After each turn the chat lists what the agent did: <b>Looked at the patch</b> for a read-only turn, or <b>Made 3 changes</b> with one line per change (<i>old → new</i>). "
                  "The card shows only this turn's changes, never the whole history. Node names and parameter names on it are links: click a node to select it and bring it into view, click a parameter to jump to that control. "
                  "A name whose node is gone shows as plain text.</p>"
                  "<p>Parameters the agent changed appear as <b>live knobs</b> on the card, only the ones that matter for the change. Turn a knob and the patch follows as you play; the agent's value stays as a dot on the track, and a double-click puts the knob back on it. "
                  "<b>Undo</b> on the card reverts the whole turn; a changed line that has been undone reads <i>Undone</i>.</p>"
                  "<p>An audio file the agent made shows as an audio card. <b>Open in a Sampler</b> (the waveform button) puts that file into a new Sampler node on the canvas, ready to play. "
                  "<b>Show in folder</b> reveals the file.</p>"
                  "<h3>Permissions</h3>"
                  "<p>By default agents may only run <code>infinite</code> commands and read the files you attach. When one tries something else, a note <i>Not allowed: …</i> appears with an <b>Allow</b> button: "
                  "<i>Allow folder</i> adds a folder, <i>Allow &lt;command&gt;</i> adds a command, for good. Settings, Terminal, Agents lists them:</p>",
                  table(["Row", "What it does"], [
                      ("Agent folder", "Where agents start, and the folder they may read. <i>The open patch's folder</i> by default; Choose… picks another."),
                      ("More folders", "Extra folders agents may read. Add folder… adds one, Remove takes it back."),
                      ("Edit files in them", "Off (default): agents run <code>infinite</code> and read attached files only. On: they may also read and change files in the folders above."),
                      ("Allowed commands", "The commands agents may run besides <code>infinite</code>, added by Allow in the chat. Remove takes one back."),
                      ("Claude Code / Codex / Antigravity model", "The model that agent answers with; the chat's picker sets the same value."),
                      ("Claude Code / Codex / Antigravity usage", "Turns, tokens, the share of cached input and the cost when the agent reports one. Cached input is re-read on every step of a turn and costs about a tenth of fresh input. <b>Reset</b> clears the counts for every agent."),
                      ("Connection", "Only when a step of the setup broke (the command, the MCP link, the skills): names the step; Repair reruns it."),
                      ("Saved chats", "How many chats are kept on this computer. Clear all deletes them after a confirmation."),
                  ]),
                  "<h3>Usage and tokens</h3>"
                  "<p>Under an agent's name on each reply, a faint <b>count of tokens</b> shows what that turn used (input, cached input and output together); hover it for the cached share. "
                  "Chats are kept locally; the <b>Chats</b> button in the toolbar lists, searches and deletes them, and <b>New chat</b> starts a fresh one.</p>"
                  "<h3>Floating windows</h3>"
                  "<p>Chat, the Viewport, the Modulation Matrix and the Performance panel can leave the dock and float. Right-click the panel's toolbar (or use its dock menu) and choose <b>Floating Window</b> after Bottom, Right, Left and Top. "
                  "The panel opens in its own window over the stage that you move and resize like Settings or Shortcuts; its X closes it. The chat's toolbar also has <b>Pop out</b>, which becomes <b>Dock</b> while it floats. "
                  "Choosing any dock position puts the panel back.</p>"))

    b.append(page("<h2>Sound panels: Session, Sample Studio, Piano Roll</h2>",
                  "<p>Three more strips dock along the bottom. Each has its own toolbar; right-click the toolbar for its dock position.</p>",
                  fig("panel_session", "Session: tracks across, scenes down. Click a clip to launch it, click a scene name to launch the row. The playing scene is outlined.", box=(0, 700, 1600, 1000)),
                  fig("panel_studio", "Sample Studio: a take cut into eight slices. Toolbar: open, play, input, Bars/Time/Samples, tools, Slice, Generate, Export, Convert. The Sample card on the right sets pitch, speed and fades.", box=(0, 680, 1600, 1000)),
                  fig("panel_pianoroll", "Piano Roll: pages of notes over the keyboard, with record input, quantise, Fx and Generate in the toolbar.", box=(0, 750, 1600, 1000))))

    b.append(page("<h2>3D View</h2>",
                  "<p>The 3D View shows the scene the 3D nodes build. The toolbar holds add, select, move, rotate and scale, the pivot and orientation choices and the view toggles. "
                  "Drag the axis gizmo at the top right to orbit; the four buttons at the bottom right zoom, pan, fly the camera and lock it. "
                  "The Scene list at the right lists every object; the eye hides one.</p>",
                  fig("panel_view3d", "3D View docked at the bottom with the Scene list open.", box=(0, 610, 1600, 1000))))

    b.append(page("<h2>Windows and dialogs</h2>",
                  row((fig("window_shortcuts", "All Shortcuts (Menu, All shortcuts): filter box, then every shortcut by group.", box=(440, 240, 1130, 750)), 1),
                      (fig("window_help", "Help and module reference: getting started, controls, then every node.", box=(430, 175, 1160, 800)), 1)),
                  row((fig("window_templates", "Template library (File, Template library): opens a copy; the original never changes.", box=(410, 205, 1160, 770)), 1),
                      (fig("dialog_about", "About Infinite: version, build, credits, licence links.", box=(560, 230, 1040, 740)), 1))))

    b.append(page("<h2>Take your own screenshots</h2>",
                  "<p>Every picture in this guide was made by script, so you can regenerate them after an update or make your own for a tutorial.</p>",
                  table(["Tool", "What it does"], [
                      ("scripts/screenshot_all_nodes.py", "Renders every node type through the app's RPC <code>screenshot_node</code> and saves a 2x PNG for each into docs/node_screenshots/."),
                      ("scripts/node_screenshot.py", "Spawns one node by type name and saves a cropped screenshot of it. Useful for tutorials and bug reports."),
                      ("tools/manual/ui_shots_offscreen.py", "Runs the app off-screen and drives it over RPC (panels, synthetic clicks and keys) to capture the window, menus, panels, settings and dialogs in this chapter into docs/ui_screenshots/. Works with the screen locked."),
                  ]),
                  "<pre><code>python3 scripts/screenshot_all_nodes.py --out docs/node_screenshots\n"
                  "python3 scripts/node_screenshot.py \"Wavetable\" --out /tmp/shots\n"
                  "python3 tools/manual/build.py        # rebuild this guide with the fresh pictures</code></pre>",
                  "<p>Screenshots are captured at 2x, show real values and are never recoloured. The control token for the RPC is read from the app's own settings folder, so nothing leaves your machine.</p>"))
    return "".join(b)
