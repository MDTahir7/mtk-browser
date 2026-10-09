import bpy
import math
import mathutils

scene = bpy.context.scene
obj = bpy.data.objects['search-icon-002']
center = mathutils.Vector((-0.0002, 0.0054, 0.0252))

# Clear lights/cameras
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

# Studio 3-point lighting + soft fill
# Key Light
key_data = bpy.data.lights.new('Key', 'AREA')
key_data.energy = 1.8
key_data.size = 0.2
key_data.color = (1.0, 0.98, 1.0)
key_obj = bpy.data.objects.new('Key', key_data)
scene.collection.objects.link(key_obj)
kc = key_obj.constraints.new(type='TRACK_TO')
kc.target = track_empty
kc.track_axis = 'TRACK_NEGATIVE_Z'
kc.up_axis = 'UP_Y'

# Fill Light (cool soft violet)
fill_data = bpy.data.lights.new('Fill', 'AREA')
fill_data.energy = 1.0
fill_data.size = 0.3
fill_data.color = (0.75, 0.7, 1.0)
fill_obj = bpy.data.objects.new('Fill', fill_data)
scene.collection.objects.link(fill_obj)
fc = fill_obj.constraints.new(type='TRACK_TO')
fc.target = track_empty
fc.track_axis = 'TRACK_NEGATIVE_Z'
fc.up_axis = 'UP_Y'

# Rim Light (backlight for metallic rim)
rim_data = bpy.data.lights.new('Rim', 'AREA')
rim_data.energy = 1.5
rim_data.size = 0.2
rim_data.color = (0.7, 0.5, 1.0)
rim_obj = bpy.data.objects.new('Rim', rim_data)
scene.collection.objects.link(rim_obj)
rc = rim_obj.constraints.new(type='TRACK_TO')
rc.target = track_empty
rc.track_axis = 'TRACK_NEGATIVE_Z'
rc.up_axis = 'UP_Y'

# Bottom bounce light to illuminate handle
bounce_data = bpy.data.lights.new('Bounce', 'POINT')
bounce_data.energy = 0.5
bounce_data.color = (1.0, 0.95, 0.85)
bounce_obj = bpy.data.objects.new('Bounce', bounce_data)
scene.collection.objects.link(bounce_obj)

# Cycles configuration
scene.render.engine = 'CYCLES'
scene.cycles.samples = 32
scene.cycles.device = 'CPU'
scene.render.film_transparent = True
scene.render.resolution_x = 256
scene.render.resolution_y = 256

dist = 0.125

variations = [
    ('view_300', 300, 15),
    ('view_315', 315, 15),
    ('view_330', 330, 15),
    ('view_315_pitch25', 315, 25),
    ('view_340_pitch10', 340, 10),
]

for name, yaw_deg, pitch_deg in variations:
    rad = math.radians(yaw_deg)
    pitch = math.radians(pitch_deg)
    cx = center.x + dist * math.cos(pitch) * math.sin(rad)
    cy = center.y - dist * math.cos(pitch) * math.cos(rad)
    cz = center.z + dist * math.sin(pitch)
    cam_obj.location = (cx, cy, cz)
    
    # Position lights relative to camera
    key_obj.location = (cx + 0.1, cy - 0.1, cz + 0.1)
    fill_obj.location = (cx - 0.15, cy + 0.15, cz + 0.05)
    rim_obj.location = (center.x - (cx - center.x), center.y - (cy - center.y), center.z + 0.1)
    bounce_obj.location = (center.x, center.y, center.z - 0.08)
    
    bpy.context.view_layer.update()
    scene.render.filepath = f'E:/search/extracted/{name}.png'
    bpy.ops.render.render(write_still=True)
    print("Rendered:", name)
