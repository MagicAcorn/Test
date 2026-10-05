// Small vector/matrix library. Mat34 has the same memory layout as GX's Mtx
// (3 rows x 4 columns, row-major, column vectors) so it can be handed to
// GX_LoadPosMtxImm() directly.
#pragma once
#include <math.h>
#include "core/types.h"

static const f32 HV_PI = 3.14159265358979f;
static const f32 HV_TAU = 6.28318530717959f;

inline f32 hvLerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
inline f32 hvSmooth(f32 t) { t = hvClamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline f32 hvSaturate(f32 t) { return hvClamp(t, 0.0f, 1.0f); }
inline f32 hvWrapAngle(f32 a) {
    while (a > HV_PI) a -= HV_TAU;
    while (a < -HV_PI) a += HV_TAU;
    return a;
}
inline f32 hvApproach(f32 cur, f32 target, f32 maxStep) {
    if (cur < target) return hvMin(cur + maxStep, target);
    return hvMax(cur - maxStep, target);
}
inline f32 hvApproachAngle(f32 cur, f32 target, f32 maxStep) {
    f32 d = hvWrapAngle(target - cur);
    if (d > maxStep) d = maxStep;
    if (d < -maxStep) d = -maxStep;
    return hvWrapAngle(cur + d);
}
// frame-rate independent exponential smoothing factor
inline f32 hvDamp(f32 rate, f32 dt) { return 1.0f - expf(-rate * dt); }

struct Vec2 {
    f32 x, y;
    Vec2() : x(0), y(0) {}
    Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2 &o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2 &o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(f32 s) const { return Vec2(x * s, y * s); }
    f32 len() const { return sqrtf(x * x + y * y); }
};

struct Vec3 {
    f32 x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3 &o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3 &o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3 operator*(f32 s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(f32 s) const { return Vec3(x / s, y / s, z / s); }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3 &operator-=(const Vec3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3 &operator*=(f32 s) { x *= s; y *= s; z *= s; return *this; }
    f32 len() const { return sqrtf(x * x + y * y + z * z); }
    f32 len2() const { return x * x + y * y + z * z; }
    f32 lenXZ() const { return sqrtf(x * x + z * z); }
};

inline f32 dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) { return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline Vec3 normalize(const Vec3 &v) { f32 l = v.len(); return l > 1e-8f ? v / l : Vec3(0, 1, 0); }
inline Vec3 lerp(const Vec3 &a, const Vec3 &b, f32 t) { return a + (b - a) * t; }
inline Vec3 vmul(const Vec3 &a, const Vec3 &b) { return Vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline f32 distXZ(const Vec3 &a, const Vec3 &b) { f32 dx = a.x - b.x, dz = a.z - b.z; return sqrtf(dx * dx + dz * dz); }
inline f32 dist2XZ(const Vec3 &a, const Vec3 &b) { f32 dx = a.x - b.x, dz = a.z - b.z; return dx * dx + dz * dz; }
inline f32 yawTo(const Vec3 &from, const Vec3 &to) { return atan2f(to.x - from.x, to.z - from.z); }

struct Quat {
    f32 x, y, z, w;
    Quat() : x(0), y(0), z(0), w(1) {}
    Quat(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
    static Quat axisAngle(const Vec3 &axis, f32 a) {
        f32 s = sinf(a * 0.5f);
        return Quat(axis.x * s, axis.y * s, axis.z * s, cosf(a * 0.5f));
    }
    static Quat yaw(f32 a) { return Quat(0, sinf(a * 0.5f), 0, cosf(a * 0.5f)); }
    Quat operator*(const Quat &q) const {
        return Quat(w * q.x + x * q.w + y * q.z - z * q.y,
                    w * q.y - x * q.z + y * q.w + z * q.x,
                    w * q.z + x * q.y - y * q.x + z * q.w,
                    w * q.w - x * q.x - y * q.y - z * q.z);
    }
    Vec3 rotate(const Vec3 &v) const {
        Vec3 u(x, y, z);
        Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
};

inline Quat qnormalize(const Quat &q) {
    f32 l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) return Quat();
    f32 i = 1.0f / l;
    return Quat(q.x * i, q.y * i, q.z * i, q.w * i);
}
inline Quat qnlerp(const Quat &a, const Quat &b, f32 t) {
    f32 d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    f32 s = d < 0 ? -1.0f : 1.0f;
    return qnormalize(Quat(a.x + (b.x * s - a.x) * t, a.y + (b.y * s - a.y) * t, a.z + (b.z * s - a.z) * t, a.w + (b.w * s - a.w) * t));
}

struct Mat34 {
    f32 m[3][4];

    static Mat34 identity() {
        Mat34 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 4; j++) r.m[i][j] = (i == j) ? 1.0f : 0.0f;
        return r;
    }
    static Mat34 translation(const Vec3 &t) {
        Mat34 r = identity();
        r.m[0][3] = t.x; r.m[1][3] = t.y; r.m[2][3] = t.z;
        return r;
    }
    static Mat34 scale(const Vec3 &s) {
        Mat34 r = identity();
        r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z;
        return r;
    }
    static Mat34 rotY(f32 a) {
        Mat34 r = identity();
        f32 c = cosf(a), s = sinf(a);
        r.m[0][0] = c; r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }
    static Mat34 rotX(f32 a) {
        Mat34 r = identity();
        f32 c = cosf(a), s = sinf(a);
        r.m[1][1] = c; r.m[1][2] = -s;
        r.m[2][1] = s; r.m[2][2] = c;
        return r;
    }
    static Mat34 rotZ(f32 a) {
        Mat34 r = identity();
        f32 c = cosf(a), s = sinf(a);
        r.m[0][0] = c; r.m[0][1] = -s;
        r.m[1][0] = s; r.m[1][1] = c;
        return r;
    }
    static Mat34 trs(const Vec3 &t, const Quat &q, const Vec3 &s) {
        Mat34 r;
        f32 xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        f32 xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        f32 wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0][0] = (1 - 2 * (yy + zz)) * s.x; r.m[0][1] = 2 * (xy - wz) * s.y; r.m[0][2] = 2 * (xz + wy) * s.z; r.m[0][3] = t.x;
        r.m[1][0] = 2 * (xy + wz) * s.x; r.m[1][1] = (1 - 2 * (xx + zz)) * s.y; r.m[1][2] = 2 * (yz - wx) * s.z; r.m[1][3] = t.y;
        r.m[2][0] = 2 * (xz - wy) * s.x; r.m[2][1] = 2 * (yz + wx) * s.y; r.m[2][2] = (1 - 2 * (xx + yy)) * s.z; r.m[2][3] = t.z;
        return r;
    }
    // world transform for a placed object: translate * rotY(yaw) * uniform scale
    static Mat34 place(const Vec3 &t, f32 yaw, f32 s) {
        Mat34 r;
        f32 c = cosf(yaw) * s, sn = sinf(yaw) * s;
        r.m[0][0] = c; r.m[0][1] = 0; r.m[0][2] = sn; r.m[0][3] = t.x;
        r.m[1][0] = 0; r.m[1][1] = s; r.m[1][2] = 0; r.m[1][3] = t.y;
        r.m[2][0] = -sn; r.m[2][1] = 0; r.m[2][2] = c; r.m[2][3] = t.z;
        return r;
    }

    Mat34 operator*(const Mat34 &b) const {
        Mat34 r;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 4; j++) {
                r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j] + (j == 3 ? m[i][3] : 0.0f);
            }
        }
        return r;
    }
    Vec3 point(const Vec3 &v) const {
        return Vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3],
                    m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3],
                    m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]);
    }
    Vec3 vector(const Vec3 &v) const {
        return Vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                    m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                    m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
    }
    Vec3 pos() const { return Vec3(m[0][3], m[1][3], m[2][3]); }
    Vec3 axisX() const { return Vec3(m[0][0], m[1][0], m[2][0]); }
    Vec3 axisY() const { return Vec3(m[0][1], m[1][1], m[2][1]); }
    Vec3 axisZ() const { return Vec3(m[0][2], m[1][2], m[2][2]); }

    // general affine inverse
    Mat34 inverse() const {
        Mat34 r;
        f32 a = m[0][0], b = m[0][1], c = m[0][2];
        f32 d = m[1][0], e = m[1][1], f = m[1][2];
        f32 g = m[2][0], h = m[2][1], i = m[2][2];
        f32 A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
        f32 det = a * A + b * B + c * C;
        f32 id = (fabsf(det) > 1e-12f) ? 1.0f / det : 0.0f;
        r.m[0][0] = A * id; r.m[0][1] = -(b * i - c * h) * id; r.m[0][2] = (b * f - c * e) * id;
        r.m[1][0] = B * id; r.m[1][1] = (a * i - c * g) * id; r.m[1][2] = -(a * f - c * d) * id;
        r.m[2][0] = C * id; r.m[2][1] = -(a * h - b * g) * id; r.m[2][2] = (a * e - b * d) * id;
        Vec3 t(m[0][3], m[1][3], m[2][3]);
        for (int k = 0; k < 3; k++) r.m[k][3] = -(r.m[k][0] * t.x + r.m[k][1] * t.y + r.m[k][2] * t.z);
        return r;
    }
    // rotation-only matrix suitable for GX normal transforms (scale removed)
    Mat34 normalMatrix() const {
        Mat34 inv = inverse();
        Mat34 r;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) r.m[i][j] = inv.m[j][i];
            r.m[i][3] = 0;
        }
        // remove (approximately uniform) scale
        f32 det = r.m[0][0] * (r.m[1][1] * r.m[2][2] - r.m[1][2] * r.m[2][1]) -
                  r.m[0][1] * (r.m[1][0] * r.m[2][2] - r.m[1][2] * r.m[2][0]) +
                  r.m[0][2] * (r.m[1][0] * r.m[2][1] - r.m[1][1] * r.m[2][0]);
        f32 s = cbrtf(fabsf(det));
        if (s > 1e-8f) {
            f32 is = 1.0f / s;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) r.m[i][j] *= is;
        }
        return r;
    }
};

