import os
import subprocess

import bpy
from bpy.app.handlers import persistent

from . import gltf_bounds, scene_sync, stage_io

LARGE_FILE_BYTES = 64 * 1024 * 1024
WORKSPACE_BLEND = os.path.join(os.path.dirname(os.path.abspath(__file__)), "level_editor.blend")


def _default_stage_path(scene):
    props = scene.donut_scene
    if props.stage_path:
        return props.stage_path
    directory = props.stage_dir or os.getcwd()
    return os.path.join(directory, (props.stage_name or "UntitledStage") + ".stage")


class DONUT_OT_load_stage(bpy.types.Operator):
    bl_idname = "donut.load_stage"
    bl_label = "Load Stage"
    bl_options = {"REGISTER", "UNDO"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.stage", options={"HIDDEN"})

    def invoke(self, context, event):
        self.filepath = _default_stage_path(context.scene)
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        try:
            count = scene_sync.import_stage(self.filepath)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        try:
            ensure_level_editor_workspace(activate=True)
        except Exception:
            pass
        self.report({"INFO"}, "Loaded %d prims from %s" % (count, self.filepath))
        return {"FINISHED"}


class DONUT_OT_save_stage(bpy.types.Operator):
    bl_idname = "donut.save_stage"
    bl_label = "Save Stage"
    bl_options = {"REGISTER", "UNDO"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.stage", options={"HIDDEN"})

    def invoke(self, context, event):
        self.filepath = _default_stage_path(context.scene)
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        try:
            count = scene_sync.export_stage(self.filepath)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, "Saved %d prims to %s" % (count, self.filepath))
        return {"FINISHED"}


class DONUT_OT_add_prim(bpy.types.Operator):
    bl_idname = "donut.add_prim"
    bl_label = "Add Prim"
    bl_options = {"REGISTER", "UNDO"}

    prim_type: bpy.props.EnumProperty(
        name="Type",
        items=(
            ("EMPTY", "Empty", "Empty prim with a transform"),
            ("CAMERA", "Camera", "Camera prim"),
            ("SUN", "Directional Light", "Directional light prim"),
            ("SKY", "Sky", "Sky prim"),
            ("MODEL", "Model", "Model reference prim"),
            ("NAME", "Named", "Prim with only a name"),
            ("TAG", "Tagged", "Prim with a tag"),
        ),
        default="EMPTY",
    )
    prim_name: bpy.props.StringProperty(name="Name", default="Prim")

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        try:
            scene_sync.add_prim(self.prim_type, self.prim_name)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        return {"FINISHED"}


class DONUT_OT_remove_prim(bpy.types.Operator):
    bl_idname = "donut.remove_prim"
    bl_label = "Remove Prim"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        obj = context.active_object
        if obj is None or not obj.donut_prim.is_prim:
            self.report({"WARNING"}, "Select a prim first")
            return {"CANCELLED"}
        bpy.data.objects.remove(obj, do_unlink=True)
        return {"FINISHED"}


class DONUT_OT_validate_stage(bpy.types.Operator):
    bl_idname = "donut.validate_stage"
    bl_label = "Validate Stage"
    bl_options = {"REGISTER"}

    def execute(self, context):
        try:
            document, base_dir = scene_sync.build_stage_document()
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        errors, warnings = stage_io.validate_document(document, base_dir)

        for message in warnings[:10]:
            self.report({"WARNING"}, message)
        for message in errors[:10]:
            self.report({"ERROR"}, message)

        if errors:
            return {"CANCELLED"}

        self.report({"INFO"}, "Stage valid: %d prims, %d warnings" % (len(document["prims"]), len(warnings)))
        return {"FINISHED"}


def _configure_workspace(workspace):
    for screen in workspace.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                for space in area.spaces:
                    if space.type != "VIEW_3D":
                        continue
                    space.show_region_toolbar = False
                    space.show_region_tool_header = False
                    space.show_region_ui = False
                    space.shading.type = "SOLID"
                    space.show_gizmo = True
                    space.show_gizmo_navigate = False
                    space.show_gizmo_tool = False
                    space.show_gizmo_object_translate = True
                    space.show_gizmo_object_rotate = True
                    space.show_gizmo_object_scale = True
                    space.overlay.show_relationship_lines = False
                    space.overlay.show_floor = True
            elif area.type == "PROPERTIES":
                for space in area.spaces:
                    if space.type == "PROPERTIES":
                        try:
                            space.context = "level_editor"
                        except (TypeError, RuntimeError):
                            pass
            elif area.type == "FILE_BROWSER":
                for space in area.spaces:
                    if space.type == "FILE_BROWSER":
                        space.browse_mode = "ASSETS"
            elif area.type == "OUTLINER":
                for space in area.spaces:
                    if space.type == "OUTLINER":
                        space.display_mode = "VIEW_LAYER"


def ensure_level_editor_workspace(activate=False):
    window = bpy.context.window
    if window is None:
        return None

    workspace = bpy.data.workspaces.get("Level Editor")
    previous = window.workspace

    if workspace is None and os.path.isfile(WORKSPACE_BLEND):
        try:
            bpy.ops.workspace.append_activate(idname="Level Editor", filepath=WORKSPACE_BLEND)
        except RuntimeError:
            pass
        workspace = bpy.data.workspaces.get("Level Editor")
        if workspace is not None and not activate:
            window.workspace = previous

    if workspace is None:
        layout = bpy.data.workspaces.get("Layout")
        if layout is not None:
            window.workspace = layout
        try:
            bpy.ops.workspace.duplicate()
        except RuntimeError:
            return None
        workspace = window.workspace
        workspace.name = "Level Editor"
        for screen in workspace.screens:
            screen.name = "Level Editor"
        scene = bpy.context.scene
        if scene is not None:
            scene.tool_settings.use_snap = True
            scene.tool_settings.snap_elements = {"INCREMENT"}

    _configure_workspace(workspace)

    if activate:
        window.workspace = workspace
        try:
            bpy.ops.workspace.reorder_to_front()
        except RuntimeError:
            pass

    return workspace


class DONUT_OT_create_level_editor_workspace(bpy.types.Operator):
    bl_idname = "donut.create_level_editor_workspace"
    bl_label = "Open Level Editor"
    bl_options = {"REGISTER"}

    def execute(self, context):
        if context.window is None:
            self.report({"WARNING"}, "Requires a window")
            return {"CANCELLED"}
        workspace = ensure_level_editor_workspace(activate=True)
        if workspace is None:
            self.report({"ERROR"}, "Could not create the Level Editor workspace")
            return {"CANCELLED"}
        self.report({"INFO"}, "Level Editor workspace ready")
        return {"FINISHED"}


class DONUT_OT_import_model_preview(bpy.types.Operator):
    bl_idname = "donut.import_model_preview"
    bl_label = "Import Full Model"
    bl_options = {"REGISTER", "UNDO"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.gltf;*.glb", options={"HIDDEN"})
    message: bpy.props.StringProperty(default="")

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.donut_prim.is_prim and obj.donut_prim.has_model

    def invoke(self, context, event):
        prim = context.active_object.donut_prim
        base_dir = context.scene.donut_scene.stage_source_dir or context.scene.donut_scene.stage_dir or os.getcwd()
        candidate = stage_io.resolve_model_path(prim.model.path, base_dir)
        if candidate:
            self.filepath = candidate
            size = gltf_bounds.file_size(candidate)
            if size > LARGE_FILE_BYTES:
                self.message = "%s is %.0f MB. Importing can take minutes and a lot of memory." % (
                    os.path.basename(candidate), size / 1048576.0)
                return context.window_manager.invoke_props_dialog(self, width=460)
            return self.execute(context)
        self.filepath = candidate or stage_io.resolve_path(base_dir, prim.model.path or "")
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def draw(self, context):
        if self.message:
            self.layout.label(text=self.message)
            self.layout.label(text="Use Create Proxy for a lightweight box instead.")

    def execute(self, context):
        obj = context.active_object
        if obj is None or not obj.donut_prim.is_prim:
            self.report({"WARNING"}, "Select a prim first")
            return {"CANCELLED"}
        try:
            count = scene_sync.import_model_preview(obj, self.filepath)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        if not count:
            self.report({"WARNING"}, "No objects imported")
            return {"CANCELLED"}
        self.report({"INFO"}, "Imported %d objects" % count)
        return {"FINISHED"}


class DONUT_OT_import_model_meshes(bpy.types.Operator):
    bl_idname = "donut.import_model_meshes"
    bl_label = "Import Meshes (no textures)"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.donut_prim.is_prim and obj.donut_prim.has_model

    def execute(self, context):
        obj = context.active_object
        base_dir = context.scene.donut_scene.stage_source_dir or context.scene.donut_scene.stage_dir or os.getcwd()
        resolved = stage_io.resolve_model_path(obj.donut_prim.model.path, base_dir)
        if not resolved:
            self.report({"ERROR"}, "Model not found: %s" % obj.donut_prim.model.path)
            return {"CANCELLED"}

        temp_path = None
        try:
            count = scene_sync.import_model_geometry(obj)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        if not count:
            self.report({"WARNING"}, "No meshes imported")
            return {"CANCELLED"}
        self.report({"INFO"}, "Imported %d mesh objects (no textures)" % count)
        return {"FINISHED"}


class DONUT_OT_import_model_textures(bpy.types.Operator):
    bl_idname = "donut.import_model_textures"
    bl_label = "Import Textures"
    bl_options = {"REGISTER", "UNDO"}

    max_size: bpy.props.EnumProperty(
        name="Texture Size",
        items=(
            ("512", "512", "Resize textures to 512 px (lowest memory)"),
            ("1024", "1024", "Resize textures to 1024 px"),
            ("2048", "2048", "Resize textures to 2048 px"),
            ("0", "Original", "Keep original resolution (high memory)"),
        ),
        default="1024",
    )

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.donut_prim.is_prim and obj.donut_prim.has_model

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        obj = context.active_object
        try:
            count = scene_sync.import_model_textured(obj, int(self.max_size))
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        if not count:
            self.report({"WARNING"}, "No objects imported")
            return {"CANCELLED"}
        self.report({"INFO"}, "Imported %d objects with textures" % count)
        return {"FINISHED"}


class DONUT_OT_create_model_proxy(bpy.types.Operator):
    bl_idname = "donut.create_model_proxy"
    bl_label = "Create Proxy"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.donut_prim.is_prim and obj.donut_prim.has_model

    def execute(self, context):
        obj = context.active_object
        base_dir = context.scene.donut_scene.stage_source_dir or context.scene.donut_scene.stage_dir or os.getcwd()
        resolved = stage_io.resolve_model_path(obj.donut_prim.model.path, base_dir)
        if not resolved:
            self.report({"ERROR"}, "Model not found: %s" % obj.donut_prim.model.path)
            return {"CANCELLED"}
        try:
            bounds = gltf_bounds.read_bounds(resolved)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        if bounds is None:
            self.report({"ERROR"}, "No geometry bounds in %s" % resolved)
            return {"CANCELLED"}
        scene_sync.create_model_proxy(obj, bounds[0], bounds[1])
        self.report({"INFO"}, "Proxy created for %s" % os.path.basename(resolved))
        return {"FINISHED"}


class DONUT_OT_load_archtype(bpy.types.Operator):
    bl_idname = "donut.load_archtype"
    bl_label = "Load Archtype"
    bl_options = {"REGISTER", "UNDO"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.archtype", options={"HIDDEN"})

    def invoke(self, context, event):
        self.filepath = context.scene.donut_scene.archtype_path or os.path.join(os.getcwd(), "archtype.archtype")
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        try:
            obj = scene_sync.load_archtype(self.filepath)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, "Loaded archtype %s" % obj.donut_prim.prim_name)
        return {"FINISHED"}


class DONUT_OT_save_archtype(bpy.types.Operator):
    bl_idname = "donut.save_archtype"
    bl_label = "Save Archtype"
    bl_options = {"REGISTER", "UNDO"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.archtype", options={"HIDDEN"})

    def invoke(self, context, event):
        obj = context.active_object
        if obj is not None and obj.donut_prim.is_template and obj.donut_prim.archtype:
            self.filepath = obj.donut_prim.archtype
        else:
            self.filepath = context.scene.donut_scene.archtype_path or os.path.join(os.getcwd(), "archtype.archtype")
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        try:
            name = scene_sync.save_archtype(self.filepath)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, "Saved archtype %s" % name)
        return {"FINISHED"}


class DONUT_OT_add_prim_from_archtype(bpy.types.Operator):
    bl_idname = "donut.add_prim_from_archtype"
    bl_label = "Instance Archtype in Stage"
    bl_options = {"REGISTER", "UNDO"}

    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.archtype", options={"HIDDEN"})

    def invoke(self, context, event):
        self.filepath = context.scene.donut_scene.archtype_path or os.path.join(os.getcwd(), "archtype.archtype")
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}

    def execute(self, context):
        try:
            obj = scene_sync.add_prim_from_archtype(self.filepath)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, "Instanced %s" % obj.donut_prim.prim_name)
        return {"FINISHED"}


