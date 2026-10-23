# Keystone

A fast native 3D viewer for IFC building models, for the Omarchy desktop.

![Keystone showing the Schependomlaan model with a wall selected](docs/screenshot.png)

Open an `.ifc` file, orbit around the building, and click any element to see
what it is: its class, type, storey, materials, area, volume, and every
property set and quantity the file gives it. IFC2x3, IFC4, and IFC4x3 all
work.

Keystone follows Omarchy: the viewport sits on your theme's background, the
selection takes the theme's accent, corners follow Hyprland's rounding, and
the window speaks the language picked under Setup > Language.

## Shortcuts

| Key | Action |
| --- | --- |
| Ctrl+O | Open a model (or drop an `.ifc` file on the window) |
| Click | Select an element |
| Drag | Orbit |
| Right drag, or Shift+drag | Pan |
| Wheel, or pinch | Zoom towards the pointer |
| Double-click | Select and frame an element |
| Space | Frame the selection, or the whole model |
| Home | Reset the view |
| 1 2 3 4 | Front, side, back, and top views |
| ← → ↑ ↓ | Orbit |
| + − | Zoom in and out |
| I | Show or hide properties |
| Esc | Clear the selection |
| ? | Show or hide shortcuts |
| Q | Quit |

## How it works

[IfcOpenShell](https://ifcopenshell.org)'s `IfcConvert` triangulates the
model into a Wavefront OBJ with one group per element, named by its GlobalId.
Keystone runs it in the background, reads the OBJ, and draws the model with
Qt Quick 3D, one draw call per material. Openings, spaces, and spatial zones
are left out, as they are volumes rather than things.

Properties come from the IFC file itself: Keystone indexes the STEP text once
and reads an element's attributes, relationships, property sets, quantities,
and units when you select it. (IfcConvert's own XML export crashes on some
IFC2x3 files, which is most of what's out there.) When a file carries no area
or volume for an element, Keystone measures them from its geometry and says so.

Triangulating is most of the wait on a real building (about 15 seconds for
the 49 MB Schependomlaan model on four cores), so the converted geometry is
kept in `~/.cache/keystone/models`, keyed by the file's path, size, and time,
for the twelve most recently opened models. Opening one again takes about
half a second. Your `.ifc` files are only ever read.

## Building

Keystone is C++17 and Qt Quick, built with qmake6 and make into one binary.

```sh
bin/fetch-ifcconvert   # IfcConvert 0.9.0 into vendor/, once
bin/build              # build/keystone
bin/test               # the Qt Test suite, offscreen
bin/install            # makepkg -fsi: installs it so it shows up in the launcher
```

`build/keystone model.ifc` opens a model straight away. Set
`KEYSTONE_IFCCONVERT` to use a different IfcConvert.

The package bundles IfcConvert under `/usr/lib/keystone/ifcconvert` and
registers `application/x-ifc` for `.ifc` files.

## License

MIT for Keystone; see [LICENSE](LICENSE). The bundled IfcConvert is
IfcOpenShell's, under the LGPL 3.0 or later.
