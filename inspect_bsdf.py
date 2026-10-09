import bpy

mat = bpy.data.materials.get('search-icon-002')
if mat and mat.node_tree:
    for n in mat.node_tree.nodes:
        if n.type == 'BSDF_PRINCIPLED':
            for inp in n.inputs:
                if inp.is_linked:
                    for link in inp.links:
                        print(f"BSDF Input: {inp.name} <- Node: {link.from_node.name} ({link.from_node.type}) Output: {link.from_socket.name}")
