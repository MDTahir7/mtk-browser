import bpy
import math
import mathutils

scene = bpy.context.scene
obj = bpy.data.objects['search-icon-002']
# The center of both the magnifying glass and the earth in X and Z is (0.0048, 0.0, 0.0302)!
center = mathutils.Vector((0.0048, 0.0, 0.0302))

for o in list(scene.objects):
    if o.type in ('CAMERA', 'LIGHT'):
        bpy.data.objects.remove(o, do_unlink=True)

# Clean glass shader (ensure transmission is smooth and clear)
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

track_empty = bpy.data.objects.new('TrackTarget', None)
track_empty.location = center
scene.collection.objects.link(track_empty)

cam_data = bpy.data.cameras.new('Camera')
cam_data.lens = 48
cam_obj = bpy.data.objects.new('Camera', cam_data)
scene.collection.objects.link(cam_obj)
scene.camera = cam_obj

con = cam_obj.constraints.new(type='TRACK_TO')
con.target = track_empty
con.track_axis = 'TRACK_NEGATIVE_Z'
con.up_axis = 'UP_Y'

# Lights
key_data = bpy.data.lights.new('Key', 'AREA')
key_data.energy = 2.2
key_data.size = 0.25
key_data.color = (1.0, 0.98, 1.0)
key_obj = bpy.data.objects.new('Key', key_data)
scene.collection.objects.link(key_obj)

fill_data = bpy.data.lights.new('Fill', 'AREA')
fill_data.energy = 1.4
fill_data.size = 0.35
fill_data.color = (0.85, 0.8, 1.0)
fill_obj = bpy.data.objects.new('Fill', fill_data)
scene.collection.objects.link(fill_obj)

rim_data = bpy.data.lights.new('Rim', 'AREA')
rim_data.energy = 2.0
rim_data.size = 0.25
rim_data.color = (0.7, 0.5, 1.0)
rim_obj = bpy.data.objects.new('Rim', rim_data)
scene.collection.objects.link(rim_obj)

scene.render.engine = 'CYCLES'
scene.cycles.samples = 64
scene.cycles.use_denoising = True
scene.cycles.device = 'CPU'
scene.render.film_transparent = True
scene.render.resolution_x = 256
scene.render.resolution_y = 256

dist = 0.12

# Test angles facing from negative Y towards positive Y
# yaw 0 means looking directly along +Y
# pitch 12 means slightly from above
tests = [
    ('front_0', 0, 10),
    ('front_15', 15, 12),
    ('front_n15', -15, 12),
    ('front_25', 25, 15),
    ('front_n25', -25, 15),
]

for name, yaw_deg, pitch_deg in tests:
    rad = math.radians(yaw_deg)
    pitch = math.radians(pitch_deg)
    # Looking from -Y:
    cx = center.x + dist * math.cos(pitch) * math.sin(rad)
    cy = center.y - dist * math.cos(pitch) * math.cos(rad)
    cz = center.z + dist * math.sin(pitch)
    cam_obj.location = (cx, cy, cz)
    
    key_obj.location = (cx + 0.12, cy - 0.08, cz + 0.12)
    fill_obj.location = (cx - 0.15, cy - 0.05, cz + 0.08)
    rim_obj.location = (center.x, center.y + 0.15, center.z + 0.1)
    
    bpy.context.view_layer.update()
    scene.render.filepath = f'E:/search/extracted/{name}.png'
    bpy.ops.render.render(write_still=True)
    print("Rendered:", name)
