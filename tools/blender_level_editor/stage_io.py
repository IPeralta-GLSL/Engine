import json
import math
import os

ATTRIBUTE_ORDER = ("Name", "Tag", "Transform", "Camera", "DirectionalLight", "Sky", "Model")

ATTRIBUTE_DEFAULTS = {
    "Name": {"value": ""},
    "Tag": {"value": ""},
    "Transform": {
        "translation": [0.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scaling": [1.0, 1.0, 1.0],
    },
    "Camera": {
        "position": [0.0, 1.8, 0.0],
        "target": [1.0, 1.8, 0.0],
        "up": [0.0, 1.0, 0.0],
        "fovDegrees": 45.0,
        "zNear": 0.1,
        "zFar": 1000.0,
    },
    "DirectionalLight": {
        "direction": [0.1, -1.0, -0.15],
        "color": [1.0, 1.0, 1.0],
        "irradiance": 4.0,
        "angularSize": 0.53,
    },
    "Sky": {
        "skyColor": [0.17, 0.37, 0.65],
        "horizonColor": [0.5, 0.7, 0.92],
        "groundColor": [0.62, 0.59, 0.55],
        "brightness": 0.12,
        "horizonSize": 30.0,
        "glowSize": 5.0,
        "glowIntensity": 0.1,
        "glowSharpness": 4.0,
    },
    "Model": {"path": ""},
}

VECTOR_SIZES = {
    "Transform": {"translation": 3, "rotation": 4, "scaling": 3},
    "Camera": {"position": 3, "target": 3, "up": 3},
    "DirectionalLight": {"direction": 3, "color": 3},
    "Sky": {"skyColor": 3, "horizonColor": 3, "groundColor": 3},
}


def default_attribute(name):
    return json.loads(json.dumps(ATTRIBUTE_DEFAULTS[name]))


def read_json(path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path, document):
    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
    text = json.dumps(document, indent="\t", ensure_ascii=False)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)
        handle.write("\n")


def resolve_path(base_dir, value):
    if not value:
        return value
    if os.path.isabs(value):
        return value
    return os.path.normpath(os.path.join(base_dir, value))


def resolve_model_path(model_path, base_dir):
    if not model_path:
        return ""
    if os.path.isabs(model_path):
        return model_path if os.path.isfile(model_path) else ""

    directories = []
    current = os.path.abspath(base_dir or os.getcwd())
    while True:
        directories.append(current)
        parent = os.path.dirname(current)
        if parent == current:
            break
        current = parent

    directories.append(os.getcwd())

    for root in directories:
        candidate = os.path.normpath(os.path.join(root, model_path))
        if os.path.isfile(candidate):
            return candidate

    return ""


def load_archtype(path):
    document = read_json(path)
    if not isinstance(document, dict):
        raise ValueError("archtype root is not an object: %s" % path)
    attributes = document.get("attributes")
    if not isinstance(attributes, dict):
        attributes = {}
    return {"name": document.get("name", "UntitledArchtype"), "attributes": attributes}


def load_stage(path):
    document = read_json(path)
    if not isinstance(document, dict):
        raise ValueError("stage root is not an object: %s" % path)

    base_dir = os.path.dirname(os.path.abspath(path))
    prims = []

    for node in document.get("prims", []):
        if not isinstance(node, dict):
            continue

        archtype_ref = node.get("archtype")
        if not isinstance(archtype_ref, str) or not archtype_ref:
            archtype_ref = None

        attributes = {}

        if archtype_ref:
            archtype_path = resolve_path(base_dir, archtype_ref)
            if os.path.isfile(archtype_path):
                attributes.update(load_archtype(archtype_path)["attributes"])

        node_attributes = node.get("attributes")
        if isinstance(node_attributes, dict):
            attributes.update(node_attributes)

        prims.append({"archtype": archtype_ref, "attributes": attributes})

    return {
        "name": document.get("name", "UntitledStage"),
        "path": os.path.abspath(path),
        "base_dir": base_dir,
        "prims": prims,
    }


