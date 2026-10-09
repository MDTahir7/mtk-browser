import bpy
import math
import mathutils
import os

scene = bpy.context.scene
obj = bpy.data.objects['search-icon-002']
center = mathutils.Vector((-0.0002, 0.0054, 0.0252))

# Clear existing cameras and lights
for o in list(scene.objects):
    if o.type in ('CAMERA', 'LIGHT'):
        bpy.data.objects.remove(o, do_unlink=True)

# Add target empty
track_empty = bpy.data.objects.new('TrackTarget', None)
track_empty.location = center
scene.collection.objects.link(track_empty)

# Camera setup (matching angle 225)
cam_data = bpy.data.cameras.new('Camera')
cam_data.lens = 48
cam_data.sensor_width = 36
cam_obj = bpy.data.objects.new('Camera', cam_data)
scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

con = cam_obj.constraints.new(type='TRACK_TO')
con.target = track_empty
con.track_axis = 'TRACK_NEGATIVE_Z'
con.up_axis = 'UP_Y'

dist = 0.118
pitch = math.radians(18)
base_yaw = math.radians(225)

cam_x = center.x + dist * math.cos(pitch) * math.sin(base_yaw)
cam_y = center.y - dist * math.cos(pitch) * math.cos(base_yaw)
cam_z = center.z + dist * math.sin(pitch)
cam_obj.location = (cam_x, cam_y, cam_z)

# Lights setup
# Key light (soft violet-white)
key_data = bpy.data.lights.new('KeyLight', 'AREA')
key_data.energy = 1.0
key_data.size = 0.12
key_data.color = (0.95, 0.92, 1.0)
key_obj = bpy.data.objects.new('KeyLight', key_data)
scene.collection.objects.link(key_obj)
key_obj.location = (center.x + 0.12, center.y - 0.12, center.z + 0.12)
k_con = key_obj.constraints.new(type='TRACK_TO')
k_con.target = track_empty
k_con.track_axis = 'TRACK_NEGATIVE_Z'
k_con.up_axis = 'UP_Y'

# Rim light (bright violet for vibrant 3D rim)
rim_data = bpy.data.lights.new('RimLight', 'AREA')
rim_data.energy = 1.4
rim_data.size = 0.15
rim_data.color = (0.65, 0.45, 1.0)
rim_obj = bpy.data.objects.new('RimLight', rim_data)
scene.collection.objects.link(rim_obj)
rim_obj.location = (center.x - 0.12, center.y + 0.12, center.z + 0.08)
r_con = rim_obj.constraints.new(type='TRACK_TO')
r_con.target = track_empty
r_con.track_axis = 'TRACK_NEGATIVE_Z'
r_con.up_axis = 'UP_Y'

# Specular glint light (cyan/white gleam across the lens)
glint_data = bpy.data.lights.new('GlintLight', 'POINT')
glint_data.energy = 0.6
glint_data.color = (0.5, 0.85, 1.0)
glint_obj = bpy.data.objects.new('GlintLight', glint_data)
scene.collection.objects.link(glint_obj)

# Render settings
scene.render.engine = 'BLENDER_EEVEE_NEXT'
scene.render.film_transparent = True
scene.render.resolution_x = 256
scene.render.resolution_y = 256

output_dir = 'E:/search/extracted/anim_frames'
os.makedirs(output_dir, exist_ok=True)

total_frames = 60
orig_loc = obj.location.copy()
orig_rot = obj.rotation_euler.copy()

for f in range(total_frames):
    t = f / float(total_frames)
    angle = 2.0 * math.pi * t
    
    # Smooth levitation
    dz = 0.003 * math.sin(angle)
    obj.location = (orig_loc.x, orig_loc.y, orig_loc.z + dz)
    
    # Subtle 3D tilt & wobble
    rx = orig_rot.x + math.radians(5.0 * math.sin(angle))
    ry = orig_rot.y + math.radians(4.0 * math.sin(angle * 2.0))
    rz = orig_rot.z + math.radians(8.0 * math.cos(angle))
    obj.rotation_euler = (rx, ry, rz)
    
    # Glint light sweeping smoothly
    gx = center.x + 0.08 * math.cos(angle)
    gy = center.y - 0.08 * math.sin(angle)
    gz = center.z + 0.05 + 0.02 * math.sin(angle)
    glint_obj.location = (gx, gy, gz)
    
    bpy.context.view_layer.update()
    
    scene.render.filepath = f'{output_dir}/frame_{f:03d}.png'
    bpy.ops.render.render(write_still=True)

print("ANIMATION_RENDER_COMPLETE")
