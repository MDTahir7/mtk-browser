import bpy
import math
import mathutils

scene = bpy.context.scene
obj = bpy.data.objects['search-icon-002']
center = mathutils.Vector((-0.0002, 0.0054, 0.0252))

for o in list(scene.objects):
    if o.type in ('CAMERA', 'LIGHT'):
        bpy.data.objects.remove(o, do_unlink=True)

track_empty = bpy.data.objects.new('TrackTarget', None)
track_empty.location = center
scene.collection.objects.link(track_empty)

cam_data = bpy.data.cameras.new('Camera')
cam_data.lens = 45
cam_obj = bpy.data.objects.new('Camera', cam_data)
scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

con = cam_obj.constraints.new(type='TRACK_TO')
con.target = track_empty
con.track_axis = 'TRACK_NEGATIVE_Z'
con.up_axis = 'UP_Y'

# Add 360 studio lighting
for i, (ang, col, energy) in enumerate([
    (math.radians(45), (1.0, 0.95, 1.0), 1.0),
    (math.radians(135), (0.7, 0.5, 1.0), 0.8),
    (math.radians(225), (0.4, 0.7, 1.0), 0.8),
    (math.radians(315), (0.9, 0.8, 1.0), 0.6)
]):
    l_data = bpy.data.lights.new(f'Light_{i}', 'AREA')
    l_data.energy = energy
    l_data.size = 0.15
    l_data.color = col
    l_obj = bpy.data.objects.new(f'Light_{i}', l_data)
    scene.collection.objects.link(l_obj)
    lx = center.x + 0.15 * math.cos(ang)
    ly = center.y + 0.15 * math.sin(ang)
    lz = center.z + 0.1
    l_obj.location = (lx, ly, lz)
    lc = l_obj.constraints.new(type='TRACK_TO')
    lc.target = track_empty
    lc.track_axis = 'TRACK_NEGATIVE_Z'
    lc.up_axis = 'UP_Y'

scene.render.engine = 'BLENDER_EEVEE_NEXT'
scene.render.film_transparent = True
scene.render.resolution_x = 256
scene.render.resolution_y = 256

dist = 0.12
pitch = math.radians(22)

# Render 4 cardinal angles
angles = [45, 135, 225, 315]
for a in angles:
    rad = math.radians(a)
    cx = center.x + dist * math.cos(pitch) * math.sin(rad)
    cy = center.y - dist * math.cos(pitch) * math.cos(rad)
    cz = center.z + dist * math.sin(pitch)
    cam_obj.location = (cx, cy, cz)
    bpy.context.view_layer.update()
    scene.render.filepath = f'E:/search/extracted/angle_{a}.png'
    bpy.ops.render.render(write_still=True)

print("ALL_ANGLES_DONE")
