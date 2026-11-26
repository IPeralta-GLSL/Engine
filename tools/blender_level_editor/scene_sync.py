import json
import math
import os

import bpy
from mathutils import Matrix, Quaternion, Vector

from . import gltf_strip, stage_io

STAGE_COLLECTION = "Donut Stage"
ARCHTYPE_COLLECTION = "Donut Archtype"

DEFAULT_ROTATION_LENGTH = 1.0

_AXIS_TO_BLENDER = Matrix(((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0)))
_AXIS_TO_ENGINE = _AXIS_TO_BLENDER.transposed()
_QUAT_TO_BLENDER = _AXIS_TO_BLENDER.to_quaternion()
_QUAT_TO_ENGINE = _QUAT_TO_BLENDER.inverted()


def _round(value):
    value = float(value)
    if abs(value) < 5e-7:
        return 0.0
    return round(value, 6)


def _round_vector(values):
    return [_round(value) for value in values]


def _permute_scale(values):
    return [values[0], values[2], values[1]]


def _to_blender_vector(values):
    return _AXIS_TO_BLENDER @ Vector(values)


def _to_engine_vector(values):
    return _AXIS_TO_ENGINE @ Vector(values)


def _to_blender_quaternion(values):
    rotation = Quaternion((values[3], values[0], values[1], values[2]))
    return _QUAT_TO_BLENDER @ rotation @ _QUAT_TO_ENGINE


def _to_engine_quaternion(rotation):
    converted = _QUAT_TO_ENGINE @ rotation @ _QUAT_TO_BLENDER
    if converted.w < 0.0:
        converted = Quaternion((-converted.w, -converted.x, -converted.y, -converted.z))
    return converted


def _camera_quaternion(position, target, up):
    position = Vector(position)
    forward = Vector(target) - position
    if forward.length < 1e-8:
        forward = Vector((1.0, 0.0, 0.0))
    forward.normalize()

    up_vector = Vector(up)
    right = forward.cross(up_vector)
    if right.length < 1e-8:
        right = forward.orthogonal()
    right.normalize()
    up_vector = right.cross(forward)
    up_vector.normalize()

    matrix = Matrix((
        (right.x, up_vector.x, -forward.x),
        (right.y, up_vector.y, -forward.y),
        (right.z, up_vector.z, -forward.z),
    ))
    return matrix.to_quaternion()


def _light_quaternion(direction):
    vector = Vector(direction)
    if vector.length < 1e-8:
        vector = Vector((0.0, -1.0, 0.0))
    vector.normalize()
    return Vector((0.0, 0.0, -1.0)).rotation_difference(vector)


def _active_scene(scene=None):
    return scene if scene is not None else bpy.context.scene


def _stage_collection(scene):
    collection = bpy.data.collections.get(STAGE_COLLECTION)
    if collection is None:
        collection = bpy.data.collections.new(STAGE_COLLECTION)
    if collection.name not in scene.collection.children:
        scene.collection.children.link(collection)
    return collection


def _scene_prims(scene):
    return [obj for obj in scene.objects if obj.donut_prim.is_prim and not obj.donut_prim.is_template]


def _archtype_collection(scene):
    collection = bpy.data.collections.get(ARCHTYPE_COLLECTION)
    if collection is None:
        collection = bpy.data.collections.new(ARCHTYPE_COLLECTION)
    if collection.name not in scene.collection.children:
        scene.collection.children.link(collection)
    return collection


def _sorted_prims(scene=None):
    prims = _scene_prims(_active_scene(scene))
    return sorted(prims, key=lambda obj: (obj.donut_prim.stage_index, obj.name))


def clear_stage(scene=None):
    scene = _active_scene(scene)

    for obj in _scene_prims(scene):
        prim = obj.donut_prim
        if prim.model_collection:
            collection = bpy.data.collections.get(prim.model_collection)
            if collection is not None:
                for child in list(collection.objects):
                    bpy.data.objects.remove(child, do_unlink=True)
                bpy.data.collections.remove(collection)
        bpy.data.objects.remove(obj, do_unlink=True)


def _apply_transform(obj, transform):
    if not isinstance(transform, dict):
        return
    obj.location = _to_blender_vector(transform.get("translation", (0.0, 0.0, 0.0)))
    rotation = transform.get("rotation", (0.0, 0.0, 0.0, 1.0))
    if isinstance(rotation, (list, tuple)) and len(rotation) == 4:
        obj.rotation_quaternion = _to_blender_quaternion(rotation)
    obj.scale = _permute_scale(transform.get("scaling", (1.0, 1.0, 1.0)))


