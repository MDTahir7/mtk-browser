import bpy
import math
import mathutils
import time

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

# Angle 315 or similar where magnifying glass is in front and Earth is behind
# Let's inspect where Earth is relative to the magnifying glass!
dist = 0.12
pitch = math.radians(15)
# Let's test a few angles around 315 or wherever magnifying glass faces the camera
rad = math.radians(315)
cx = center.x + dist * math.cos(pitch) * math.sin(rad)
cy = center.y - dist * math.cos(pitch) * math.cos(rad)
cz = center.z + dist * math.sin(pitch)
cam_obj.location = (cx, cy, cz)

# Add lighting
key_data = bpy.data.lights.new('Key', 'SUN')
key_data.energy = 4.0
key_obj = bpy.data.objects.new('Key', key_data)
scene.collection.objects.link(key_obj)
key_obj.location = (center.x + 0.2, center.y - 0.2, center.z + 0.3)
kc = key_obj.constraints.new(type='TRACK_TO')
kc.target = track_empty
kc.track_axis = 'TRACK_NEGATIVE_Z'
kc.up_axis = 'UP_Y'

rim_data = bpy.data.lights.new('Rim', 'POINT')
rim_data.energy = 2.0
rim_obj = bpy.data.objects.new('Rim', rim_data)
scene.collection.objects.link(rim_obj)
rim_obj.location = (center.x - 0.15, center.y + 0.15, center.z + 0.1)

# Check Cycles vs EEVEE
scene.render.film_transparent = True
scene.render.resolution_x = 256
scene.render.resolution_y = 256

# Let's try Cycles with 32 samples (very fast for a 256x256 icon!)
scene.render.engine = 'CYCLES'
scene.cycles.samples = 32
scene.cycles.device = 'CPU'

t0 = time.time()
scene.render.filepath = 'E:/search/extracted/cycles_test_315.png'
bpy.ops.render.render(write_still=True)
print(f"Cycles render took: {time.time() - t0:.2f}s")
