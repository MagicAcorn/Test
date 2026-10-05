"""Rest pose of the humanoid rig with the engine's proportion tweaks applied
(must match anim::skeleton()/toModel() in source/gfx/anim.cpp)."""
import numpy as np

STRETCH = {'upperleg.l': 1.75, 'upperleg.r': 1.75, 'lowerleg.l': 1.75, 'lowerleg.r': 1.75,
           'upperarm.l': 1.25, 'upperarm.r': 1.25, 'lowerarm.l': 1.25, 'lowerarm.r': 1.25,
           'spine': 1.0, 'chest': 1.0}
USCALE = {'head': 0.74}


def quat_mat(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, q, s):
    m = np.eye(4)
    m[:3, :3] = quat_mat(q) * np.asarray(s)[None, :]
    m[:3, 3] = t
    return m


def stretched_rest(rig):
    """Returns (base, jm): per-joint 4x4 world matrices at rest. base is the
    shear-free hierarchy, jm additionally scales a bone along its own Y."""
    n = len(rig.names)
    idx = {nm: i for i, nm in enumerate(rig.names)}
    st = np.ones(n)
    us = np.ones(n)
    for k, v in STRETCH.items():
        if k in idx:
            st[idx[k]] = v
    for k, v in USCALE.items():
        if k in idx:
            us[idx[k]] = v
    lift = 0.0
    if all(k in idx for k in ('upperleg.l', 'lowerleg.l', 'foot.l')):
        lift = (st[idx['upperleg.l']] - 1) * abs(rig.rest[idx['lowerleg.l']][0][1]) + \
               (st[idx['lowerleg.l']] - 1) * abs(rig.rest[idx['foot.l']][0][1])
    hips = idx.get('hips', -1)
    base = [None] * n
    jm = [None] * n
    for j in range(n):
        t, q, s = rig.rest[j]
        t = np.array(t, float)
        par = rig.parent[j]
        if par >= 0:
            t[1] *= st[par]
        if j == hips:
            t[1] += lift
        local = trs(t, q, np.array(s, float) * us[j])
        base[j] = (base[par] if par >= 0 else rig.root_parent_world) @ local
        jm[j] = base[j].copy()
        jm[j][:3, 1] *= st[j]
    return base, jm