class DONUT_OT_duplicate_prim(bpy.types.Operator):
    bl_idname = "donut.duplicate_prim"
    bl_label = "Duplicate Entity"
    bl_options = {"REGISTER", "UNDO"}

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return obj is not None and obj.donut_prim.is_prim and not obj.donut_prim.is_template

    def execute(self, context):
        try:
            copy = scene_sync.duplicate_prim(context.active_object)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}
        self.report({"INFO"}, "Duplicated as %s" % copy.donut_prim.prim_name)
        return {"FINISHED"}


class DONUT_OT_preview_in_engine(bpy.types.Operator):
    bl_idname = "donut.preview_in_engine"
    bl_label = "Preview in Engine"
    bl_options = {"REGISTER"}

    def execute(self, context):
        props = context.scene.donut_scene
        path = props.stage_path
        if not path:
            self.report({"WARNING"}, "Save the stage first")
            return {"CANCELLED"}
        try:
            scene_sync.export_stage(path)
        except Exception as error:
            self.report({"ERROR"}, str(error))
            return {"CANCELLED"}

        source = props.stage_source_dir or os.path.dirname(path)
        binary = os.path.join(os.path.dirname(source), "bin", "rt_bindless")
        if os.name == "nt":
            binary += ".exe"
        if not os.path.isfile(binary):
            self.report({"ERROR"}, "rt_bindless not found at %s" % binary)
            return {"CANCELLED"}

        subprocess.Popen(
            [binary, path],
            cwd=os.path.dirname(binary),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        self.report({"INFO"}, "Launched engine preview")
        return {"FINISHED"}


_CLASSES = (
    DONUT_OT_load_stage,
    DONUT_OT_save_stage,
    DONUT_OT_add_prim,
    DONUT_OT_remove_prim,
    DONUT_OT_validate_stage,
    DONUT_OT_create_level_editor_workspace,
    DONUT_OT_import_model_preview,
    DONUT_OT_import_model_meshes,
    DONUT_OT_import_model_textures,
    DONUT_OT_create_model_proxy,
    DONUT_OT_load_archtype,
    DONUT_OT_save_archtype,
    DONUT_OT_add_prim_from_archtype,
    DONUT_OT_duplicate_prim,
    DONUT_OT_preview_in_engine,
)


class DONUT_MT_level_editor(bpy.types.Menu):
    bl_idname = "DONUT_MT_level_editor"
    bl_label = "Level Editor"

    def draw(self, context):
        layout = self.layout
        layout.operator("donut.create_level_editor_workspace", text="Open Level Editor", icon="WORKSPACE")
        layout.separator()
        layout.operator("donut.load_stage", text="Load Stage", icon="FILEBROWSER")
        layout.operator("donut.save_stage", text="Save Stage", icon="FILE_TICK")
        layout.operator("donut.validate_stage", text="Validate Stage", icon="CHECKMARK")
        layout.separator()
        layout.operator("donut.add_prim", text="Add Entity", icon="ADD")
        layout.operator("donut.duplicate_prim", text="Duplicate Entity", icon="DUPLICATE")
        layout.operator("donut.add_prim_from_archtype", text="Instance Archetype", icon="ADD")
        layout.operator("donut.remove_prim", text="Remove Entity", icon="REMOVE")
        layout.separator()
        layout.operator("donut.preview_in_engine", text="Preview in Engine", icon="PLAY")
        layout.separator()
        layout.operator("donut.create_model_proxy", text="Model Proxy", icon="MESH_CUBE")
        layout.operator("donut.import_model_meshes", text="Import Meshes", icon="MESH_DATA")
        layout.operator("donut.import_model_textures", text="Import Textures", icon="TEXTURE")
        layout.separator()
        layout.operator("donut.load_archtype", text="Load Archetype", icon="FILEBROWSER")
        layout.operator("donut.save_archtype", text="Save Archetype", icon="FILE_TICK")


def _draw_level_editor_menu(self, context):
    self.layout.menu("DONUT_MT_level_editor", icon="WORLD")


@persistent
def _on_load_post(_dummy):
    try:
        ensure_level_editor_workspace(activate=False)
    except Exception:
        pass


def register():
    for cls in _CLASSES:
        bpy.utils.register_class(cls)
    bpy.utils.register_class(DONUT_MT_level_editor)
    bpy.types.TOPBAR_MT_editor_menus.append(_draw_level_editor_menu)
    bpy.app.handlers.load_post.append(_on_load_post)


def unregister():
    try:
        bpy.app.handlers.load_post.remove(_on_load_post)
    except (ValueError, RuntimeError):
        pass
    try:
        bpy.types.TOPBAR_MT_editor_menus.remove(_draw_level_editor_menu)
    except (ValueError, RuntimeError):
        pass
    bpy.utils.unregister_class(DONUT_MT_level_editor)
    for cls in reversed(_CLASSES):
        bpy.utils.unregister_class(cls)
