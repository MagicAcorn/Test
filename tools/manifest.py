"""Which third-party (CC0 KayKit) assets go into the game, and how."""
import numpy as np
from gltfutil import Gltf
from meshbuild import fnv1a

ADV = 'KayKit-Character-Pack-Adventures-1.0/addons/kaykit_character_pack_adventures/'
SKL = 'KayKit-Character-Pack-Skeletons-1.0/addons/kaykit_character_pack_skeletons/'
HEX = 'KayKit-Medieval-Hexagon-Pack-1.0/addons/kaykit_medieval_hexagon_pack/Assets/gltf/'
DUN = 'KayKit-Dungeon-Remastered-1.0/addons/kaykit_dungeon_remastered/Assets/gltf/'
HAL = 'KayKit-Halloween-Bits-1.0/addons/kaykit_halloween_bits/Assets/gltf/'
FUR = 'KayKit-Furniture-Bits-1.0/addons/kaykit_furniture_bits/Assets/gltf/'

# our clip name -> (source clip, loops)
CLIPS = {
    'idle': ('Idle', True),
    'idle_unarmed': ('Unarmed_Idle', True),
    'idle_2h': ('2H_Melee_Idle', True),
    'walk': ('Walking_A', True),
    'walk_back': ('Walking_Backwards', True),
    'run': ('Running_A', True),
    'run_b': ('Running_B', True),
    'strafe_l': ('Running_Strafe_Left', True),
    'strafe_r': ('Running_Strafe_Right', True),
    'chop': ('1H_Melee_Attack_Chop', False),
    'slash': ('1H_Melee_Attack_Slice_Diagonal', False),
    'slash_h': ('1H_Melee_Attack_Slice_Horizontal', False),
    'stab': ('1H_Melee_Attack_Stab', False),
    'heavy': ('2H_Melee_Attack_Chop', False),
    'spin': ('2H_Melee_Attack_Spin', False),
    'block': ('Blocking', True),
    'block_hit': ('Block_Hit', False),
    'hit': ('Hit_A', False),
    'hit_b': ('Hit_B', False),
    'death': ('Death_A', False),
    'dead': ('Death_A_Pose', True),
    'dodge': ('Dodge_Backward', False),
    'roll': ('Dodge_Forward', False),
    'interact': ('Interact', False),
    'pickup': ('PickUp', False),
    'use_item': ('Use_Item', False),
    'cast': ('Spellcast_Shoot', False),
    'cast_long': ('Spellcast_Long', False),
    'casting': ('Spellcasting', True),
    'cast_raise': ('Spellcast_Raise', False),
    'cheer': ('Cheer', False),
    'sit_down': ('Sit_Floor_Down', False),
    'sit': ('Sit_Floor_Idle', True),
    'sit_up': ('Sit_Floor_StandUp', False),
    'chair_sit': ('Sit_Chair_Idle', True),
    'throw': ('Throw', False),
    'jump': ('Jump_Full_Short', False),
    'jump_start': ('Jump_Start', False),
    'jump_idle': ('Jump_Idle', True),
    'jump_land': ('Jump_Land', False),
    'lie': ('Lie_Idle', True),
}
SKEL_CLIPS = {
    'sk_awaken': ('Skeletons_Awaken_Floor_Long', False),
    'sk_rise': ('Spawn_Ground_Skeletons', False),
    'sk_death': ('Death_C_Skeletons', False),
    'sk_dead': ('Death_C_Pose', True),
    'sk_inactive': ('Skeletons_Inactive_Floor_Pose', True),
    'sk_idle': ('Idle_Combat', True),
    'sk_walk': ('Walking_D_Skeletons', True),
    'sk_run': ('Running_C', True),
    'sk_attack': ('1H_Melee_Attack_Jump_Chop', False),
    'sk_cast': ('Spellcast_Summon', False),
}

