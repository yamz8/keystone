---
name: keystone
description: |
  Read IFC/BIM building models with Keystone: what's in a model, which
  elements it has, and any element's type, storey, materials, quantities,
  and property sets. Use for ANY question about an .ifc file, such as how many
  walls, what a door is made of, the area of a slab, or what's on a storey,
  and to open a model in the 3D viewer for the user.
---

# Keystone

Keystone is a 3D viewer for IFC building models (IFC2x3, IFC4, IFC4x3). Its
commands read a model without a window and never change the file.

## Commands

- `keystone info <model.ifc> --json`: schema, storeys with element counts,
  and how many elements of each IFC class. Start here.
- `keystone elements <model.ifc> --json [--class IfcWall] [--storey "00 ground floor"]`:
  every element with its GlobalId, class, name, and storey. `--class` matches
  subclasses by prefix, so `IfcWall` includes `IfcWallStandardCase`.
- `keystone properties <model.ifc> <GlobalId> --json`: the element's class,
  name, type, storey, materials (with layer thicknesses), area, volume, tag,
  and every property set and quantity, with units.
- `keystone open <model.ifc>`: open the viewer for the user to look at.

Each command exits 0 on success, or 1 with the reason on stderr.

## Workflow

1. `keystone info model.ifc --json` to see what the model holds.
2. `keystone elements model.ifc --json --class IfcDoor` to find the elements
   that matter and their GlobalIds.
3. `keystone properties model.ifc <GlobalId> --json` for each one you need.

Values come formatted with their units, as the file states them ("250 mm",
"25.75 m²"). Quantities are the file's own; a model without them has none to
report here, though the viewer measures area and volume from the geometry.
