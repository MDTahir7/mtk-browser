import bpy
import mathutils

obj = bpy.data.objects['search-icon-002']
bpy.context.view_layer.objects.active = obj
obj.select_set(True)

bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.separate(type='LOOSE')
bpy.ops.object.mode_set(mode='OBJECT')

print("Separated objects count:", len(bpy.context.selected_objects))
for o in bpy.context.selected_objects:
    print(f"Object {o.name}: center={o.location}, dims={o.dimensions}")
