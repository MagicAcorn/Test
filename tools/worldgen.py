"""Generates the Vale of Hearth: terrain heightfield + vertex paint, water,
roads, and placement of buildings, props, gathering nodes, NPCs, monster
spawns, lights and named regions. Output is a single WRL1 asset.

Coordinates: x east, z south (north is -z), y up. 1 unit ~ 0.75 m.
"""
import math
import struct
import numpy as np
from meshbuild import fnv1a

SIZE = 384.0
CELL = 2.0
N = int(SIZE / CELL) + 1          # vertices per side (193)
WATER = 3.0
HSCALE = 1.0 / 128.0              # u16 height units

# marker kinds (must match source/game/world.h)
MK_NODE, MK_SPAWN, MK_NPC, MK_REGION, MK_LIGHT, MK_STATION, MK_PLAYER, MK_FISH, MK_POI = 1, 2, 3, 4, 5, 6, 7, 8, 9

# gathering node types (must match source/game/data.cpp)
NODE = {
    'tree_oak': 1, 'tree_birch': 2, 'tree_pine': 3, 'tree_maple': 4, 'tree_willow': 5, 'tree_ironwood': 6,
    'ore_copper': 10, 'ore_tin': 11, 'ore_coal': 12, 'ore_iron': 13, 'ore_silver': 14, 'ore_gold': 15, 'ore_crystal': 16,
    'herb_mint': 20, 'herb_sage': 21, 'herb_lavender': 22, 'herb_sunpetal': 23, 'herb_emberroot': 24, 'herb_moonbloom': 25,
    'fish_pond': 30, 'fish_river': 31, 'fish_lake': 32,
}
STATION = {'forge': 1, 'anvil': 2, 'cookpot': 3, 'workbench': 4, 'alchemy': 5, 'hearth': 6, 'noticeboard': 7, 'campfire': 8, 'market': 9, 'bed': 10}
REGION = {'emberwick': 1, 'whisperwood': 2, 'copperhill': 3, 'barrow': 4, 'farmland': 5, 'mirrorlake': 6, 'river': 7, 'meadows': 8, 'mountains': 9}
SPAWN = {'slime_green': 1, 'slime_blue': 2, 'slime_red': 3, 'sk_minion': 4, 'sk_warrior': 5, 'sk_rogue': 6, 'sk_mage': 7, 'barrow_king': 8, 'slime_gold': 9, 'slime_purple': 10}
NPC = {'elder': 1, 'smith': 2, 'cook': 3, 'carpenter': 4, 'alchemist': 5, 'fisher': 6, 'captain': 7, 'merchant': 8,
       'farmer': 9, 'miner': 10, 'herbalist': 11, 'woodsman': 12, 'bard': 13, 'priest': 14, 'child': 15, 'adventurer': 50}
LIGHT = {'lantern': 1, 'torch': 2, 'fire': 3, 'window': 4, 'crystal': 5, 'candle': 6}

TOWN = np.array([200.0, 196.0])
LAKE = np.array([150.0, 322.0])
FOREST = np.array([92.0, 112.0])
QUARRY = np.array([318.0, 196.0])
BARROW = np.array([300.0, 72.0])
FARM = np.array([88.0, 258.0])

BRIDGE_DECK = 0.8   # height of the bridge model's deck ends above its origin (0.1 * 8)
RIVER_PTS = [(238, -20), (236, 30), (246, 85), (240, 130), (246, 175), (244, 215), (230, 255), (204, 285), (178, 305), (160, 318)]


# ------------------------------------------------------------------ noise
class Noise:
    def __init__(self, seed):
        self.rng = np.random.default_rng(seed)

    def value(self, X, Z, scale, octaves=4, persistence=0.5):
        out = np.zeros_like(X)
        amp = 1.0
        tot = 0.0
        freq = 1.0 / scale
        for o in range(octaves):
            g = int(SIZE * freq) + 3
            lat = self.rng.uniform(-1, 1, size=(g + 1, g + 1))
            fx = X * freq
            fz = Z * freq
            ix = np.floor(fx).astype(int) % g
            iz = np.floor(fz).astype(int) % g
            tx = fx - np.floor(fx)
            tz = fz - np.floor(fz)
            tx = tx * tx * (3 - 2 * tx)
            tz = tz * tz * (3 - 2 * tz)
            a = lat[iz, ix]
            b = lat[iz, ix + 1]
            c = lat[iz + 1, ix]
            d = lat[iz + 1, ix + 1]
            out += amp * ((a * (1 - tx) + b * tx) * (1 - tz) + (c * (1 - tx) + d * tx) * tz)
            tot += amp
            amp *= persistence
            freq *= 2.0
        return out / tot


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def catmull(pts, samples=12):
    pts = [np.array(p, np.float64) for p in pts]
    out = []
    for i in range(len(pts) - 1):
        p0 = pts[max(i - 1, 0)]
        p1 = pts[i]
        p2 = pts[i + 1]
        p3 = pts[min(i + 2, len(pts) - 1)]
        for k in range(samples):
            t = k / samples
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(pts[-1])
    return np.array(out)


def dist_to_polyline(X, Z, poly):
    """Distance from grid points to a polyline; also returns param along it (0..1)."""
    P = np.stack([X, Z], axis=-1)
    best = np.full(X.shape, 1e9)
    bestt = np.zeros(X.shape)
    total = 0.0
    lens = [np.linalg.norm(poly[i + 1] - poly[i]) for i in range(len(poly) - 1)]
    L = sum(lens)
    acc = 0.0
    for i in range(len(poly) - 1):
        a, b = poly[i], poly[i + 1]
        ab = b - a
        l2 = max(ab @ ab, 1e-9)
        t = np.clip(((P - a) @ ab) / l2, 0, 1)
        proj = a + t[..., None] * ab
        d = np.linalg.norm(P - proj, axis=-1)
        m = d < best
        best = np.where(m, d, best)
        bestt = np.where(m, (acc + t * lens[i]) / L, bestt)
        acc += lens[i]
    return best, bestt


def dist_point_polyline(p, poly):
    best = 1e9
    for i in range(len(poly) - 1):
        a, b = poly[i], poly[i + 1]
        ab = b - a
        t = np.clip(((p - a) @ ab) / max(ab @ ab, 1e-9), 0, 1)
        best = min(best, np.linalg.norm(p - (a + t * ab)))
    return best


