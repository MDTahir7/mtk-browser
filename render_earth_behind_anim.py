import bpy
import math
import mathutils
import os

scene = bpy.context.scene
obj = bpy.data.objects['search-icon-002']
bpy.context.view_layer.objects.active = obj
obj.select_set(True)

# Separate into loose parts
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.separate(type='LOOSE')
bpy.ops.object.mode_set(mode='OBJECT')

all_objs = list(bpy.context.scene.objects)
earth_parts = []
glass_parts = []

for o in all_objs:
    if not o.name.startswith('search-icon-002'):
        continue
    # Check bounding box center in Y
    ys = [v.co.y for v in o.data.vertices]
    avg_y = sum(ys) / len(ys) if ys else 0
    if avg_y > 0.0:  # Earth is at Y = +0.0120
        earth_parts.append(o)
    else:  # Magnifying glass is at Y = -0.0125
        glass_parts.append(o)

print(f"Earth parts: {len(earth_parts)}, Glass parts: {len(glass_parts)}")

# Join Earth parts into one object
bpy.ops.object.select_all(action='DESELECT')
for o in earth_parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = earth_parts[0]
bpy.ops.object.join()
earth_obj = bpy.context.active_object
earth_obj.name = 'EarthGlobe'

# Join Glass parts into one object
bpy.ops.object.select_all(action='DESELECT')
for o in glass_parts:
    o.select_set(True)
bpy.context.view_layer.objects.active = glass_parts[0]
bpy.ops.object.join()
glass_obj = bpy.context.active_object
glass_obj.name = 'MagnifyingGlass'

# Set origins to geometry centers
bpy.ops.object.select_all(action='DESELECT')
earth_obj.select_set(True)
bpy.context.view_layer.objects.active = earth_obj
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')

bpy.ops.object.select_all(action='DESELECT')
glass_obj.select_set(True)
bpy.context.view_layer.objects.active = glass_obj
bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='BOUNDS')

print("Earth origin:", earth_obj.location)
print("Glass origin:", glass_obj.location)

# Modify material for crystal clear glass on the lens
mat = bpy.data.materials.get('search-icon-002')
nodes = mat.node_tree.nodes
links = mat.node_tree.links
bsdf = None
rough_node = None
trans_node = None
for n in nodes:
    if n.type == 'BSDF_PRINCIPLED':
        bsdf = n
    elif n.type == 'TEX_IMAGE' and n.name == 'Roughness':
        rough_node = n
    elif n.type == 'TEX_IMAGE' and n.name == 'Transmisive':
        trans_node = n

if bsdf and rough_node and trans_node:
    for l in list(bsdf.inputs['Roughness'].links):
        links.remove(l)
    sub_node = nodes.new(type='ShaderNodeMath')
    sub_node.operation = 'SUBTRACT'
    sub_node.inputs[0].default_value = 1.0
    links.new(trans_node.outputs['Color'], sub_node.inputs[1])
    
    mul_node = nodes.new(type='ShaderNodeMath')
    mul_node.operation = 'MULTIPLY'
    links.new(rough_node.outputs['Color'], mul_node.inputs[0])
    links.new(sub_node.outputs['Value'], mul_node.inputs[1])
    links.new(mul_node.outputs['Value'], bsdf.inputs['Roughness'])

# Clear existing cameras and lights
for o in list(scene.objects):
    if o.type in ('CAMERA', 'LIGHT'):
        bpy.data.objects.remove(o, do_unlink=True)

# The overall center is between earth and glass
center = mathutils.Vector((0.0048, 0.0, 0.0302))

track_empty = bpy.data.objects.new('TrackTarget', None)
track_empty.location = center
scene.collection.objects.link(track_empty)

# Camera looking from front (-Y to +Y)
cam_data = bpy.data.cameras.new('Camera')
cam_data.lens = 48
cam_obj = bpy.data.objects.new('Camera', cam_data)
scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

con = cam_obj.constraints.new(type='TRACK_TO')
con.target = track_empty
con.track_axis = 'TRACK_NEGATIVE_Z'
con.up_axis = 'UP_Y'

dist = 0.125
yaw_deg = 5      # Slightly tilted 3D view (5 degrees) so you see the rim depth
pitch_deg = 8    # Gentle pitch (8 degrees) from above
rad = math.radians(yaw_deg)
pitch = math.radians(pitch_deg)

