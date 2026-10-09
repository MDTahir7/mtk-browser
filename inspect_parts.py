import bpy
import bmesh

obj = bpy.data.objects['search-icon-002']
me = obj.data

bm = bmesh.new()
bm.from_mesh(me)

# Check number of loose parts
islands = []
all_verts = set(bm.verts)
while all_verts:
    start_v = next(iter(all_verts))
    island = {start_v}
    queue = [start_v]
    all_verts.remove(start_v)
    while queue:
        curr = queue.pop()
        for e in curr.link_edges:
            other = e.other_vert(curr)
            if other in all_verts:
                all_verts.remove(other)
                island.add(other)
                queue.append(other)
    islands.append(island)

print(f"Total separate loose mesh parts: {len(islands)}")
for idx, island in enumerate(islands):
    xs = [v.co.x for v in island]
    ys = [v.co.y for v in island]
    zs = [v.co.z for v in island]
    min_c = (min(xs), min(ys), min(zs))
    max_c = (max(xs), max(ys), max(zs))
    cnt = ((min(xs)+max(xs))/2, (min(ys)+max(ys))/2, (min(zs)+max(zs))/2)
    dim = (max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs))
    print(f"Part {idx}: verts={len(island)}, center=({cnt[0]:.4f}, {cnt[1]:.4f}, {cnt[2]:.4f}), dims=({dim[0]:.4f}, {dim[1]:.4f}, {dim[2]:.4f})")
bm.free()