def values_equal(a, b, tolerance=1e-6):
    if isinstance(a, bool) or isinstance(b, bool):
        return a is b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(float(a), float(b), rel_tol=tolerance, abs_tol=tolerance)
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return False
        return all(values_equal(x, y, tolerance) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        if set(a.keys()) != set(b.keys()):
            return False
        return all(values_equal(a[key], b[key], tolerance) for key in a)
    return a == b


def compute_overrides(attributes, base_attributes):
    overrides = {}
    for name, value in attributes.items():
        if name in base_attributes and values_equal(base_attributes[name], value):
            continue
        overrides[name] = value
    return overrides


def ordered_attributes(attributes):
    ordered = {}
    for name in ATTRIBUTE_ORDER:
        if name in attributes:
            ordered[name] = attributes[name]
    for name in attributes:
        if name not in ordered:
            ordered[name] = attributes[name]
    return ordered


def _absolute_archtype_path(archtype_ref, base_dir, source_base_dir):
    if os.path.isabs(archtype_ref):
        return archtype_ref
    root = source_base_dir if source_base_dir else base_dir
    return os.path.normpath(os.path.join(root, archtype_ref))


def _relative_archtype_path(archtype_ref, base_dir, source_base_dir):
    if os.path.isabs(archtype_ref):
        return archtype_ref.replace(os.sep, "/")
    absolute = _absolute_archtype_path(archtype_ref, base_dir, source_base_dir)
    return os.path.relpath(absolute, base_dir).replace(os.sep, "/")


def _absolute_archtype_path(archtype_ref, base_dir):
    if os.path.isabs(archtype_ref):
        return archtype_ref
    return os.path.normpath(os.path.join(base_dir, archtype_ref))


def rebase_archtype(archtype_ref, source_base_dir, target_base_dir):
    if os.path.isabs(archtype_ref):
        return archtype_ref.replace(os.sep, "/")
    absolute = os.path.normpath(os.path.join(source_base_dir, archtype_ref))
    return os.path.relpath(absolute, target_base_dir).replace(os.sep, "/")


def build_document(stage_name, prims, base_dir):
    prim_nodes = []

    for prim in prims:
        attributes = prim.get("attributes", {})
        archtype_ref = prim.get("archtype")
        node = {}

        if archtype_ref:
            node["archtype"] = archtype_ref.replace(os.sep, "/")
            base_attributes = {}
            archtype_path = _absolute_archtype_path(archtype_ref, base_dir)
            if os.path.isfile(archtype_path):
                base_attributes = load_archtype(archtype_path)["attributes"]
            overrides = compute_overrides(attributes, base_attributes)
            if overrides:
                node["attributes"] = ordered_attributes(overrides)
        else:
            node["attributes"] = ordered_attributes(attributes)

        prim_nodes.append(node)

    return {"name": stage_name, "prims": prim_nodes}


def save_stage(path, stage_name, prims):
    base_dir = os.path.dirname(os.path.abspath(path))
    document = build_document(stage_name, prims, base_dir)
    write_json(path, document)
    return document


def save_archtype(path, name, attributes):
    document = {"name": name, "attributes": ordered_attributes(attributes)}
    write_json(path, document)
    return document


def validate_document(document, base_dir):
    errors = []
    warnings = []
    names = {}

    if not isinstance(document, dict):
        return ["document is not an object"], warnings

    if not isinstance(document.get("name"), str):
        errors.append("stage name is missing or not a string")

    prims = document.get("prims")
    if not isinstance(prims, list):
        return ["prims is missing or not an array"], warnings

    for index, node in enumerate(prims):
        if not isinstance(node, dict):
            errors.append("prim %d is not an object" % index)
            continue

        archtype_ref = node.get("archtype")
        if archtype_ref is not None:
            if not isinstance(archtype_ref, str) or not archtype_ref:
                errors.append("prim %d has an invalid archtype path" % index)
            elif not os.path.isfile(resolve_path(base_dir, archtype_ref)):
                warnings.append("prim %d archtype not found: %s" % (index, archtype_ref))

        attributes = node.get("attributes", {})
        if attributes is None:
            attributes = {}
        if not isinstance(attributes, dict):
            errors.append("prim %d attributes is not an object" % index)
            continue

        if not attributes and archtype_ref is None:
            warnings.append("prim %d has no attributes" % index)

        name_value = None

        for name, value in attributes.items():
            if not isinstance(value, dict):
                errors.append("prim %d attribute %s is not an object" % (index, name))
                continue

            if name in ("Name", "Tag"):
                field = value.get("value", "")
                if not isinstance(field, str):
                    errors.append("prim %d attribute %s.value is not a string" % (index, name))
                elif name == "Name":
                    name_value = field
            elif name == "Model":
                if not isinstance(value.get("path", ""), str):
                    errors.append("prim %d attribute Model.path is not a string" % index)
            elif name in VECTOR_SIZES:
                for key, size in VECTOR_SIZES[name].items():
                    vector = value.get(key)
                    if not isinstance(vector, list) or len(vector) != size:
                        errors.append("prim %d attribute %s.%s must be %d numbers" % (index, name, key, size))

        if name_value:
            names[name_value] = names.get(name_value, 0) + 1

    for name, count in names.items():
        if count > 1:
            warnings.append("duplicate prim name: %s (%d)" % (name, count))

    return errors, warnings
