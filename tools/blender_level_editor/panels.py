import bpy

from . import stage_io

_FLAG_NAMES = {
    "Name": "has_name",
    "Tag": "has_tag",
    "Transform": "has_transform",
    "Camera": "has_camera",
    "DirectionalLight": "has_directional_light",
    "Sky": "has_sky",
    "Model": "has_model",
}


def draw_stage(layout, context):
    props = context.scene.donut_scene

    column = layout.column(align=True)
    column.prop(props, "stage_name")
    if props.stage_path:
        column.label(text=props.stage_path, icon="FILE")
    column.prop(props, "import_meshes_on_load")

    row = layout.row(align=True)
    row.operator("donut.load_stage", text="Load", icon="FILEBROWSER")
    row.operator("donut.save_stage", text="Save", icon="FILE_TICK")

    layout.operator("donut.validate_stage", text="Validate", icon="CHECKMARK")

    layout.separator()

    row = layout.row(align=True)
    row.operator("donut.add_prim", text="Add", icon="ADD")
    row.operator("donut.duplicate_prim", text="Duplicate", icon="DUPLICATE")
    row.operator("donut.remove_prim", text="Remove", icon="REMOVE")

    layout.operator("donut.preview_in_engine", text="Preview in Engine", icon="PLAY")

    obj = context.active_object
    if obj is not None and obj.donut_prim.is_prim and obj.donut_prim.has_model:
        row = layout.row(align=True)
        row.operator("donut.create_model_proxy", text="Proxy", icon="MESH_CUBE")
        row.operator("donut.import_model_meshes", text="Meshes", icon="MESH_DATA")
        row.operator("donut.import_model_textures", text="Textures", icon="TEXTURE")
        layout.operator("donut.import_model_preview", text="Import Full Model (original)", icon="IMPORT")

    layout.separator()

    count = sum(1 for item in context.scene.objects if item.donut_prim.is_prim)
    layout.label(text="Prims: %d" % count)


def draw_entity(layout, context):
    obj = context.active_object

    if obj is None or not obj.donut_prim.is_prim:
        layout.label(text="Select a prim", icon="INFO")
        return

    prim = obj.donut_prim
    if prim.is_template:
        layout.label(text="Archtype template", icon="FILE_VOLUME")
    layout.prop(prim, "prim_name")
    layout.prop(prim, "archtype")

    for name in stage_io.ATTRIBUTE_ORDER:
        _draw_attribute(layout, obj, prim, name)

    extras = (prim.extra_json or "").strip()
    if extras and extras != "{}":
        box = layout.box()
        box.label(text="Extra Attributes", icon="PRESET")
        box.label(text=extras[:200])


def _draw_attribute(layout, obj, prim, name):
    flag = _FLAG_NAMES[name]
    box = layout.box()
    box.prop(prim, flag)

    if not getattr(prim, flag):
        return

    if name == "Tag":
        box.prop(prim, "tag_value")
    elif name == "Transform":
        if obj.type in ("CAMERA", "LIGHT"):
            column = box.column(align=True)
            column.prop(prim.transform, "translation")
            column.prop(prim.transform, "rotation")
            column.prop(prim.transform, "scaling")
        else:
            column = box.column(align=True)
            column.prop(obj, "location")
            column.prop(obj, "rotation_euler")
            column.prop(obj, "scale")
    elif name == "Camera":
        column = box.column(align=True)
        if obj.type == "CAMERA":
            column.prop(obj.data, "angle")
            column.prop(obj.data, "clip_start")
            column.prop(obj.data, "clip_end")
        else:
            column.prop(prim.camera, "fov_degrees")
            column.prop(prim.camera, "z_near")
            column.prop(prim.camera, "z_far")
        column.prop(prim.camera, "target_distance")
    elif name == "DirectionalLight":
        column = box.column(align=True)
        if obj.type == "LIGHT":
            column.prop(obj.data, "color")
            column.prop(obj.data, "energy")
            column.prop(obj.data, "angle")
        else:
            column.prop(prim.light, "color")
            column.prop(prim.light, "irradiance")
            column.prop(prim.light, "angular_size")
        column.prop(prim.light, "direction_length")
    elif name == "Sky":
        column = box.column(align=True)
        column.prop(prim.sky, "sky_color")
        column.prop(prim.sky, "horizon_color")
        column.prop(prim.sky, "ground_color")
        column.prop(prim.sky, "brightness")
        column.prop(prim.sky, "horizon_size")
        column.prop(prim.sky, "glow_size")
        column.prop(prim.sky, "glow_intensity")
        column.prop(prim.sky, "glow_sharpness")
    elif name == "Model":
        box.prop(prim.model, "path")


class DONUT_PT_stage(bpy.types.Panel):
    bl_idname = "DONUT_PT_stage"
    bl_label = "Stage"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Level Editor"

    def draw(self, context):
        draw_stage(self.layout, context)


class DONUT_PT_prim_attributes(bpy.types.Panel):
    bl_idname = "DONUT_PT_prim_attributes"
    bl_label = "Entity Attributes"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Level Editor"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        draw_entity(self.layout, context)


class DONUT_PT_archtype(bpy.types.Panel):
    bl_idname = "DONUT_PT_archtype"
    bl_label = "Archetypes"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Level Editor"
    bl_options = {"DEFAULT_CLOSED"}

    def draw(self, context):
        layout = self.layout

        row = layout.row(align=True)
        row.operator("donut.load_archtype", text="Load", icon="FILEBROWSER")
        row.operator("donut.save_archtype", text="Save", icon="FILE_TICK")

        layout.operator("donut.add_prim_from_archtype", text="Instance in Stage", icon="ADD")

        obj = context.active_object
        if obj is not None and obj.donut_prim.is_template:
            layout.label(text="Template: %s" % obj.donut_prim.prim_name, icon="OUTLINER_OB_EMPTY")
            layout.prop(obj.donut_prim, "archtype", text="Source")
        else:
            layout.label(text="Load an archtype to edit it", icon="INFO")


class DONUT_PT_stage_properties(bpy.types.Panel):
    bl_idname = "DONUT_PT_stage_properties"
    bl_label = "Stage"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "level_editor"

    def draw(self, context):
        draw_stage(self.layout, context)


class DONUT_PT_entity_properties(bpy.types.Panel):
    bl_idname = "DONUT_PT_entity_properties"
    bl_label = "Entity Attributes"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "level_editor"

    def draw(self, context):
        draw_entity(self.layout, context)


_CLASSES = (
    DONUT_PT_stage,
    DONUT_PT_prim_attributes,
    DONUT_PT_archtype,
    DONUT_PT_stage_properties,
    DONUT_PT_entity_properties,
)


def register():
    for cls in _CLASSES:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(_CLASSES):
        bpy.utils.unregister_class(cls)