def _create_prim_object(index, default_name, attributes, collection):
    name_attribute = attributes.get("Name")
    prim_name = name_attribute.get("value", "") if isinstance(name_attribute, dict) else ""
    display_name = prim_name or default_name or ("Prim_%d" % index)

    if "Camera" in attributes:
        camera = attributes["Camera"] if isinstance(attributes["Camera"], dict) else {}
        data = bpy.data.cameras.new(display_name)
        data.sensor_fit = "VERTICAL"
        data.angle = math.radians(float(camera.get("fovDegrees", 45.0)))
        data.clip_start = float(camera.get("zNear", 0.1))
        data.clip_end = float(camera.get("zFar", 1000.0))
        obj = bpy.data.objects.new(display_name, data)
        obj.rotation_mode = "QUATERNION"
        position = _to_blender_vector(camera.get("position", (0.0, 1.8, 0.0)))
        target = _to_blender_vector(camera.get("target", (1.0, 1.8, 0.0)))
        up = _to_blender_vector(camera.get("up", (0.0, 1.0, 0.0)))
        obj.location = position
        obj.rotation_quaternion = _camera_quaternion(position, target, up)
    elif "DirectionalLight" in attributes:
        light = attributes["DirectionalLight"] if isinstance(attributes["DirectionalLight"], dict) else {}
        data = bpy.data.lights.new(display_name, "SUN")
        data.color = light.get("color", (1.0, 1.0, 1.0))
        data.energy = float(light.get("irradiance", 4.0))
        data.angle = math.radians(float(light.get("angularSize", 0.53)))
        obj = bpy.data.objects.new(display_name, data)
        obj.rotation_mode = "QUATERNION"
        direction = _to_blender_vector(light.get("direction", (0.1, -1.0, -0.15)))
        obj.rotation_quaternion = _light_quaternion(direction)
    else:
        obj = bpy.data.objects.new(display_name, None)
        obj.empty_display_type = "PLAIN_AXES"
        obj.empty_display_size = 0.5
        obj.rotation_mode = "QUATERNION"
        _apply_transform(obj, attributes.get("Transform"))

    collection.objects.link(obj)
    _apply_attributes(obj, index, prim_name, attributes)
    return obj


def _apply_attributes(obj, index, prim_name, attributes):
    prim = obj.donut_prim
    prim.is_prim = True
    prim.stage_index = index
    prim.prim_name = prim_name

    prim.has_name = isinstance(attributes.get("Name"), dict)
    prim.has_tag = isinstance(attributes.get("Tag"), dict)
    tag_attribute = attributes.get("Tag")
    prim.tag_value = tag_attribute.get("value", "") if isinstance(tag_attribute, dict) else ""

    transform = attributes.get("Transform")
    prim.has_transform = isinstance(transform, dict)
    if prim.has_transform:
        prim.transform.translation = transform.get("translation", (0.0, 0.0, 0.0))
        prim.transform.rotation = transform.get("rotation", (0.0, 0.0, 0.0, 1.0))
        prim.transform.scaling = transform.get("scaling", (1.0, 1.0, 1.0))

    camera = attributes.get("Camera")
    prim.has_camera = isinstance(camera, dict)
    if prim.has_camera:
        position = camera.get("position", (0.0, 1.8, 0.0))
        target = camera.get("target", (1.0, 1.8, 0.0))
        prim.camera.fov_degrees = float(camera.get("fovDegrees", 45.0))
        prim.camera.z_near = float(camera.get("zNear", 0.1))
        prim.camera.z_far = float(camera.get("zFar", 1000.0))
        distance = (Vector(target) - Vector(position)).length
        prim.camera.target_distance = distance if distance > 1e-4 else 10.0

    light = attributes.get("DirectionalLight")
    prim.has_directional_light = isinstance(light, dict)
    if prim.has_directional_light:
        direction = light.get("direction", (0.1, -1.0, -0.15))
        length = Vector(direction).length
        prim.light.direction_length = length if length > 1e-4 else DEFAULT_ROTATION_LENGTH
        prim.light.color = light.get("color", (1.0, 1.0, 1.0))
        prim.light.irradiance = float(light.get("irradiance", 4.0))
        prim.light.angular_size = float(light.get("angularSize", 0.53))

    sky = attributes.get("Sky")
    prim.has_sky = isinstance(sky, dict)
    if prim.has_sky:
        prim.sky.sky_color = sky.get("skyColor", (0.17, 0.37, 0.65))
        prim.sky.horizon_color = sky.get("horizonColor", (0.5, 0.7, 0.92))
        prim.sky.ground_color = sky.get("groundColor", (0.62, 0.59, 0.55))
        prim.sky.brightness = float(sky.get("brightness", 0.12))
        prim.sky.horizon_size = float(sky.get("horizonSize", 30.0))
        prim.sky.glow_size = float(sky.get("glowSize", 5.0))
        prim.sky.glow_intensity = float(sky.get("glowIntensity", 0.1))
        prim.sky.glow_sharpness = float(sky.get("glowSharpness", 4.0))

    model = attributes.get("Model")
    prim.has_model = isinstance(model, dict)
    if prim.has_model:
        prim.model.path = model.get("path", "")

    extras = {name: value for name, value in attributes.items() if name not in stage_io.ATTRIBUTE_ORDER}
    prim.extra_json = json.dumps(extras)

    prim.archtype = ""