CHARACTERS = {
    'chr/knight': ADV + 'Characters/gltf/Knight.glb',
    'chr/barbarian': ADV + 'Characters/gltf/Barbarian.glb',
    'chr/mage': ADV + 'Characters/gltf/Mage.glb',
    'chr/rogue': ADV + 'Characters/gltf/Rogue.glb',
    'chr/rogue_hooded': ADV + 'Characters/gltf/Rogue_Hooded.glb',
    'chr/sk_minion': SKL + 'Characters/gltf/Skeleton_Minion.glb',
    'chr/sk_warrior': SKL + 'Characters/gltf/Skeleton_Warrior.glb',
    'chr/sk_rogue': SKL + 'Characters/gltf/Skeleton_Rogue.glb',
    'chr/sk_mage': SKL + 'Characters/gltf/Skeleton_Mage.glb',
}

# held items: asset name -> (character file, node name)
ITEMS = {
    'itm/sword': ('chr/knight', '1H_Sword'),
    'itm/greatsword': ('chr/knight', '2H_Sword'),
    'itm/shield_round': ('chr/knight', 'Round_Shield'),
    'itm/shield_badge': ('chr/knight', 'Badge_Shield'),
    'itm/axe': ('chr/barbarian', '1H_Axe'),
    'itm/greataxe': ('chr/barbarian', '2H_Axe'),
    'itm/mug': ('chr/barbarian', 'Mug'),
    'itm/staff': ('chr/mage', '2H_Staff'),
    'itm/wand': ('chr/mage', '1H_Wand'),
    'itm/spellbook': ('chr/mage', 'Spellbook_open'),
    'itm/knife': ('chr/rogue', 'Knife'),
    'itm/crossbow': ('chr/rogue', '1H_Crossbow'),
}

HEX_SCALE = 8.0

# name -> (relative path, scale)
STATIC = {}


def _hex(name, rel, scale=HEX_SCALE):
    STATIC['hx/' + name] = (HEX + rel, scale)


for colour in ('blue',):
    for b in ('home_A', 'home_B', 'tavern', 'blacksmith', 'market', 'church', 'windmill', 'watermill',
              'well', 'lumbermill', 'mine', 'tower_A', 'barracks', 'archeryrange', 'castle'):
        _hex(b.lower(), 'buildings/%s/building_%s_%s.gltf' % (colour, b, colour))
for b in ('bridge_A', 'grain', 'stage_A', 'scaffolding', 'destroyed'):
    _hex(b.lower(), 'buildings/neutral/building_%s.gltf' % b)
for n in ('tree_single_A', 'tree_single_B', 'tree_single_A_cut', 'tree_single_B_cut', 'trees_A_small', 'trees_A_medium',
          'trees_B_small', 'trees_B_medium', 'rock_single_A', 'rock_single_B', 'rock_single_C', 'rock_single_D',
          'rock_single_E', 'mountain_A', 'mountain_B', 'mountain_C', 'hill_single_A', 'hill_single_B', 'hill_single_C',
          'waterlily_A', 'waterlily_B', 'waterplant_A', 'waterplant_B', 'cloud_big', 'cloud_small'):
    _hex(n.lower(), 'decoration/nature/%s.gltf' % n)
for n in ('barrel', 'bucket_water', 'bucket_empty', 'crate_A_big', 'crate_B_small', 'crate_long_A', 'crate_open',
          'flag_blue', 'flag_red', 'ladder',
          'pallet', 'resource_lumber', 'resource_stone', 'sack', 'target', 'tent', 'weaponrack', 'wheelbarrow'):
    _hex(n.lower(), 'decoration/props/%s.gltf' % n)

