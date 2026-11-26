import json
import os
import tempfile


def write_geometry_only(source_path):
    with open(source_path, "r", encoding="utf-8") as handle:
        document = json.load(handle)

    for key in ("images", "textures", "samplers", "materials", "extensions", "extensionsUsed", "extensionsRequired"):
        document.pop(key, None)

    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitive.pop("material", None)
            primitive.pop("extensions", None)

    for node in document.get("nodes", []):
        node.pop("extensions", None)

    directory = os.path.dirname(os.path.abspath(source_path))
    handle = tempfile.NamedTemporaryFile(
        prefix=".geom_", suffix=".gltf", dir=directory, delete=False, mode="w", encoding="utf-8")
    try:
        json.dump(document, handle)
    finally:
        handle.close()
    return handle.name
