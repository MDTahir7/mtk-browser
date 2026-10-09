import bpy

obj = bpy.data.objects.get('search-icon-002')
print("OBJECT:", obj.name if obj else "Not found")
if obj:
    print("Location:", obj.location)
    print("Dimensions:", obj.dimensions)
    print("Materials:")
    for slot in obj.material_slots:
        print(f"  Slot: {slot.name}, Material: {slot.material.name if slot.material else 'None'}")
        if slot.material and slot.material.node_tree:
            for node in slot.material.node_tree.nodes:
                print(f"    Node: {node.type} | {node.name}")
                if node.type == 'BSDF_PRINCIPLED':
                    for inp in node.inputs:
                        if inp.is_linked:
                            print(f"      Linked input: {inp.name} <- {inp.links[0].from_node.name}")
                        elif hasattr(inp, 'default_value') and inp.name in ('Transmission', 'Transmission Weight', 'Roughness', 'IOR', 'Alpha'):
                            print(f"      Input: {inp.name} = {inp.default_value}")