cx = center.x + dist * math.cos(pitch) * math.sin(rad)
cy = center.y - dist * math.cos(pitch) * math.cos(rad)
cz = center.z + dist * math.sin(pitch)
cam_obj.location = (cx, cy, cz)

# 3-Point Studio Lights
# Key light
key_data = bpy.data.lights.new('Key', 'AREA')
key_data.energy = 2.4
key_data.size = 0.25
key_data.color = (1.0, 0.98, 1.0)
key_obj = bpy.data.objects.new('Key', key_data)
scene.collection.objects.link(key_obj)
key_obj.location = (cx + 0.12, cy - 0.08, cz + 0.12)
kc = key_obj.constraints.new(type='TRACK_TO')
kc.target = track_empty
kc.track_axis = 'TRACK_NEGATIVE_Z'
kc.up_axis = 'UP_Y'

# Fill light (violet tint)
fill_data = bpy.data.lights.new('Fill', 'AREA')
fill_data.energy = 1.4
fill_data.size = 0.35
fill_data.color = (0.85, 0.8, 1.0)
fill_obj = bpy.data.objects.new('Fill', fill_data)
scene.collection.objects.link(fill_obj)
fill_obj.location = (cx - 0.15, cy - 0.05, cz + 0.08)
fc = fill_obj.constraints.new(type='TRACK_TO')
fc.target = track_empty
fc.track_axis = 'TRACK_NEGATIVE_Z'
fc.up_axis = 'UP_Y'

# Rim light (backlight on earth & rim)
rim_data = bpy.data.lights.new('Rim', 'AREA')
rim_data.energy = 2.0
rim_data.size = 0.25
rim_data.color = (0.7, 0.55, 1.0)
rim_obj = bpy.data.objects.new('Rim', rim_data)
scene.collection.objects.link(rim_obj)
rim_obj.location = (center.x, center.y + 0.16, center.z + 0.12)
rc = rim_obj.constraints.new(type='TRACK_TO')
rc.target = track_empty
rc.track_axis = 'TRACK_NEGATIVE_Z'
rc.up_axis = 'UP_Y'

# Handle bounce
bounce_data = bpy.data.lights.new('Bounce', 'POINT')
bounce_data.energy = 0.8
bounce_data.color = (1.0, 0.95, 0.85)
bounce_obj = bpy.data.objects.new('Bounce', bounce_data)
scene.collection.objects.link(bounce_obj)
bounce_obj.location = (center.x - 0.02, center.y - 0.05, center.z - 0.08)

# Cycles configuration with OpenImageDenoise
scene.render.engine = 'CYCLES'
scene.cycles.samples = 48
scene.cycles.use_denoising = True
scene.cycles.device = 'CPU'
scene.render.film_transparent = True
scene.render.resolution_x = 256
scene.render.resolution_y = 256

output_dir = 'E:/search/extracted/earth_behind_anim'
os.makedirs(output_dir, exist_ok=True)

total_frames = 60
earth_orig_rot = earth_obj.rotation_euler.copy()
glass_orig_loc = glass_obj.location.copy()
glass_orig_rot = glass_obj.rotation_euler.copy()

print("Starting 60-frame render...")
for f in range(total_frames):
    t = f / float(total_frames)
    angle = 2.0 * math.pi * t
    
    # 1. Earth globe rotates smoothly 360 degrees around its Z axis
    earth_obj.rotation_euler = (earth_orig_rot.x, earth_orig_rot.y, earth_orig_rot.z + angle)
    
    # 2. Magnifying glass floats in front with subtle levitation and tilt
    dz = 0.0018 * math.sin(angle)
    dx = 0.0008 * math.cos(angle)
    glass_obj.location = (glass_orig_loc.x + dx, glass_orig_loc.y, glass_orig_loc.z + dz)
    
    rx = glass_orig_rot.x + math.radians(3.5 * math.sin(angle))
    ry = glass_orig_rot.y + math.radians(2.5 * math.cos(angle))
    rz = glass_orig_rot.z + math.radians(4.0 * math.sin(angle * 2.0))
    glass_obj.rotation_euler = (rx, ry, rz)
    
    bpy.context.view_layer.update()
    scene.render.filepath = f'{output_dir}/frame_{f:03d}.png'
    bpy.ops.render.render(write_still=True)
    if f % 10 == 0:
        print(f"Rendered frame {f}/{total_frames}")

print("ANIMATION_RENDER_COMPLETE")