class World:
    def __init__(self):
        self.objects = []    # (model, x, y, z, yaw, scale, coltype, ca, cb, flags)
        self.markers = []    # (kind, id, x, y, z, yaw, radius, extra)
        self.rng = np.random.default_rng(1234)
        xs = np.arange(N) * CELL
        self.X, self.Z = np.meshgrid(xs, xs)
        self.blocked = np.zeros((N, N), bool)   # cells reserved by placed things

    # ---------------------------------------------------------- terrain
    def height_at(self, x, z):
        fx, fz = x / CELL, z / CELL
        ix, iz = int(np.clip(math.floor(fx), 0, N - 2)), int(np.clip(math.floor(fz), 0, N - 2))
        tx, tz = fx - ix, fz - iz
        h = self.H
        a = h[iz, ix] * (1 - tx) + h[iz, ix + 1] * tx
        b = h[iz + 1, ix] * (1 - tx) + h[iz + 1, ix + 1] * tx
        return float(a * (1 - tz) + b * tz)

    def slope_at(self, x, z):
        e = 1.0
        dx = self.height_at(x + e, z) - self.height_at(x - e, z)
        dz = self.height_at(x, z + e) - self.height_at(x, z - e)
        return math.sqrt(dx * dx + dz * dz) / (2 * e)

    def build_terrain(self):
        X, Z = self.X, self.Z
        nz = Noise(77)
        base = 5.0 + nz.value(X, Z, 90, 4) * 3.2 + nz.value(X, Z, 30, 3) * 0.8
        # rolling hills
        hills = np.clip(nz.value(X, Z, 70, 3), 0, 1) * 7.0
        h = base + hills
        # mountains at the borders
        edge = np.minimum(np.minimum(X, SIZE - X), np.minimum(Z, SIZE - Z))
        mnoise = nz.value(X, Z, 40, 4) * 0.5 + 0.5
        mtn = smoothstep(42, 6, edge) * (22 + 26 * mnoise)
        h = h + mtn
        # quarry plateau with rocky cliffs on its west side
        dq = np.linalg.norm(np.stack([X - QUARRY[0], Z - QUARRY[1]], -1), axis=-1)
        plateau = smoothstep(52, 36, dq + nz.value(X, Z, 14, 2) * 6) * 7.5
        h = h + plateau
        # pit inside the quarry
        h = h - smoothstep(20, 8, np.linalg.norm(np.stack([X - (QUARRY[0] + 6), Z - (QUARRY[1] - 4)], -1), axis=-1)) * 3.0
        # barrow mound field: gentle bumps
        db = np.linalg.norm(np.stack([X - BARROW[0], Z - BARROW[1]], -1), axis=-1)
        h = h + smoothstep(40, 10, db) * 1.5
        # flatten areas
        for c, r, target in ((TOWN, 46, 6.0), (FARM, 44, 5.0), (BARROW, 34, 8.0)):
            d = np.linalg.norm(np.stack([X - c[0], Z - c[1]], -1), axis=-1)
            w = smoothstep(r, r * 0.55, d)
            h = h * (1 - w) + (target + nz.value(X, Z, 50, 2) * 0.4) * w
        # lake bowl
        dl = np.linalg.norm(np.stack([X - LAKE[0], (Z - LAKE[1]) * 1.15], -1), axis=-1) + nz.value(X, Z, 18, 3) * 9
        lake_bed = WATER - 3.2 + smoothstep(8, 36, dl) * 3.2
        lake_w = smoothstep(46, 30, dl)
        h = np.minimum(h, lake_bed * lake_w + h * (1 - lake_w) + 0.0)
        h = np.where(dl < 36, np.minimum(h, lake_bed + 0.0 + smoothstep(24, 36, dl) * 2.5), h)
        # river
        self.river = catmull(RIVER_PTS, 14)
        rd, rt = dist_to_polyline(X, Z, self.river)
        width = 5.0 + rt * 3.0
        bed = WATER - 2.0
        river_h = bed + smoothstep(width * 0.6, width * 1.6, rd) * 4.0
        h = np.where(rd < width * 2.2, np.minimum(h, river_h + smoothstep(width * 1.6, width * 2.2, rd) * 3.0), h)
        self.river_dist = rd
        # roads (to every district) slightly flattened
        self.roads = [
            catmull([TOWN + (0, -26), (200, 140), (175, 110), (130, 112), (100, 112)], 10),     # to Whisperwood
            catmull([TOWN + (26, 0), (250, 196), (275, 196), (300, 198)], 10),                  # to Copperhill (bridge)
            catmull([TOWN + (18, -18), (232, 150), (262, 112), (290, 86)], 10),                 # to Barrow (crosses river)
            catmull([TOWN + (-22, 14), (160, 220), (125, 240), (100, 252)], 10),                # to Farmland
            catmull([TOWN + (-6, 26), (190, 245), (175, 275), (165, 292)], 10),                 # to Mirror Lake
        ]
        road_d = np.full(X.shape, 1e9)
        for r in self.roads:
            d, _ = dist_to_polyline(X, Z, r)
            road_d = np.minimum(road_d, d)
        self.road_d = road_d
        # smooth heights under roads a bit (local blur)
        blur = h.copy()
        for k in range(2):
            blur = (np.roll(blur, 1, 0) + np.roll(blur, -1, 0) + np.roll(blur, 1, 1) + np.roll(blur, -1, 1) + blur * 4) / 8
        rw = smoothstep(5.0, 2.0, road_d)
        h = h * (1 - rw) + blur * rw
        # keep road banks above water except at the bridges
        self.H = h
        self.noise = nz

    def paint(self):
        X, Z, h = self.X, self.Z, self.H
        nz = Noise(99)
        n1 = nz.value(X, Z, 24, 3)
        n2 = nz.value(X, Z, 7, 2)
        C = np.zeros((N, N, 3))
        grass_a = np.array([92, 156, 62.0])
        grass_b = np.array([128, 178, 70.0])
        grass_c = np.array([150, 176, 74.0])
        t = np.clip(n1 * 0.9 + 0.5, 0, 1)[..., None]
        C = grass_a * (1 - t) + grass_b * t
        C = C * (1 - np.clip(n2 * 1.5, 0, 1)[..., None] * 0.35) + grass_c * np.clip(n2 * 1.5, 0, 1)[..., None] * 0.35
        # forest floor
        df = np.linalg.norm(np.stack([X - FOREST[0], Z - FOREST[1]], -1), axis=-1) + n1 * 20
        fw = smoothstep(80, 50, df)[..., None]
        C = C * (1 - fw) + np.array([62, 118, 54.0]) * (1 - n2[..., None] * 0.2) * fw
        # barrow gloom
        db = np.linalg.norm(np.stack([X - BARROW[0], Z - BARROW[1]], -1), axis=-1) + n1 * 10
        bw = smoothstep(46, 28, db)[..., None]
        C = C * (1 - bw) + np.array([88, 104, 76.0]) * bw
        # slopes -> rock
        gx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) / (2 * CELL)
        gz = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) / (2 * CELL)
        slope = np.sqrt(gx * gx + gz * gz)
        rw = smoothstep(0.55, 0.95, slope + n2 * 0.15)[..., None]
        rock = np.array([138, 130, 118.0]) * (0.9 + n1[..., None] * 0.2)
        C = C * (1 - rw) + rock * rw
        # high mountains: lighter rock, snow caps
        hw = smoothstep(26, 40, h + n1 * 4)[..., None]
        C = C * (1 - hw) + np.array([150, 146, 140.0]) * hw
        sw = smoothstep(44, 52, h + n2 * 4)[..., None]
        C = C * (1 - sw) + np.array([240, 244, 250.0]) * sw
        # quarry floor
        dq = np.linalg.norm(np.stack([X - QUARRY[0], Z - QUARRY[1]], -1), axis=-1) + n1 * 8
        qw = smoothstep(40, 26, dq)[..., None]
        C = C * (1 - qw) + np.array([168, 140, 110.0]) * (0.9 + n2[..., None] * 0.2) * qw
        # farm fields: stripes of crops
        dfarm = np.maximum(np.abs(X - (FARM[0] + 6)), np.abs(Z - (FARM[1] + 8)) * 1.2)
        fieldmask = (dfarm < 26) & (np.linalg.norm(np.stack([X - (FARM[0] - 10), Z - (FARM[1] - 10)], -1), axis=-1) > 12)
        stripe = (np.floor((X + Z * 0.15) / 3.0) % 2 == 0)
        fieldcol = np.where(stripe[..., None], np.array([218, 184, 92.0]), np.array([168, 150, 72.0]))
        patch = (np.floor(X / 18) + np.floor(Z / 14)) % 3
        fieldcol = np.where((patch == 1)[..., None], np.where(stripe[..., None], np.array([120, 170, 70.0]), np.array([104, 142, 60.0])), fieldcol)
        C = np.where(fieldmask[..., None], fieldcol, C)
        self.fieldmask = fieldmask
        # sand near water & underwater beds
        near = smoothstep(WATER + 1.4, WATER + 0.3, h)[..., None]
        C = C * (1 - near) + np.array([206, 190, 140.0]) * near
        under = smoothstep(WATER + 0.1, WATER - 1.5, h)[..., None]
        C = C * (1 - under) + np.array([120, 124, 92.0]) * under
        # roads (dirt) with darker edges
        road = smoothstep(3.2, 1.8, self.road_d + n2 * 0.8)[..., None]
        edge = (smoothstep(4.4, 3.2, self.road_d) * (1 - smoothstep(3.2, 2.0, self.road_d)))[..., None]
        C = C * (1 - edge * 0.25) + np.array([96, 120, 60.0]) * edge * 0.25
        C = C * (1 - road) + np.array([170, 136, 94.0]) * (0.95 + n2[..., None] * 0.1) * road
        # town plaza cobbles
        dt = np.linalg.norm(np.stack([X - TOWN[0], Z - TOWN[1]], -1), axis=-1)
        plaza = smoothstep(17, 15, dt)[..., None]
        cob = np.array([168, 160, 146.0]) * (0.92 + (((np.floor(X / 2) + np.floor(Z / 2)) % 2) * 0.06))[..., None]
        C = C * (1 - plaza) + cob * plaza
        ring = (smoothstep(21, 19, dt) * (1 - smoothstep(17, 15, dt)))[..., None]
        C = C * (1 - ring) + np.array([164, 132, 92.0]) * ring
        self.C = np.clip(C, 0, 255)
        # grass density mask (alpha): no grass on roads, rock, sand, water, plaza, fields
        dens = np.ones((N, N))
        dens *= 1 - smoothstep(0.45, 0.7, slope)
        dens *= 1 - smoothstep(4.0, 2.5, self.road_d)
        dens *= smoothstep(WATER + 1.0, WATER + 2.0, h)
        dens *= 1 - smoothstep(19, 16, dt)
        dens *= 1 - qw[..., 0]
        dens *= np.where(fieldmask, 0.0, 1.0)
        dens *= 1 - hw[..., 0]
        dens *= np.clip(0.55 + n2 * 1.2, 0, 1)
        self.A = np.clip(dens * 255, 0, 255)

    def darken_under(self, x, z, radius, amount):
        d = np.sqrt((self.X - x) ** 2 + (self.Z - z) ** 2)
        w = smoothstep(radius, radius * 0.3, d)[..., None] * amount
        self.C = self.C * (1 - w)
        self.A = self.A * (1 - w[..., 0] * 0.7)

    # --------------------------------------------------------- placement
    def place(self, model, x, z, yaw=0.0, scale=1.0, col=None, y=None, sink=0.0, flags=0, shadow=None):
        """col: None | ('c', radius) | ('b', halfx, halfz)"""
        if model.startswith('hx/'):
            scale = scale / 8.0     # the pipeline already bakes HEX_SCALE (8) into hex-pack models
        if y is None:
            y = self.height_at(x, z) - sink
        ct, ca, cb = 0, 0.0, 0.0
        if col:
            if col[0] == 'c':
                ct, ca = 1, col[1]
            else:
                ct, ca, cb = 2, col[1], col[2]
        self.objects.append((model, x, y, z, yaw, scale, ct, ca, cb, flags))
        if shadow:
            self.darken_under(x, z, shadow, 0.28)
        return y

    def marker(self, kind, ident, x, z, yaw=0.0, radius=0.0, y=None, extra=0):
        if y is None:
            y = self.height_at(x, z)
        self.markers.append((kind, ident, x, y, z, yaw, radius, extra))

    def free(self, x, z, r):
        """True if no object within r (cheap linear check)."""
        for o in self.objects:
            if (o[1] - x) ** 2 + (o[3] - z) ** 2 < (r + (o[7] if o[6] == 1 else max(o[7], o[8]))) ** 2:
                return False
        return True

    def ok_ground(self, x, z, maxslope=0.45, min_h=WATER + 0.8):
        if x < 8 or z < 8 or x > SIZE - 8 or z > SIZE - 8:
            return False
        if self.height_at(x, z) < min_h:
            return False
        if self.slope_at(x, z) > maxslope:
            return False
        if self.road_dist(x, z) < 4.5:
            return False
        return True

    def road_dist(self, x, z):
        ix, iz = int(np.clip(round(x / CELL), 0, N - 1)), int(np.clip(round(z / CELL), 0, N - 1))
        return float(self.road_d[iz, ix])

    def river_dist_at(self, x, z):
        ix, iz = int(np.clip(round(x / CELL), 0, N - 1)), int(np.clip(round(z / CELL), 0, N - 1))
        return float(self.river_dist[iz, ix])

    # ----------------------------------------------------------- content
    def build_town(self):
        tx, tz = TOWN
        self.place('pr/hearth', tx, tz, 0, 1.0, ('c', 3.6), shadow=5)
        self.marker(MK_STATION, STATION['hearth'], tx, tz, radius=5.0)
        self.marker(MK_LIGHT, LIGHT['fire'], tx, tz, y=self.height_at(tx, tz) + 2.6)
        self.marker(MK_REGION, REGION['emberwick'], tx, tz, radius=52)
        self.marker(MK_PLAYER, 0, tx + 2, tz + 9, yaw=math.pi)
        # lanterns around the plaza
        for k in range(8):
            a = k * math.pi / 4 + math.pi / 8
            x, z = tx + math.cos(a) * 16.5, tz + math.sin(a) * 16.5
            self.place('hw/lantern_standing', x, z, a, 1.8, ('c', 0.5))
            self.marker(MK_LIGHT, LIGHT['lantern'], x, z, y=self.height_at(x, z) + 1.5)
        # benches facing the hearth
        for k in range(4):
            a = k * math.pi / 2
            x, z = tx + math.cos(a) * 10.5, tz + math.sin(a) * 10.5
            self.place('hw/bench', x, z, -a + math.pi / 2, 1.0, ('b', 1.0, 0.4))
        # buildings: (model, angle around plaza, distance, scale, collision half extents)
        B = [
            ('hx/tavern', -90, 34, 8.0, (5.5, 5.0)),
            ('hx/blacksmith', 0, 33, 8.0, (5.2, 5.0)),
            ('hx/church', -150, 36, 8.0, (5.0, 4.5)),
            ('hx/market', 150, 33, 8.0, (5.0, 5.0)),
            ('hx/home_a', -35, 38, 8.0, (3.3, 3.5)),
            ('hx/home_b', -120, 40, 8.0, (3.6, 3.6)),
            ('hx/home_a', 115, 38, 8.0, (3.3, 3.5)),
            ('hx/home_b', 60, 37, 8.0, (3.6, 3.6)),
            ('hx/home_a', -180, 36, 8.0, (3.3, 3.5)),
            ('hx/well', -60, 21, 6.0, (1.4, 1.4)),
            ('hx/tower_a', 35, 44, 7.0, (3.0, 3.0)),
        ]
        self.buildings = {}
        for model, ang, dist, s, (hx, hz) in B:
            a = math.radians(ang)
            x, z = tx + math.cos(a) * dist, tz + math.sin(a) * dist
            yaw = -a - math.pi / 2   # face the plaza (model front is +z)
            self.place(model, x, z, yaw, s, ('b', hx, hz), shadow=7)
            self.buildings.setdefault(model, []).append((x, z, yaw))
        # blacksmith yard: forge + anvil in front of the smithy
        a = 0.0
        fx, fz = tx + 24, tz + 5
        self.place('pr/forge', fx, fz, -math.pi / 2, 1.0, ('b', 1.3, 0.9))
        self.marker(MK_STATION, STATION['forge'], fx, fz, radius=3.0)
        self.marker(MK_LIGHT, LIGHT['fire'], fx, fz, y=self.height_at(fx, fz) + 1.3)
        ax, az = tx + 23, tz - 4
        self.place('pr/anvil', ax, az, 0.3, 1.0, ('c', 0.8))
        self.marker(MK_STATION, STATION['anvil'], ax, az, radius=2.6)
        self.place('hx/weaponrack', tx + 26, tz - 9, -math.pi / 2, 7.0, ('b', 1.2, 0.5))
        self.place('dn/barrel_small', tx + 21, tz - 8, 0, 1.0, ('c', 0.6))
        # tavern yard: cooking pot + tables
        cx, cz = tx - 7, tz - 22
        self.place('pr/cookpot', cx, cz, 0.4, 1.0, ('c', 1.0))
        self.marker(MK_STATION, STATION['cookpot'], cx, cz, radius=2.8)
        self.marker(MK_LIGHT, LIGHT['fire'], cx, cz, y=self.height_at(cx, cz) + 0.5)
        for k, (ox, oz) in enumerate([(6, -21), (10, -18)]):
            self.place('dn/table_medium', tx + ox, tz + oz, 0.3 * k, 1.0, ('b', 1.2, 0.8))
            self.place('dn/stool', tx + ox + 1.6, tz + oz, 0, 1.0)
            self.place('dn/stool', tx + ox - 1.6, tz + oz, 0, 1.0)
        self.place('dn/keg', tx - 13, tz - 24, 0, 1.0, ('c', 0.8))
        self.place('dn/barrel_small_stack', tx - 15, tz - 21, 0.5, 1.0, ('c', 1.0))
        # notice board on the north side of the plaza
        nx, nz = tx + 6, tz - 13.5
        self.place('pr/noticeboard', nx, nz, 0.25, 1.0, ('b', 1.1, 0.3))
        self.marker(MK_STATION, STATION['noticeboard'], nx, nz, radius=2.8)
        # market stalls south-west + merchant
        mx, mz = tx - 20, tz + 18
        self.marker(MK_STATION, STATION['market'], mx, mz, radius=4.0)
        for k in range(3):
            self.place('dn/crates_stacked', mx - 5 + k * 4.5, mz + 6, 0.2 * k, 0.8, ('c', 0.9))
        self.place('hx/sack', mx + 6, mz + 1, 0, 7.0, ('c', 0.6))
        # alchemy corner near the church
        alx, alz = tx - 26, tz - 12
        self.place('pr/alchemy', alx, alz, 0.9, 1.0, ('b', 1.0, 0.5))
        self.marker(MK_STATION, STATION['alchemy'], alx, alz, radius=2.8)
        self.place('dn/shelf_small', alx - 2.2, alz - 1.4, 0.9, 1.0, ('b', 0.8, 0.3))
        # flowers/props around houses
        for k in range(14):
            a = self.rng.uniform(0, 2 * math.pi)
            r = self.rng.uniform(22, 44)
            x, z = tx + math.cos(a) * r, tz + math.sin(a) * r
            if self.free(x, z, 1.5) and self.road_dist(x, z) > 3:
                m = self.rng.choice(['dn/barrel_large', 'dn/box_small', 'hx/crate_a_big', 'hx/barrel', 'dn/barrel_small'])
                s = 7.0 if m.startswith('hx/') else (0.6 if m == 'dn/barrel_large' else 1.0)
                self.place(m, x, z, self.rng.uniform(0, 6), s, ('c', 0.7))
        # town NPCs
        self.marker(MK_NPC, NPC['elder'], tx - 4, tz + 6, yaw=math.pi * 0.8)
        self.marker(MK_NPC, NPC['smith'], tx + 22, tz + 1, yaw=-math.pi / 2)
        self.marker(MK_NPC, NPC['cook'], tx - 4, tz - 20, yaw=0.2)
        self.marker(MK_NPC, NPC['alchemist'], tx - 24, tz - 10, yaw=0.9)
        self.marker(MK_NPC, NPC['merchant'], mx, mz + 2, yaw=math.pi)
        self.marker(MK_NPC, NPC['captain'], tx + 12, tz - 26, yaw=-0.5)
        self.marker(MK_NPC, NPC['bard'], tx + 9, tz + 9, yaw=-2.3)
        self.marker(MK_NPC, NPC['priest'], tx - 30, tz - 20, yaw=0.4)
        self.marker(MK_NPC, NPC['child'], tx + 3, tz + 14, yaw=1.0)
        # bridge east of town over the river
        self.bridges = []
        for (bx, bz, yaw) in ((245.5, 196.0, math.pi / 2), (244.0, 136.0, math.pi / 2)):
            by = max(self.height_at(bx - 12, bz), self.height_at(bx + 12, bz)) + 0.1
            self.place('hx/bridge_a', bx, bz, yaw, 8.0, None, y=by - BRIDGE_DECK)
            self.bridges.append((bx, bz, 11.0, 4.0, by, 1.1))
        # lumbermill + workbench by the river south of the bridge
        lx, lz = 228.0, 228.0
        self.place('hx/lumbermill', lx, lz, math.pi * 0.5, 8.0, ('b', 4.5, 4.0), shadow=6)
        wx, wz = lx - 8, lz - 3
        self.place('pr/workbench', wx, wz, 0.2, 1.0, ('b', 1.1, 0.6))
        self.marker(MK_STATION, STATION['workbench'], wx, wz, radius=2.8)
        self.marker(MK_NPC, NPC['carpenter'], wx - 2, wz + 1.5, yaw=1.6)
        self.place('hx/resource_lumber', lx - 5, lz + 6, 0.3, 7.0, ('c', 1.6))
        # watermill downstream
        self.place('hx/watermill', 232.0, 262.0, math.pi * 0.25, 8.0, ('b', 4.5, 4.0), shadow=6)

    def build_forest(self):
        fx, fz = FOREST
        self.marker(MK_REGION, REGION['whisperwood'], fx, fz, radius=70)
        kinds = ['pr/oak_a', 'pr/oak_b', 'pr/oak_c', 'pr/birch_a', 'pr/birch_b', 'pr/pine_a', 'pr/pine_b', 'pr/darkoak_a', 'pr/autumn_a']
        weights = np.array([3, 3, 2, 2, 2, 2, 2, 1.5, 1.0])
        weights /= weights.sum()
        count = 0
        for k in range(900):
            a = self.rng.uniform(0, 2 * math.pi)
            r = math.sqrt(self.rng.uniform(0, 1)) * 78
            x, z = fx + math.cos(a) * r, fz + math.sin(a) * r * 0.9
            if not self.ok_ground(x, z, 0.6):
                continue
            if not self.free(x, z, 3.2):
                continue
            m = self.rng.choice(kinds, p=weights)
            s = self.rng.uniform(0.85, 1.25)
            self.place(m, x, z, self.rng.uniform(0, 6.28), s, ('c', 0.8 * s), shadow=4.0)
            count += 1
            if count > 190:
                break
        # undergrowth
        for k in range(160):
            a = self.rng.uniform(0, 2 * math.pi)
            r = math.sqrt(self.rng.uniform(0, 1)) * 74
            x, z = fx + math.cos(a) * r, fz + math.sin(a) * r
            if self.ok_ground(x, z, 0.6) and self.free(x, z, 1.2):
                m = self.rng.choice(['pr/bush_a', 'pr/bush_b', 'pr/bush_berry', 'pr/mushroom_red', 'pr/rock_0', 'pr/mushroom_glow'], p=[0.3, 0.25, 0.12, 0.13, 0.12, 0.08])
                self.place(m, x, z, self.rng.uniform(0, 6.28), self.rng.uniform(0.8, 1.3), ('c', 0.5) if 'rock' in m else None)
                if m == 'pr/mushroom_glow':
                    self.marker(MK_LIGHT, LIGHT['crystal'], x, z, y=self.height_at(x, z) + 0.5)
        # adventurers' camp at the forest edge
        cx, cz = 150.0, 138.0
        self.place('pr/campfire', cx, cz, 0, 1.2, ('c', 1.0))
        self.marker(MK_STATION, STATION['campfire'], cx, cz, radius=3.0)
        self.marker(MK_LIGHT, LIGHT['fire'], cx, cz, y=self.height_at(cx, cz) + 0.6)
        for k, a in enumerate((0.6, 2.2, 3.9)):
            x, z = cx + math.cos(a) * 6.5, cz + math.sin(a) * 6.5
            self.place('hx/tent', x, z, -a - math.pi / 2, 7.5, ('c', 2.2))
        self.place('dn/box_stacked', cx + 3, cz - 4, 0.4, 1.0, ('c', 0.8))
        self.marker(MK_NPC, NPC['woodsman'], cx - 3, cz + 2, yaw=0.5)
        self.marker(MK_POI, 1, cx, cz, radius=10)   # adventurer camp (NPC adventurers gather here)
        # herbalist hut deep in the woods
        hx_, hz_ = 70.0, 92.0
        self.place('hx/home_b', hx_, hz_, 0.6, 7.0, ('b', 3.2, 3.2), shadow=6)
        self.marker(MK_NPC, NPC['herbalist'], hx_ + 5, hz_ + 4, yaw=2.0)

    def build_barrow(self):
        bx, bz = BARROW
        self.marker(MK_REGION, REGION['barrow'], bx, bz, radius=42)
        self.place('hw/crypt', bx, bz - 6, math.pi, 1.6, ('b', 3.4, 3.4), shadow=6)
        self.marker(MK_POI, 2, bx, bz - 2, radius=6)
        # grave rows
        for row in range(4):
            for col in range(6):
                x = bx - 15 + col * 6 + self.rng.uniform(-0.6, 0.6)
                z = bz + 6 + row * 5.5 + self.rng.uniform(-0.6, 0.6)
                m = self.rng.choice(['hw/grave_a', 'hw/grave_b', 'hw/gravestone', 'hw/gravemarker_a', 'hw/gravemarker_b', 'hw/grave_a_destroyed'])
                self.place(m, x, z, math.pi + self.rng.uniform(-0.2, 0.2), 1.0, ('c', 0.6))
        # fence ring
        ring = 30
        for k in range(28):
            a = k * 2 * math.pi / 28
            if abs(a - math.pi / 2 * 3) < 0.25:   # gate gap facing south-west road
                continue
            x, z = bx + math.cos(a) * ring, bz + math.sin(a) * ring * 0.9
            m = 'hw/fence_broken' if self.rng.uniform() < 0.25 else 'hw/fence'
            self.place(m, x, z, -a + math.pi / 2, 1.0, ('b', 2.0, 0.3))
        for k in range(10):
            a = self.rng.uniform(0, 2 * math.pi)
            r = self.rng.uniform(10, 26)
            x, z = bx + math.cos(a) * r, bz + math.sin(a) * r
            if self.free(x, z, 2.5):
                self.place(self.rng.choice(['hw/tree_dead_large', 'hw/tree_dead_medium', 'hw/tree_dead_small']), x, z, self.rng.uniform(0, 6), 1.3, ('c', 0.6))
        for k in range(8):
            a = k * math.pi / 4
            x, z = bx + math.cos(a) * 20, bz + math.sin(a) * 18
            if self.free(x, z, 1.0):
                self.place('hw/post_lantern', x, z, a, 1.0, ('c', 0.3))
                self.marker(MK_LIGHT, LIGHT['lantern'], x, z, y=self.height_at(x, z) + 4.0)
        for k in range(12):
            a = self.rng.uniform(0, 2 * math.pi)
            r = self.rng.uniform(6, 28)
            x, z = bx + math.cos(a) * r, bz + math.sin(a) * r
            if self.free(x, z, 1.0):
                self.place(self.rng.choice(['hw/pumpkin_orange', 'hw/skull', 'hw/bone_a', 'hw/ribcage', 'hw/pumpkin_orange_jackolantern', 'hw/pumpkin_yellow_small']), x, z, self.rng.uniform(0, 6), 1.0)
        self.place('hw/shrine_candles', bx + 16, bz - 14, -2.3, 1.0, ('c', 1.0))
        self.marker(MK_LIGHT, LIGHT['candle'], bx + 16, bz - 14, y=self.height_at(bx + 16, bz - 14) + 1.2)
        # skeleton spawns
        self.marker(MK_SPAWN, SPAWN['sk_minion'], bx - 8, bz + 14, radius=10, extra=4)
        self.marker(MK_SPAWN, SPAWN['sk_minion'], bx + 10, bz + 16, radius=9, extra=3)
        self.marker(MK_SPAWN, SPAWN['sk_warrior'], bx + 4, bz + 2, radius=8, extra=2)
        self.marker(MK_SPAWN, SPAWN['sk_rogue'], bx - 14, bz - 6, radius=8, extra=2)
        self.marker(MK_SPAWN, SPAWN['sk_mage'], bx + 14, bz - 4, radius=6, extra=1)
        self.marker(MK_SPAWN, SPAWN['barrow_king'], bx, bz - 1, radius=1, extra=1)
        self.marker(MK_NPC, NPC['priest'] + 100, bx - 6, bz + 34, yaw=math.pi)   # watchman at the gate

    def build_quarry(self):
        qx, qz = QUARRY
        self.marker(MK_REGION, REGION['copperhill'], qx, qz, radius=50)
        self.place('hx/mine', qx + 22, qz - 6, -math.pi / 2, 9.0, ('b', 5.0, 5.0), shadow=6)
        self.marker(MK_NPC, NPC['miner'], qx + 12, qz - 2, yaw=-1.2)
        self.place('hx/wheelbarrow', qx + 10, qz + 4, 0.6, 7.0, ('c', 1.0))
        self.place('hx/scaffolding', qx + 8, qz - 18, 0.2, 7.0, ('b', 3.0, 3.0))
        self.place('hx/resource_stone', qx - 4, qz + 14, 0.5, 7.0, ('c', 1.6))
        self.place('dn/crates_stacked', qx + 14, qz + 10, 0.2, 1.0, ('c', 1.0))
        for k in range(4):
            a = k * 1.4
            x, z = qx + math.cos(a) * 30, qz + math.sin(a) * 26
            self.place('dn/torch_lit', x, z, 0, 1.6, ('c', 0.3))
            self.marker(MK_LIGHT, LIGHT['torch'], x, z, y=self.height_at(x, z) + 1.8)
        # decorative rocks on the cliffs
        for k in range(60):
            a = self.rng.uniform(0, 2 * math.pi)
            r = self.rng.uniform(26, 56)
            x, z = qx + math.cos(a) * r, qz + math.sin(a) * r
            if 8 < x < SIZE - 8 and 8 < z < SIZE - 8 and self.free(x, z, 2.5) and self.road_dist(x, z) > 4:
                m = 'pr/rock_%d' % self.rng.integers(0, 4)
                self.place(m, x, z, self.rng.uniform(0, 6), self.rng.uniform(0.8, 1.4), ('c', 1.2))

    def build_farm(self):
        fx, fz = FARM
        self.marker(MK_REGION, REGION['farmland'], fx, fz, radius=46)
        wx, wz = fx - 10, fz - 10
        self.place('hx/windmill', wx, wz, 0.7, 8.0, ('c', 4.0), shadow=7)
        self.place('hx/home_b', wx - 12, wz + 2, 1.2, 8.0, ('b', 3.6, 3.6), shadow=6)
        self.place('hx/grain', wx + 8, wz - 6, 0.3, 7.0, ('c', 2.0))
        self.marker(MK_NPC, NPC['farmer'], wx + 4, wz + 6, yaw=0.5)
        # fences along field borders
        for k in range(12):
            x = fx - 20 + k * 4.2
            z = fz + 36
            if self.road_dist(x, z) > 3:
                self.place('hw/fence', x, z, 0, 1.0, ('b', 2.0, 0.3))
        for k in range(10):
            x = fx + 34
            z = fz - 18 + k * 4.2
            if self.road_dist(x, z) > 3:
                self.place('hw/fence', x, z, math.pi / 2, 1.0, ('b', 2.0, 0.3))
        for k in range(10):
            x, z = fx + self.rng.uniform(-20, 30), fz + self.rng.uniform(-14, 32)
            if self.fieldmask[int(round(z / CELL)), int(round(x / CELL))] and self.free(x, z, 1.5):
                m = self.rng.choice(['hw/pumpkin_orange', 'hw/pumpkin_yellow_small', 'hx/sack'])
                self.place(m, x, z, self.rng.uniform(0, 6), 7.0 if m == 'hx/sack' else 1.0)

    def build_lake(self):
        lx, lz = LAKE
        self.marker(MK_REGION, REGION['mirrorlake'], lx, lz, radius=44)
        # dock (made of planks: crates long) on the north shore
        dx, dz = lx + 6, lz - 30
        dy = WATER + 0.55
        for k in range(5):
            self.place('hx/pallet', dx, dz + k * 2.6, 0, 7.0, None, y=dy)
        self.docks = [(dx, dz + 5.2, 1.6, 7.0, dy + 0.35, 0.0)]
        self.marker(MK_NPC, NPC['fisher'], dx - 2.5, dz - 3, yaw=math.pi)
        self.place('dn/barrel_small', dx - 3, dz - 2, 0, 1.0, ('c', 0.6))
        # lilies + reeds around
        for k in range(40):
            a = self.rng.uniform(0, 2 * math.pi)
            r = self.rng.uniform(18, 34)
            x, z = lx + math.cos(a) * r, lz + math.sin(a) * r * 0.9
            hh = self.height_at(x, z)
            if hh < WATER - 0.3:
                self.place(self.rng.choice(['hx/waterlily_a', 'hx/waterlily_b']), x, z, self.rng.uniform(0, 6), 7.0, None, y=WATER + 0.02)
            elif hh < WATER + 0.6:
                self.place('pr/reeds', x, z, self.rng.uniform(0, 6), 1.0)

    def scatter_meadow(self):
        """Trees, bushes and rocks across the open vale, avoiding districts."""
        districts = [(TOWN, 50), (FARM, 46), (BARROW, 36), (QUARRY, 30), (LAKE, 40)]
        placed = 0
        for k in range(2400):
            x, z = self.rng.uniform(12, SIZE - 12), self.rng.uniform(12, SIZE - 12)
            if any(np.hypot(x - c[0], z - c[1]) < r for c, r in districts):
                continue
            if np.hypot(x - FOREST[0], z - FOREST[1]) < 70:
                continue
            if not self.ok_ground(x, z, 0.7):
                continue
            h = self.height_at(x, z)
            if h > 30:
                if self.rng.uniform() < 0.5 and self.free(x, z, 3):
                    self.place(self.rng.choice(['pr/pine_a', 'pr/pine_b', 'pr/pine_c']), x, z, self.rng.uniform(0, 6), self.rng.uniform(1.0, 1.4), ('c', 0.8))
                continue
            if not self.free(x, z, 4.0):
                continue
            r = self.rng.uniform()
            if r < 0.45:
                m = self.rng.choice(['pr/oak_a', 'pr/oak_b', 'pr/birch_a', 'pr/autumn_a', 'pr/autumn_b', 'pr/blossom_a', 'pr/pine_b'],
                                    p=[0.25, 0.2, 0.15, 0.1, 0.08, 0.07, 0.15])
                s = self.rng.uniform(0.9, 1.3)
                self.place(m, x, z, self.rng.uniform(0, 6), s, ('c', 0.8 * s), shadow=4)
            elif r < 0.8:
                self.place(self.rng.choice(['pr/bush_a', 'pr/bush_b', 'pr/bush_berry', 'pr/bush_autumn']), x, z, self.rng.uniform(0, 6), self.rng.uniform(0.8, 1.4))
            else:
                s = self.rng.uniform(0.7, 1.3)
                self.place('pr/rock_%d' % self.rng.integers(0, 3), x, z, self.rng.uniform(0, 6), s, ('c', 1.0 * s))
            placed += 1
            if placed > 260:
                break
        # willows along the river
        for k in range(200):
            i = self.rng.integers(5, len(self.river) - 5)
            p = self.river[i]
            off = self.rng.uniform(9, 15) * (1 if self.rng.uniform() < 0.5 else -1)
            dirv = self.river[i + 1] - self.river[i - 1]
            nrm = np.array([-dirv[1], dirv[0]]) / np.linalg.norm(dirv)
            x, z = p + nrm * off
            if self.ok_ground(x, z, 0.6) and self.free(x, z, 5) and np.hypot(x - TOWN[0], z - TOWN[1]) > 40:
                self.place('pr/willow_a', x, z, self.rng.uniform(0, 6), self.rng.uniform(0.9, 1.2), ('c', 0.9), shadow=4)
        # mountain decoration: KayKit mountains on the ridge
        for k in range(30):
            side = k % 4
            t = self.rng.uniform(0.05, 0.95)
            e = self.rng.uniform(4, 14)
            x, z = [(t * SIZE, e), (SIZE - e, t * SIZE), (t * SIZE, SIZE - e), (e, t * SIZE)][side]
            m = self.rng.choice(['hx/mountain_a', 'hx/mountain_b', 'hx/mountain_c'])
            self.place(m, x, z, self.rng.uniform(0, 6), self.rng.uniform(9, 14), None, sink=2.0)

    def gather_nodes(self):
        def put(kind, x, z, yaw=None):
            self.marker(MK_NODE, NODE[kind], x, z, yaw=self.rng.uniform(0, 6.28) if yaw is None else yaw)
            self.objects_reserved.append((x, z))

        self.objects_reserved = []

        def try_ring(center, rmin, rmax, kind, count, slope=0.6, spacing=3.5, ground=True):
            n = 0
            for k in range(count * 40):
                a = self.rng.uniform(0, 2 * math.pi)
                r = self.rng.uniform(rmin, rmax)
                x, z = center[0] + math.cos(a) * r, center[1] + math.sin(a) * r
                if ground and not self.ok_ground(x, z, slope):
                    continue
                if not self.free(x, z, spacing):
                    continue
                if any((x - ox) ** 2 + (z - oz) ** 2 < spacing ** 2 for ox, oz in self.objects_reserved):
                    continue
                put(kind, x, z)
                n += 1
                if n >= count:
                    break
        # woodcutting
        try_ring(TOWN + (-30, -60), 6, 34, 'tree_oak', 10, spacing=5)
        try_ring(FOREST, 10, 60, 'tree_birch', 8, spacing=5)
        try_ring(FOREST, 20, 70, 'tree_pine', 8, spacing=5)
        try_ring(FOREST + (-20, -30), 0, 30, 'tree_maple', 6, spacing=5)
        try_ring(np.array([228.0, 290.0]), 0, 26, 'tree_willow', 6, spacing=5)
        try_ring(FOREST + (-30, -25), 0, 22, 'tree_ironwood', 4, spacing=6)
        # mining
        try_ring(QUARRY, 8, 30, 'ore_copper', 6, slope=0.9, spacing=4)
        try_ring(QUARRY, 8, 30, 'ore_tin', 6, slope=0.9, spacing=4)
        try_ring(QUARRY, 14, 34, 'ore_coal', 4, slope=0.9, spacing=4)
        try_ring(QUARRY + (8, 4), 2, 18, 'ore_iron', 5, slope=0.9, spacing=4)
        try_ring(QUARRY + (12, -10), 0, 14, 'ore_silver', 3, slope=1.0, spacing=4)
        try_ring(QUARRY + (16, -14), 0, 12, 'ore_gold', 2, slope=1.0, spacing=4)
        try_ring(BARROW + (-4, -16), 0, 12, 'ore_crystal', 3, slope=1.0, spacing=4)
        try_ring(np.array([150.0, 80.0]), 0, 18, 'ore_copper', 3, slope=0.9, spacing=4)
        try_ring(np.array([150.0, 80.0]), 0, 18, 'ore_tin', 3, slope=0.9, spacing=4)
        # herbs
        try_ring(TOWN, 50, 80, 'herb_mint', 10, spacing=3)
        try_ring(FOREST, 0, 60, 'herb_sage', 8, spacing=3)
        try_ring(FARM, 30, 60, 'herb_lavender', 7, spacing=3)
        try_ring(LAKE, 34, 60, 'herb_sunpetal', 6, spacing=3)
        try_ring(QUARRY, 30, 55, 'herb_emberroot', 5, spacing=3)
        try_ring(BARROW, 10, 34, 'herb_moonbloom', 5, spacing=3)
        # fishing spots
        for k, t in enumerate((0.18, 0.4, 0.55, 0.78)):
            i = int(t * (len(self.river) - 1))
            p = self.river[i]
            self.marker(MK_FISH, NODE['fish_river'], p[0], p[1], y=WATER, radius=4)
        for a in (0.3, 1.5, 2.6, 4.0, 5.2):
            x, z = LAKE[0] + math.cos(a) * 20, LAKE[1] + math.sin(a) * 18
            self.marker(MK_FISH, NODE['fish_lake'], x, z, y=WATER, radius=4)
        # pond in the forest -> the forest has a small pond carved at (118,150)? use river upstream instead
        self.marker(MK_FISH, NODE['fish_pond'], 236.0, 30.0, y=WATER, radius=4)

    def spawns(self):
        # meadow slimes near town / roads
        for c, kind, n in (((160, 170), 'slime_green', 4), ((240, 160), 'slime_green', 3), ((150, 240), 'slime_green', 4),
                           ((120, 300), 'slime_blue', 4), ((190, 330), 'slime_blue', 3), ((290, 260), 'slime_red', 3),
                           ((280, 140), 'slime_purple', 3), ((60, 180), 'slime_green', 3)):
            self.marker(MK_SPAWN, SPAWN[kind], c[0], c[1], radius=14, extra=n)
        self.marker(MK_SPAWN, SPAWN['slime_gold'], 70.0, 320.0, radius=8, extra=1)
        self.marker(MK_REGION, REGION['meadows'], 200.0, 290.0, radius=40)
        self.marker(MK_REGION, REGION['mountains'], 30.0, 30.0, radius=30)

    def finalize_water_bridges(self):
        pass

    # ---------------------------------------------------------- serialize
    def to_bytes(self):
        hq = np.clip(np.round(self.H / HSCALE), 0, 65535).astype('>u2')
        col = np.concatenate([self.C, self.A[..., None]], axis=-1)
        col = np.clip(np.round(col), 0, 255).astype(np.uint8)
        objs = b''
        for (m, x, y, z, yaw, s, ct, ca, cb, fl) in self.objects:
            objs += struct.pack('>IfffffBBHff', fnv1a(m), x, y, z, yaw, s, ct, fl, 0, ca, cb)
        mks = b''
        for (k, i, x, y, z, yaw, r, ex) in self.markers:
            mks += struct.pack('>HHfffffI', k, i, x, y, z, yaw, r, ex)
        brs = b''
        allb = list(self.bridges) + list(getattr(self, 'docks', []))
        for (x, z, hl, hw, y, arch) in allb:
            brs += struct.pack('>ffffff', x, z, hl, hw, y, arch)
        rivs = b''
        for p in self.river:
            rivs += struct.pack('>ff', p[0], p[1])
        hdr_size = 64
        off = hdr_size
        h_off = off; off += len(hq.tobytes()); off += (-off) % 32
        c_off = off; off += col.size; off += (-off) % 32
        o_off = off; off += len(objs); off += (-off) % 4
        m_off = off; off += len(mks); off += (-off) % 4
        b_off = off; off += len(brs)
        r_off = off; off += len(rivs)
        hdr = struct.pack('>4sHHfffIIIIIIIIIII', b'WRL1', N, N, CELL, HSCALE, WATER,
                          h_off, c_off, len(self.objects), o_off, len(self.markers), m_off, len(allb), b_off,
                          len(self.river), r_off, 0)
        hdr += b'\0' * (hdr_size - len(hdr))
        out = bytearray(off)
        out[:hdr_size] = hdr
        out[h_off:h_off + len(hq.tobytes())] = hq.tobytes()
        out[c_off:c_off + col.size] = col.tobytes()
        out[o_off:o_off + len(objs)] = objs
        out[m_off:m_off + len(mks)] = mks
        out[b_off:b_off + len(brs)] = brs
        out[r_off:r_off + len(rivs)] = rivs
        return bytes(out)


