import maya.cmds as cmds
import mayaUsd.ufe
import mayaUsd.lib
import mayaUsd_createStageWithNewLayer

import ufe
from pxr import UsdGeom, UsdLux, Gf

def create_scene():
    # Create a new layer
    ps_path_str = mayaUsd_createStageWithNewLayer.createStageWithNewLayer()

    # Fetch the stage
    stage = mayaUsd.lib.GetPrim(ps_path_str).GetStage()

    # Y-up to match Maya's native up axis
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, UsdGeom.LinearUnits.centimeters)

    # Plane
    plane = UsdGeom.Cube.Define(stage, "/Plane")
    plane.CreateSizeAttr(2.0)
    plane_xform = UsdGeom.XformCommonAPI(plane)
    plane_xform.SetScale(Gf.Vec3f(50.0, 1.0, 50.0))
    plane_xform.SetTranslate(Gf.Vec3d(0.0, -1.0, 0.0))
    plane_top_y = 0.0

    # Sphere
    sphere_radius = 2.0
    sphere_x = -2.5
    sphere = UsdGeom.Sphere.Define(stage, "/Sphere")
    sphere.CreateRadiusAttr(sphere_radius)
    UsdGeom.XformCommonAPI(sphere).SetTranslate(Gf.Vec3d(sphere_x, plane_top_y + sphere_radius, 0.0))

    # Capsule
    cap_radius = 1.5
    cap_height = 3.0
    cap_x = 2.5
    capsule = UsdGeom.Capsule.Define(stage, "/Capsule")
    capsule.CreateRadiusAttr(cap_radius)
    capsule.CreateHeightAttr(cap_height)
    capsule.CreateAxisAttr(UsdGeom.Tokens.y)
    cap_half = cap_height * 0.5 + cap_radius
    UsdGeom.XformCommonAPI(capsule).SetTranslate(Gf.Vec3d(cap_x, plane_top_y + cap_half, 0.0))

    # DistantLight
    light = UsdLux.DistantLight.Define(stage, "/DistantLight")
    light.CreateIntensityAttr(20.0)
    light_xform = UsdGeom.XformCommonAPI(light)
    light_xform.SetTranslate(Gf.Vec3d(0.0, 20.0, 0.0))
    light_xform.SetRotate(Gf.Vec3f(-45.0, 30.0, 0.0))

    # Camera
    camera = UsdGeom.Camera.Define(stage, "/Camera")
    camera.CreateFocalLengthAttr(35.0)
    camera.CreateClippingRangeAttr(Gf.Vec2f(0.1, 100000.0))

    target = Gf.Vec3d((sphere_x + cap_x) * 0.5,(sphere_radius + cap_half) * 0.5,0.0)
    eye = Gf.Vec3d(7.0, 6.0, 16.0)
    up = Gf.Vec3d(0.0, 1.0, 0.0)

    view = Gf.Matrix4d(1.0)
    view.SetLookAt(eye, target, up)
    UsdGeom.Xformable(camera).AddTransformOp().Set(view.GetInverse())

    # Look through camera
    cmds.lookThru(ps_path_str + ",/Camera")

    return stage

def show_ovrtx_window():
    if not cmds.pluginInfo("ovrtxMayaPlugin", query=True, loaded=True):
        cmds.loadPlugin("ovrtxMayaPlugin")
    cmds.ovrtxRender()

create_scene()

show_ovrtx_window()