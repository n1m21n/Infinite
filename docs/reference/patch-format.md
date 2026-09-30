# Patch file format (version 1)

A patch is a line-based UTF-8 text file (`.inf`). The GUI writes it; you can also write it by hand
and run it headless (`Infinite --render`, `--frame`, `--validate`, `--describe`, `--explain`,
`--canonicalize`). The authoritative record layout is `src/core/Patch.h`; this page is the guide
to writing files.

## Files and line rules

- First line: `infinite-patch 1`.
- One record per line. Free-form text (type names, strings, expressions, labels) is always **last**
  on its line and runs to the end of the line.
- Blank lines and lines starting with `#` are ignored.
- CRLF line ends, a UTF-8 BOM, and trailing spaces on non-text values are accepted.
- An unknown tag, key or a removed node type is skipped with a warning; it never fails the load
  (strict mode, the default for the CLI, turns those warnings into errors; `--lenient` keeps them
  warnings).

## Nodes

```
node <index> <category> <type name to end of line>
  id <word>                      optional authoring name
  pos <x> <y>
  flags <showParams> <bypassed> <showMiniViewport> <showAdvancedParams>
  f <key> <value>                float
  i <key> <value>                int, or a dropdown option name (i shapeType Star)
  b <key> <0|1>                  bool
  c <key> <r> <g> <b>            colour
  s <key> <text to end of line>  string
end
```

- `index` is the node's number; other records refer to it.
- **Defaults rule:** any parameter you leave out keeps the node's default. A hand-written node needs
  only the keys it changes. The GUI writes every key; `--canonicalize` does too.
- Find category, type name, keys, ranges and dropdown options with `Infinite --describe [type]`.
- An `id` is a word: starts with a letter or `_`, then letters, digits and `_`. Ids are unique per file.

## Wires

```
cable <dstIndex> <dstSlot> <srcIndex>      image
geo   <dstIndex> <dstSlot> <srcIndex>      geometry, camera, light
aud   <dstIndex> <dstSlot> <srcIndex>      audio
note  <dstIndex> <dstSlot> <srcIndex>      notes
mod   <dstIndex> <dstParam> <srcIndex> <srcOutput> <polarity> <depth> <centre> [<lo> <hi> [<enabled>]]
pal   <dstIndex> <dstColor> <srcIndex> <srcSwatch>
expr  <dstIndex> <dstParam> <expression to end of line>
glob  <name> <expression to end of line>
```

Everything else in `Patch.h` (arrangement, performance matrix, gestures, transport) is written by the
GUI and read back the same way; hand-written files rarely need it.

## Names instead of numbers

Wherever a node index goes you may write the node's `id`. Wherever a slot goes you may write its
slot name (the input's label, lower case, non-alphanumerics collapsed to `_`, e.g. `input`, `input_2`;
`--describe` lists them per type). A control in `mod`/`expr` may be written by its saved key
(`mod shape sizeX lfo 0 0 1 0.5`) instead of the widget order number. `Resolve(named) == numeric`:
a named file loads to exactly the graph its numeric twin does, and `--canonicalize` writes the numeric
form (dropping `id` lines unless `--keep-ids`).

Errors are specific and exit 3: `E_BAD_ID`, `E_DUPLICATE_ID`, `E_BAD_REF` (unknown node or slot name),
`E_BAD_KEY` and `E_BAD_VALUE` (unknown control key or dropdown option), each with the nearest valid
name as a hint.

## Minimal patches

Image (`tests/headless/format/image.inf`):

```
infinite-patch 1
node 1 Source Shape
  id shape
  f sizeX 0.6
  i shapeType Star
end
node 2 Utility Output
  id out
end
cable out 0 shape
```

Audio (`tests/headless/format/audio.inf`): notes into a synth into Audio Out, plus an Output so the
patch renders. Modulation (`tests/headless/format/modulation.inf`): an LFO on `sizeX` and an
expression on `rotation`. These three files are run through `--validate` (strict) by
`scripts/headless_smoke.sh`, so they cannot rot.

## Checking a file

| Command | Answers |
|---|---|
| `--validate f.inf` | does it load, strictly; every problem with its line |
| `--explain f.inf [--json] [--all]` | what the loaded graph actually is: nodes, non-default params, every wire, resolved modulation range |
| `--frame f.inf 0 out.png` | one picture |
| `--canonicalize in out` | the numeric form the GUI would write |

`--explain` prints the graph that was built, not the file, then the status JSON on the last line.
For `tests/headless/format/modulation.inf`:

```
Nodes (3)
  Shape "shape" (1)
    sizeX 0.942168   (live value, driven by mod)
    rotation 54.8533   (live value, driven by expr)
  LFO "lfo" (2)
  Output "out" (3)
Relations (3)
  cable Output "out" (3) slot 0 [in] <- Shape "shape" (1)
  mod Shape "shape" (1) sizeX <- LFO "lfo" (2) out 0, absolute, depth 1, range 0.01..1
  expr Shape "shape" (1) rotation = sin(t) * 90
Unconnected: none
```

- A node lists only the keys that differ from the type's defaults (`--all` lists every key and marks
  the rest `(default)`). A dropdown shows its option name, then the number: `shapeType Star (6)`.
- A key that a `mod` or `expr` line writes is marked `live value`: it holds whatever the binding wrote
  last, so it changes from run to run and is not something you authored.
- Every relation line starts with the record tag that made it, so the number of `cable` lines equals
  the number of `cable` records that loaded. A wire you wrote that is missing here did not load.
- `--json` prints the same graph as one JSON line (`nodes[].params[]` with `default`, `option`,
  `driven`; `relations[]` with `kind` and `text`; `unconnected[]`) before the status line.

## Stability promise

Version 1 lines never change meaning. New fields are only ever added at the **end** of a line, and a
reader that does not know them ignores them. A key or tag that disappears is skipped with a warning
rather than failing the load. Anything that would change how an existing line reads gets a new tag or
a new version number, never a reinterpretation.