def generate():
    w = World()
    w.build_terrain()
    w.paint()
    w.build_town()
    w.build_barrow()
    w.build_quarry()
    w.build_farm()
    w.build_lake()
    w.build_forest()
    w.gather_nodes()
    w.scatter_meadow()
    w.spawns()
    return w


LAST_WORLD = None


def build_all(b, stages=None):
    global LAST_WORLD
    if stages and 'world' not in stages:
        return
    w = generate()
    LAST_WORLD = w
    # minimap colours: water blue, everything else from the paint
    wc = w.C.copy()
    deep = np.clip((WATER - w.H) / 3.0, 0, 1)[..., None]
    water_col = np.array([70, 150, 200.0]) * (1 - deep) + np.array([40, 96, 160.0]) * deep
    wc = np.where((w.H < WATER)[..., None], water_col, wc)
    w.C_minimap = wc
    data = w.to_bytes()
    b.pak.add('world/vale', 'WRLD', data)
    b.report.append(('world', 'world/vale', len(data)))
    print('  world: %d objects, %d markers, %.0f KB' % (len(w.objects), len(w.markers), len(data) / 1024))
    # preview image for docs/debugging
    try:
        from PIL import Image
        import os
        img = np.concatenate([w.C], axis=-1).astype(np.uint8)
        im = Image.fromarray(img).resize((N * 3, N * 3), Image.NEAREST)
        os.makedirs('out', exist_ok=True)
        im.save('out/world_paint.png')
    except Exception as e:  # pragma: no cover
        print('  preview failed', e)