// Camera view matrix (same convention as guLookAt).
inline Mat34 lookAt(const Vec3 &eye, const Vec3 &target, const Vec3 &up) {
    Vec3 look = normalize(eye - target);
    Vec3 right = normalize(cross(up, look));
    Vec3 u = cross(look, right);
    Mat34 r;
    r.m[0][0] = right.x; r.m[0][1] = right.y; r.m[0][2] = right.z; r.m[0][3] = -dot(eye, right);
    r.m[1][0] = u.x; r.m[1][1] = u.y; r.m[1][2] = u.z; r.m[1][3] = -dot(eye, u);
    r.m[2][0] = look.x; r.m[2][1] = look.y; r.m[2][2] = look.z; r.m[2][3] = -dot(eye, look);
    return r;
}

struct Mat44 {
    f32 m[4][4];
};

// Same as guPerspective: GX clip z in [-w, 0].
inline Mat44 perspectiveGX(f32 fovyDeg, f32 aspect, f32 n, f32 f) {
    Mat44 r;
    memset(&r, 0, sizeof(r));
    f32 cot = 1.0f / tanf(fovyDeg * 0.5f * HV_PI / 180.0f);
    r.m[0][0] = cot / aspect;
    r.m[1][1] = cot;
    f32 tmp = 1.0f / (f - n);
    r.m[2][2] = -n * tmp;
    r.m[2][3] = -(f * n) * tmp;
    r.m[3][2] = -1.0f;
    return r;
}