def import_model_geometry(obj):
    base_dir = bpy.context.scene.donut_scene.stage_dir or os.getcwd()
    resolved = stage_io.resolve_model_path(obj.donut_prim.model.path, base_dir)
    if not resolved:
        return 0
    temp_path = None
    try:
        temp_path = gltf_strip.write_geometry_only(resolved)
        return import_model_preview(obj, temp_path)
    finally:
        if temp_path and os.path.exists(temp_path):
            os.remove(temp_path)


def import_stage(filepath):
    data = stage_io.load_stage(filepath)
    scene = bpy.context.scene

    clear_stage(scene)

    collection = _stage_collection(scene)
    objects = []

    for index, prim in enumerate(data["prims"]):
        obj = _create_prim_object(index, "Prim_%d" % index, prim["attributes"], collection)
        obj.donut_prim.archtype = prim["archtype"] or ""
        objects.append(obj)

    scene_props = scene.donut_scene
    scene_props.stage_name = data["name"]
    scene_props.stage_path = data["path"]
    scene_props.stage_dir = data["base_dir"]

    if scene_props.import_meshes_on_load:
        for obj in objects:
            if obj.donut_prim.has_model:
                try:
                    import_model_geometry(obj)
                except Exception:
                    pass

    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    if objects:
        objects[0].select_set(True)
        bpy.context.view_layer.objects.active = objects[0]

    return len(objects)


def add_prim(prim_type, name):
    scene = bpy.context.scene
    collection = _stage_collection(scene)
    index = max((obj.donut_prim.stage_index for obj in _scene_prims(scene)), default=-1) + 1

    attributes = {"Name": {"value": name}}

    if prim_type in ("EMPTY", "MODEL", "TAG"):
        attributes["Transform"] = stage_io.default_attribute("Transform")
    if prim_type == "MODEL":
        attributes["Model"] = stage_io.default_attribute("Model")
    elif prim_type == "TAG":
        attributes["Tag"] = stage_io.default_attribute("Tag")
    elif prim_type == "CAMERA":
        attributes["Camera"] = stage_io.default_attribute("Camera")
    elif prim_type == "SUN":
        attributes["DirectionalLight"] = stage_io.default_attribute("DirectionalLight")
    elif prim_type == "SKY":
        attributes["Sky"] = stage_io.default_attribute("Sky")

    obj = _create_prim_object(index, name, attributes, collection)

    for other in bpy.context.selected_objects:
        other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    return obj


def _select_object(obj):
    for other in bpy.context.selected_objects:
        other.select_set(False)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def load_archtype(filepath):
    data = stage_io.load_archtype(filepath)
    scene = bpy.context.scene
    collection = _archtype_collection(scene)
    absolute = os.path.abspath(filepath)

    for obj in list(collection.objects):
        if obj.donut_prim.is_template and obj.donut_prim.prim_name == data["name"]:
            bpy.data.objects.remove(obj, do_unlink=True)

    obj = _create_prim_object(0, data["name"], data["attributes"], collection)
    obj.donut_prim.is_template = True
    obj.donut_prim.archtype = absolute
    obj.donut_prim.prim_name = data["name"]

    scene.donut_scene.archtype_path = absolute
    _select_object(obj)
    return obj


