from . import stage_io, gltf_bounds, gltf_strip, gltf_textures, props, scene_sync, operators, panels

_MODULES = (props, operators, panels)


def register():
    for module in _MODULES:
        module.register()


def unregister():
    for module in reversed(_MODULES):
        module.unregister()