// Same as guOrtho(t, b, l, r, n, f).
inline Mat44 orthoGX(f32 t, f32 b, f32 l, f32 rr, f32 n, f32 f) {
    Mat44 r;
    memset(&r, 0, sizeof(r));
    f32 tmp = 1.0f / (rr - l);
    r.m[0][0] = 2.0f * tmp;
    r.m[0][3] = -(rr + l) * tmp;
    tmp = 1.0f / (t - b);
    r.m[1][1] = 2.0f * tmp;
    r.m[1][3] = -(t + b) * tmp;
    tmp = 1.0f / (f - n);
    r.m[2][2] = -tmp;
    r.m[2][3] = -f * tmp;
    r.m[3][3] = 1.0f;
    return r;
}

struct Color {
    u8 r, g, b, a;
};
inline GXColor gxc(u8 r, u8 g, u8 b, u8 a = 255) { GXColor c = {r, g, b, a}; return c; }
inline GXColor gxlerp(GXColor a, GXColor b, f32 t) {
    GXColor c;
    c.r = (u8)(a.r + (b.r - a.r) * t);
    c.g = (u8)(a.g + (b.g - a.g) * t);
    c.b = (u8)(a.b + (b.b - a.b) * t);
    c.a = (u8)(a.a + (b.a - a.a) * t);
    return c;
}
