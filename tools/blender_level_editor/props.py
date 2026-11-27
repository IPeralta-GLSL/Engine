import bpy


def _on_prim_name_update(self, context):
    obj = self.id_data
    if isinstance(obj, bpy.types.Object) and self.prim_name and obj.name != self.prim_name:
        obj.name = self.prim_name


class DonutTransformProps(bpy.types.PropertyGroup):
    translation: bpy.props.FloatVectorProperty(name="Translation", size=3, default=(0.0, 0.0, 0.0))
    rotation: bpy.props.FloatVectorProperty(name="Rotation", size=4, default=(0.0, 0.0, 0.0, 1.0))
    scaling: bpy.props.FloatVectorProperty(name="Scaling", size=3, default=(1.0, 1.0, 1.0))


class DonutCameraProps(bpy.types.PropertyGroup):
    fov_degrees: bpy.props.FloatProperty(name="FOV Degrees", default=45.0)
    z_near: bpy.props.FloatProperty(name="Z Near", default=0.1)
    z_far: bpy.props.FloatProperty(name="Z Far", default=1000.0)
    target_distance: bpy.props.FloatProperty(name="Target Distance", default=10.0, min=0.0001)


class DonutLightProps(bpy.types.PropertyGroup):
    direction_length: bpy.props.FloatProperty(name="Direction Length", default=1.016121, min=0.0001)
    color: bpy.props.FloatVectorProperty(name="Color", size=3, default=(1.0, 1.0, 1.0), subtype="COLOR")
    irradiance: bpy.props.FloatProperty(name="Irradiance", default=4.0)
    angular_size: bpy.props.FloatProperty(name="Angular Size", default=0.53)


class DonutSkyProps(bpy.types.PropertyGroup):
    sky_color: bpy.props.FloatVectorProperty(name="Sky Color", size=3, default=(0.17, 0.37, 0.65), subtype="COLOR")
    horizon_color: bpy.props.FloatVectorProperty(name="Horizon Color", size=3, default=(0.5, 0.7, 0.92), subtype="COLOR")
    ground_color: bpy.props.FloatVectorProperty(name="Ground Color", size=3, default=(0.62, 0.59, 0.55), subtype="COLOR")
    brightness: bpy.props.FloatProperty(name="Brightness", default=0.12)
    horizon_size: bpy.props.FloatProperty(name="Horizon Size", default=30.0)
    glow_size: bpy.props.FloatProperty(name="Glow Size", default=5.0)
    glow_intensity: bpy.props.FloatProperty(name="Glow Intensity", default=0.1)
    glow_sharpness: bpy.props.FloatProperty(name="Glow Sharpness", default=4.0)


class DonutModelProps(bpy.types.PropertyGroup):
    path: bpy.props.StringProperty(name="Path", default="", subtype="FILE_PATH")


class DonutPrimProps(bpy.types.PropertyGroup):
    is_prim: bpy.props.BoolProperty(default=False)
    is_template: bpy.props.BoolProperty(default=False)
    stage_index: bpy.props.IntProperty(default=0)
    prim_name: bpy.props.StringProperty(name="Name", default="Prim", update=_on_prim_name_update)
    archtype: bpy.props.StringProperty(name="Archtype", default="")
    has_name: bpy.props.BoolProperty(name="Name", default=False)
    has_tag: bpy.props.BoolProperty(name="Tag", default=False)
    has_transform: bpy.props.BoolProperty(name="Transform", default=True)
    has_camera: bpy.props.BoolProperty(name="Camera", default=False)
    has_directional_light: bpy.props.BoolProperty(name="Directional Light", default=False)
    has_sky: bpy.props.BoolProperty(name="Sky", default=False)
    has_model: bpy.props.BoolProperty(name="Model", default=False)
    tag_value: bpy.props.StringProperty(name="Tag Value", default="")
    extra_json: bpy.props.StringProperty(name="Extra Attributes", default="{}")
    model_collection: bpy.props.StringProperty(name="Model Collection", default="")
    transform: bpy.props.PointerProperty(type=DonutTransformProps)
    camera: bpy.props.PointerProperty(type=DonutCameraProps)
    light: bpy.props.PointerProperty(type=DonutLightProps)
    sky: bpy.props.PointerProperty(type=DonutSkyProps)
    model: bpy.props.PointerProperty(type=DonutModelProps)


class DonutSceneProps(bpy.types.PropertyGroup):
    stage_name: bpy.props.StringProperty(name="Stage Name", default="UntitledStage")
    stage_path: bpy.props.StringProperty(name="Stage Path", default="", subtype="FILE_PATH")
    stage_dir: bpy.props.StringProperty(name="Stage Directory", default="", subtype="DIR_PATH")
    stage_source_dir: bpy.props.StringProperty(name="Stage Source Directory", default="", subtype="DIR_PATH")
    archtype_path: bpy.props.StringProperty(name="Archtype Path", default="", subtype="FILE_PATH")
    import_meshes_on_load: bpy.props.BoolProperty(name="Import Meshes on Load", default=True)


_CLASSES = (
    DonutTransformProps,
    DonutCameraProps,
    DonutLightProps,
    DonutSkyProps,
    DonutModelProps,
    DonutPrimProps,
    DonutSceneProps,
)


def register():
    for cls in _CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.Object.donut_prim = bpy.props.PointerProperty(type=DonutPrimProps)
    bpy.types.Scene.donut_scene = bpy.props.PointerProperty(type=DonutSceneProps)


def unregister():
    del bpy.types.Scene.donut_scene
    del bpy.types.Object.donut_prim
    for cls in reversed(_CLASSES):
        bpy.utils.unregister_class(cls)
