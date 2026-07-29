import bpy
import os
import tempfile
import time
from pathlib import Path
from collections import defaultdict
import glob
import random

MMDL_VERSION = '2'
OUT_DIR = '/Users/jdh/Projects/altsense/assets/models'

def bprint(data):
    for window in bpy.context.window_manager.windows:
        screen = window.screen
        for area in screen.areas:
            if area.type == 'CONSOLE':
                override = {'window': window, 'screen': screen, 'area': area}
                bpy.ops.console.scrollback_append(override, text=str(data), type="OUTPUT")

def export_mmdls(*args):
    # save selections
    selected = bpy.context.selected_objects.copy()
    modes = [o.mode for o in selected]
    
    # deselect all
    for obj in bpy.context.selected_objects:
        obj.select_set(False)

    for pack in args:
        if 'obj' not in pack:
            raise Exception('need obj in pack')
        
        anims = []
        if 'anim' in pack:
            anims = pack.get('anim', None)
            
            if type(anims) is str:
                anims = [anims]
        
        if len(anims) == 0:
            export_mmdl(pack['obj'], filename=pack.get('filename', None))
        else:
            for anim in anims:
                export_mmdl(
                    pack['obj'],
                    filename=pack.get('filename', None),
                    anim=anim,
                    anim_max_frame=pack.get('anim_max_frame', 0))

    # deselect all                
    for obj in bpy.context.selected_objects:
        obj.select_set(False)
    
    # restore selections
    for o, m in zip(selected, modes):
        o.select_set(True)
        bpy.ops.object.mode_set(mode=m)

def export_mmdl(obj_or_obj_name, filename=None, anim=None, anim_max_frame=0):
    bprint(f'export -- obj_or_obj_name: {obj_or_obj_name} filename: {filename} anim: {anim} anim_max_frame: {anim_max_frame}')
    buffer = f'# [MMDL_VERSION] {MMDL_VERSION}\n'
    
    obj = obj_or_obj_name

    if type(obj) is str:
        if obj not in bpy.data.objects:
            raise Exception(f'could not find object with name name {obj}')
        obj = bpy.data.objects[obj]

    if obj is None:
        raise Exception(f'obj is none')
    
    # deselect all other objects
    for other_obj in bpy.data.objects:
        other_obj.select_set(False)

    obj.select_set(True)
    
    if filename is None:
        if type(obj_or_obj_name) is str:
            filename = obj_or_obj_name
        else:
            filename = obj.name

    old_action = None
    action_obj = None

    if anim is not None and len(anim) != 0:
        action_obj = obj
        if obj.parent.type == 'ARMATURE':
            action_obj = obj.parent
           
        if anim not in bpy.data.actions:
            raise Exception(f'no such action {anim}')
 
        old_action = action_obj.animation_data.action
        if old_action.name != anim:
            bprint(f'  setting action from {old_action.name if old_action is not None else None} to {anim}')
            action_obj.animation_data.action = bpy.data.actions[anim]
        else:
            bprint(f'  skipping setting same action {anim}')
            old_action = None

    random.seed(time.time_ns())
    export_path = os.path.join(
        tempfile.gettempdir(),
        filename + '_' + str(anim) + '_' + str(random.randint(10000, 99999)) + '_SPLIT.obj')

    bpy.ops.export_scene.obj(
        filepath=export_path,
        axis_forward='X',
        axis_up='Z',
        use_selection=True,
        use_animation=True,
        use_triangles=True,
        use_materials=False,
        use_vertex_groups=True)

    exported = list(glob.glob(export_path.split('_SPLIT')[0] + '*.obj'))
    exported.sort()

    for path in exported:
        index = int(path.split('_SPLIT_')[1].split('.')[0]) - 1

        skip = False
        if index > anim_max_frame:
            bprint(f'  skipping ${index} (>{anim_max_frame})')
            skip = True
            
        mmdl_name = f'{filename}${anim}${index}' if (anim is not None and len(anim) != 0) else filename
            
        if not skip:
            bprint(f'  appending {mmdl_name}')
            buffer += f'# [MMDL_BEGIN] {mmdl_name}\n'
            buffer += '# ' + time.strftime('%X %x %Z') + '\n'
            buffer += Path(path).read_text()
            buffer += f'# [MMDL_END] {mmdl_name}\n'

        Path(path).unlink()
        
    mmdl_path = os.path.join(OUT_DIR, filename + (f'${anim}' if (anim is not None and len(anim) != 0) else '') + '.mmdl')
    bprint(f'  writing to {mmdl_path}...')
    Path(mmdl_path).write_text(buffer)
    bprint('  done.')
    
    # reassign old action
    if old_action is not None:
        action_obj.animation_data.action = old_action