for n in ('barrel_large', 'barrel_small', 'barrel_small_stack', 'box_large', 'box_small', 'box_stacked',
          'bottle_A_green', 'bottle_A_brown', 'bottle_B_green', 'bottle_C_brown', 'candle_triple', 'chair',
          'coin', 'coin_stack_large', 'crates_stacked', 'keg', 'plate', 'plate_food_A', 'plate_food_B',
          'shelf_small', 'stool', 'table_medium', 'table_long', 'torch_mounted', 'torch_lit', 'pillar', 'column',
          'rubble_large', 'rubble_half', 'banner_patternA_blue', 'banner_red', 'banner_thin_green', 'key',
          'sword_shield', 'trunk_large_A', 'barrier', 'bed_decorated'):
    STATIC['dn/' + n.lower()] = (DUN + n + '.gltf.glb', 1.0)
STATIC['dn/chest'] = (DUN + 'chest.glb', 1.0)
STATIC['dn/chest_gold'] = (DUN + 'chest_gold.glb', 1.0)

for n in ('arch', 'arch_gate', 'bench', 'coffin', 'crypt', 'fence', 'fence_broken', 'fence_gate', 'fence_pillar',
          'grave_A', 'grave_A_destroyed', 'grave_B', 'gravemarker_A', 'gravemarker_B', 'gravestone', 'lantern_hanging',
          'lantern_standing', 'post_lantern', 'pumpkin_orange', 'pumpkin_orange_jackolantern', 'pumpkin_yellow_small',
          'shrine_candles', 'skull', 'bone_A', 'ribcage', 'tree_dead_large', 'tree_dead_medium', 'tree_dead_small',
          'tree_pine_orange_large', 'tree_pine_orange_medium', 'tree_pine_yellow_large', 'tree_pine_yellow_medium',
          'tree_pine_yellow_small', 'plaque_candles'):
    STATIC['hw/' + n.lower()] = (HAL + n + '.gltf', 1.0)

for n in ('armchair', 'bed_single_A', 'bed_double_A', 'book_set', 'cabinet_medium_decorated', 'chair_A_wood',
          'lamp_standing', 'rug_rectangle_stripes_A', 'rug_oval_A', 'shelf_B_large_decorated', 'table_medium',
          'table_medium_long', 'chair_stool_wood', 'pictureframe_large_A'):
    STATIC['fu/' + n.lower()] = (FUR + n + '.gltf', 1.0)


def build_all(b, stages=None):
    if stages and 'kaykit' not in stages:
        return
    import os
    # rig + shared animations
    rig = b.add_rig_and_anims('skel/humanoid', CHARACTERS['chr/knight'], CLIPS, 'anim/')
    b.add_rig_and_anims('skel/humanoid', CHARACTERS['chr/sk_warrior'], SKEL_CLIPS, 'anim/')
    rigs = {}
    for name, rel in CHARACTERS.items():
        st, ne = b.add_character(name, rel, 'skel/humanoid', rig)
        print('  %-18s tris %5d batches %3d envelopes %3d' % (name, st['tris'], st['batches'], ne))
        rigs[name] = rel
    # procedural RuneScape-inspired townsfolk on an "athletic" copy of the rig
    import humangen
    import struct
    sk = bytearray(rig.to_bytes())
    struct.pack_into('>H', sk, 6, 1)   # flags: athletic proportions
    b.pak.add('skel/human', 'SKEL', bytes(sk))
    for name, spec in humangen.PEOPLE.items():
        parts, envs = humangen.build_person(rig, spec)
        skin = dict(num_joints=len(rig.names), envelopes=envs, skel_hash=fnv1a('skel/human'))
        st = b.add_model(name, parts, skin)
        print('  %-18s tris %5d batches %3d' % (name, st['tris'], st['batches']))
    from skelanim import Rig
    for iname, (cname, node) in ITEMS.items():
        gl = Gltf(b.path(CHARACTERS[cname]))
        r = Rig(gl)
        parts, joint = r.item_parts(node, b.gltf_texname(gl))
        b.add_model(iname, parts)
    missing = []
    for name, (rel, scale) in sorted(STATIC.items()):
        if not os.path.exists(b.path(rel)):
            missing.append(rel)
            continue
        b.add_static_gltf(name, rel, scale)
    if missing:
        print('  !! missing static assets:', ', '.join(os.path.basename(m) for m in missing))
