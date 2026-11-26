import json
import os


def _identity():
    return [[1.0 if row == col else 0.0 for col in range(4)] for row in range(4)]


def _node_matrix(node):
    if "matrix" in node:
        values = node["matrix"]
        return [
            [values[0], values[4], values[8], values[12]],
            [values[1], values[5], values[9], values[13]],
            [values[2], values[6], values[10], values[14]],
            [values[3], values[7], values[11], values[15]],
        ]

    translation = node.get("translation", [0.0, 0.0, 0.0])
    rotation = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    scale = node.get("scale", [1.0, 1.0, 1.0])
    x, y, z, w = rotation

    rotation_matrix = [
        [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)],
        [2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
        [2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)],
    ]

    matrix = [[0.0] * 4 for _ in range(4)]
    for row in range(3):
        for col in range(3):
            matrix[row][col] = rotation_matrix[row][col] * scale[col]
        matrix[row][3] = translation[row]
    matrix[3][3] = 1.0
    return matrix


def _multiply(a, b):
    return [
        [sum(a[row][k] * b[k][col] for k in range(4)) for col in range(4)]
        for row in range(4)
    ]


def _transform_point(matrix, point):
    x, y, z = point
    return [
        matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z + matrix[0][3],
        matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z + matrix[1][3],
        matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z + matrix[2][3],
    ]


def read_bounds(path):
    with open(path, "r", encoding="utf-8") as handle:
        document = json.load(handle)

    accessors = document.get("accessors", [])
    meshes = document.get("meshes", [])
    nodes = document.get("nodes", [])
    scenes = document.get("scenes", [])
    scene_index = document.get("scene", 0)

    if not scenes:
        return None

    roots = scenes[scene_index].get("nodes", [])
    lower = [float("inf")] * 3
    upper = [float("-inf")] * 3

    def visit(index, parent_matrix):
        if not isinstance(index, int) or not (0 <= index < len(nodes)):
            return
        node = nodes[index]
        world = _multiply(parent_matrix, _node_matrix(node))

        mesh_index = node.get("mesh")
        if isinstance(mesh_index, int) and 0 <= mesh_index < len(meshes):
            for primitive in meshes[mesh_index].get("primitives", []):
                position = primitive.get("attributes", {}).get("POSITION")
                if not isinstance(position, int) or not (0 <= position < len(accessors)):
                    continue
                accessor = accessors[position]
                minimum = accessor.get("min")
                maximum = accessor.get("max")
                if not (isinstance(minimum, list) and isinstance(maximum, list) and len(minimum) == 3 and len(maximum) == 3):
                    continue
                for corner in (
                    (minimum[0], minimum[1], minimum[2]),
                    (minimum[0], minimum[1], maximum[2]),
                    (minimum[0], maximum[1], minimum[2]),
                    (minimum[0], maximum[1], maximum[2]),
                    (maximum[0], minimum[1], minimum[2]),
                    (maximum[0], minimum[1], maximum[2]),
                    (maximum[0], maximum[1], minimum[2]),
                    (maximum[0], maximum[1], maximum[2]),
                ):
                    point = _transform_point(world, corner)
                    for axis in range(3):
                        lower[axis] = min(lower[axis], point[axis])
                        upper[axis] = max(upper[axis], point[axis])

        for child in node.get("children", []):
            visit(child, world)

    for root in roots:
        visit(root, _identity())

    if lower[0] == float("inf"):
        return None

    return lower, upper


def file_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return 0