def save_archtype(filepath):
    obj = bpy.context.active_object
    if obj is None or not obj.donut_prim.is_template:
        raise ValueError("Select an archtype template first")

    attributes = _attributes_from_object(obj)
    name = obj.donut_prim.prim_name or "UntitledArchtype"
    stage_io.save_archtype(filepath, name, attributes)

    absolute = os.path.abspath(filepath)
    obj.donut_prim.archtype = absolute
    bpy.context.scene.donut_scene.archtype_path = absolute
    return name


def add_prim_from_archtype(filepath):
    data = stage_io.load_archtype(filepath)
    scene = bpy.context.scene
    collection = _stage_collection(scene)
    index = max((obj.donut_prim.stage_index for obj in _scene_prims(scene)), default=-1) + 1

    absolute = os.path.abspath(filepath)
    stage_dir = scene.donut_scene.stage_dir
    if stage_dir:
        reference = os.path.relpath(absolute, stage_dir).replace(os.sep, "/")
    else:
        reference = absolute

    obj = _create_prim_object(index, data["name"], data["attributes"], collection)
    obj.donut_prim.archtype = reference
    _select_object(obj)
    return obj


def _attributes_from_object(obj):
    prim = obj.donut_prim
    attributes = {}

    try:
        extras = json.loads(prim.extra_json or "{}")
    except ValueError:
        extras = {}
    if not isinstance(extras, dict):
        extras = {}
    attributes.update(extras)

    if prim.has_name:
        attributes["Name"] = {"value": prim.prim_name}
    if prim.has_tag:
        attributes["Tag"] = {"value": prim.tag_value}

    matrix = obj.matrix_basis

    if prim.has_transform:
        if obj.type in ("CAMERA", "LIGHT"):
            attributes["Transform"] = {
                "translation": _round_vector(prim.transform.translation),
                "rotation": [_round(value) for value in prim.transform.rotation],
                "scaling": _round_vector(prim.transform.scaling),
            }
        else:
            location, rotation, scale = matrix.decompose()
            rotation = _to_engine_quaternion(rotation)
            attributes["Transform"] = {
                "translation": _round_vector(_to_engine_vector(location)),
                "rotation": [_round(rotation.x), _round(rotation.y), _round(rotation.z), _round(rotation.w)],
                "scaling": _round_vector(_permute_scale(scale)),
            }

    if prim.has_camera:
        position = matrix.translation.copy()
        forward = -(matrix.col[2].to_3d())
        if forward.length < 1e-8:
            forward = Vector((1.0, 0.0, 0.0))
        forward.normalize()
        distance = max(float(prim.camera.target_distance), 1e-4)
        target = position + forward * distance
        up = matrix.col[1].to_3d()
        if up.length < 1e-8:
            up = Vector((0.0, 1.0, 0.0))
        up.normalize()

        camera_data = obj.data if obj.type == "CAMERA" else None
        fov = math.degrees(camera_data.angle) if camera_data else prim.camera.fov_degrees
        z_near = camera_data.clip_start if camera_data else prim.camera.z_near
        z_far = camera_data.clip_end if camera_data else prim.camera.z_far

        attributes["Camera"] = {
            "position": _round_vector(_to_engine_vector(position)),
            "target": _round_vector(_to_engine_vector(target)),
            "up": _round_vector(_to_engine_vector(up)),
            "fovDegrees": _round(fov),
            "zNear": _round(z_near),
            "zFar": _round(z_far),
        }

    if prim.has_directional_light:
        direction = -(matrix.col[2].to_3d())
        if direction.length < 1e-8:
            direction = Vector((0.1, -1.0, -0.15))
        direction.normalize()
        length = max(float(prim.light.direction_length), 1e-4)
        direction = direction * length

        light_data = obj.data if obj.type == "LIGHT" else None
        color = tuple(light_data.color) if light_data else tuple(prim.light.color)
        irradiance = light_data.energy if light_data else prim.light.irradiance
        angular_size = math.degrees(light_data.angle) if light_data else prim.light.angular_size

        attributes["DirectionalLight"] = {
            "direction": _round_vector(_to_engine_vector(direction)),
            "color": _round_vector(color),
            "irradiance": _round(irradiance),
            "angularSize": _round(angular_size),
        }

    if prim.has_sky:
        attributes["Sky"] = {
            "skyColor": _round_vector(prim.sky.sky_color),
            "horizonColor": _round_vector(prim.sky.horizon_color),
            "groundColor": _round_vector(prim.sky.ground_color),
            "brightness": _round(prim.sky.brightness),
            "horizonSize": _round(prim.sky.horizon_size),
            "glowSize": _round(prim.sky.glow_size),
            "glowIntensity": _round(prim.sky.glow_intensity),
            "glowSharpness": _round(prim.sky.glow_sharpness),
        }

    if prim.has_model:
        attributes["Model"] = {"path": prim.model.path}

    return attributes


