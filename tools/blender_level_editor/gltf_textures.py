import json
import os
import tempfile

import bpy


def prepare_textured(source_path, max_size):
    with open(source_path, "r", encoding="utf-8") as handle:
        document = json.load(handle)

    source_dir = os.path.dirname(os.path.abspath(source_path))
    temp_dir = tempfile.mkdtemp(prefix="donut_tex_")

    for index, image in enumerate(document.get("images", [])):
        uri = image.get("uri")
        if not uri or "bufferView" in image:
            continue
        source_image = os.path.normpath(os.path.join(source_dir, uri))
        if not os.path.isfile(source_image):
            continue

        destination = os.path.join(temp_dir, "tex_%d.png" % index)
        datablock = bpy.data.images.load(source_image, check_existing=False)
        try:
            width, height = datablock.size
            if max_size and max(width, height) > max_size:
                factor = max_size / float(max(width, height))
                datablock.scale(max(1, int(width * factor)), max(1, int(height * factor)))
            datablock.filepath_raw = destination
            datablock.file_format = "PNG"
            datablock.save()
        finally:
            bpy.data.images.remove(datablock)

        image["uri"] = destination

    handle = tempfile.NamedTemporaryFile(
        prefix=".tex_", suffix=".gltf", dir=source_dir, delete=False, mode="w", encoding="utf-8")
    try:
        json.dump(document, handle)
    finally:
        handle.close()

    return handle.name, temp_dir