def build_stage_document():
    scene = bpy.context.scene
    scene_props = scene.donut_scene
    base_dir = scene_props.stage_dir or os.getcwd()
    prims = []

    for obj in _sorted_prims(scene):
        prims.append({"archtype": obj.donut_prim.archtype or None, "attributes": _attributes_from_object(obj)})

    name = scene_props.stage_name or "UntitledStage"
    return stage_io.build_document(name, prims, base_dir), base_dir


def export_stage(filepath):
    scene = bpy.context.scene
    scene_props = scene.donut_scene
    output_dir = os.path.dirname(os.path.abspath(filepath))
    source_dir = scene_props.stage_dir or output_dir
    prims = []

    for obj in _sorted_prims(scene):
        prim = obj.donut_prim
        archtype_ref = prim.archtype or None
        if archtype_ref:
            archtype_ref = stage_io.rebase_archtype(archtype_ref, source_dir, output_dir)
            prim.archtype = archtype_ref
        prims.append({"archtype": archtype_ref, "attributes": _attributes_from_object(obj)})

    name = scene_props.stage_name or "UntitledStage"
    stage_io.save_stage(filepath, name, prims)

    scene_props.stage_path = filepath
    scene_props.stage_dir = output_dir

    return len(prims)


def _preview_collection(obj):
    collection_name = ("MDL_" + obj.name)[:60]
    collection = bpy.data.collections.get(collection_name)
    if collection is None:
        collection = bpy.data.collections.new(collection_name)
    if collection.name not in bpy.context.scene.collection.children:
        bpy.context.scene.collection.children.link(collection)
    return collection


def _clear_preview_collection(obj):
    collection_name = obj.donut_prim.model_collection
    if not collection_name:
        return
    collection = bpy.data.collections.get(collection_name)
    if collection is None:
        return
    for child in list(collection.objects):
        bpy.data.objects.remove(child, do_unlink=True)
    bpy.data.collections.remove(collection)
    obj.donut_prim.model_collection = ""


def _box_mesh(name, lower, upper):
    x0, y0, z0 = lower
    x1, y1, z1 = upper
    vertices = [
        (x0, y0, z0),
        (x1, y0, z0),
        (x1, y1, z0),
        (x0, y1, z0),
        (x0, y0, z1),
        (x1, y0, z1),
        (x1, y1, z1),
        (x0, y1, z1),
    ]
    faces = [
        (0, 1, 2, 3),
        (4, 7, 6, 5),
        (0, 4, 5, 1),
        (1, 5, 6, 2),
        (2, 6, 7, 3),
        (3, 7, 4, 0),
    ]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    return mesh


def create_model_proxy(obj, lower, upper):
    _clear_preview_collection(obj)
    x0 = float(lower[0])
    x1 = float(upper[0])
    y0 = -float(upper[2])
    y1 = -float(lower[2])
    z0 = float(lower[1])
    z1 = float(upper[1])
    collection = _preview_collection(obj)
    name = ("MDL_" + obj.name)[:60]
    proxy = bpy.data.objects.new(name, _box_mesh(name, (x0, y0, z0), (x1, y1, z1)))
    collection.objects.link(proxy)
    proxy.parent = obj
    proxy.matrix_parent_inverse = Matrix.Identity(4)
    obj.donut_prim.model_collection = collection.name
    return proxy


def import_model_preview(obj, filepath):
    _clear_preview_collection(obj)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=filepath)
    imported = [item for item in bpy.data.objects if item not in before]

    if not imported:
        return 0

    collection = _preview_collection(obj)

    for item in imported:
        for owner in list(item.users_collection):
            owner.objects.unlink(item)
        collection.objects.link(item)
        item.parent = obj
        item.matrix_parent_inverse = obj.matrix_world.inverted()

    obj.donut_prim.model_collection = collection.name
    return len(imported)
