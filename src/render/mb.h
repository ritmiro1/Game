// ============================================================================
//  mb.h — построитель процедурных мешей (вершины, нормали, UV, цвета)
//  Все модели игры собираются из этих примитивов: фаски, сглаженные нормали,
//  UV-развёртки, вершинные цвета для смешивания материалов.
// ============================================================================
#ifndef TB_MB_H
#define TB_MB_H

#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

typedef struct {
    float *pos;              // 3 на вершину
    float *nor;
    float *uv;
    unsigned char *col;      // 4 на вершину
    unsigned short *idx;
    int vcap, vcnt;
    int icap, icnt;
    Matrix xf;               // текущее преобразование
    Color tint;
    const char *name;
} MB;

static void mb_init(MB *m, int vcap, int icap, const char *name) {
    memset(m, 0, sizeof(*m));
    m->vcap = vcap; m->icap = icap;
    m->pos = (float *)malloc(sizeof(float) * 3 * vcap);
    m->nor = (float *)malloc(sizeof(float) * 3 * vcap);
    m->uv = (float *)malloc(sizeof(float) * 2 * vcap);
    m->col = (unsigned char *)malloc(4 * vcap);
    m->idx = (unsigned short *)malloc(sizeof(unsigned short) * icap);
    m->xf = MatrixIdentity();
    m->tint = WHITE;
    m->name = name;
}

static void mb_free(MB *m) {
    free(m->pos); free(m->nor); free(m->uv); free(m->col); free(m->idx);
    memset(m, 0, sizeof(*m));
}

static void mb_reset(MB *m) { m->vcnt = 0; m->icnt = 0; m->xf = MatrixIdentity(); m->tint = WHITE; }

static int mb_vert(MB *m, Vector3 p, Vector3 n, float u, float v) {
    if (m->vcnt >= m->vcap - 1 || m->vcnt > 65000) return 0;
    Vector3 tp = Vector3Transform(p, m->xf);
    Vector3 rn = { n.x*m->xf.m0 + n.y*m->xf.m4 + n.z*m->xf.m8,
                   n.x*m->xf.m1 + n.y*m->xf.m5 + n.z*m->xf.m9,
                   n.x*m->xf.m2 + n.y*m->xf.m6 + n.z*m->xf.m10 };
    Vector3 tn = Vector3Normalize(rn);
    int i = m->vcnt++;
    m->pos[i * 3 + 0] = tp.x; m->pos[i * 3 + 1] = tp.y; m->pos[i * 3 + 2] = tp.z;
    m->nor[i * 3 + 0] = tn.x; m->nor[i * 3 + 1] = tn.y; m->nor[i * 3 + 2] = tn.z;
    m->uv[i * 2 + 0] = u; m->uv[i * 2 + 1] = v;
    m->col[i * 4 + 0] = m->tint.r; m->col[i * 4 + 1] = m->tint.g;
    m->col[i * 4 + 2] = m->tint.b; m->col[i * 4 + 3] = m->tint.a;
    return i;
}

static void mb_tri_idx(MB *m, int a, int b, int c) {
    if (m->icnt + 3 > m->icap) return;
    m->idx[m->icnt++] = (unsigned short)a;
    m->idx[m->icnt++] = (unsigned short)b;
    m->idx[m->icnt++] = (unsigned short)c;
}

// Четырёхугольник с автоматической UV-проекцией по граням
static void mb_quad(MB *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, float uvScale) {
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    Vector3 axisU = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 axisV = Vector3Normalize(Vector3CrossProduct(n, axisU));
    float lenU = Vector3Length(Vector3Subtract(b, a));
    float lenV = Vector3Length(Vector3Subtract(d, a));
    float u0 = Vector3DotProduct(a, axisU) * uvScale;
    float v0 = Vector3DotProduct(a, axisV) * uvScale;
    (void)lenU; (void)lenV;
    int i0 = mb_vert(m, a, n, u0, v0);
    int i1 = mb_vert(m, b, n, u0 + lenU * uvScale, v0);
    int i2 = mb_vert(m, c, n, u0 + lenU * uvScale, v0 + lenV * uvScale);
    int i3 = mb_vert(m, d, n, u0, v0 + lenV * uvScale);
    mb_tri_idx(m, i0, i1, i2);
    mb_tri_idx(m, i0, i2, i3);
}

static void mb_quad_uv(MB *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                       float ua, float va, float ub, float vb, float uc, float vc, float ud, float vd) {
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    int i0 = mb_vert(m, a, n, ua, va);
    int i1 = mb_vert(m, b, n, ub, vb);
    int i2 = mb_vert(m, c, n, uc, vc);
    int i3 = mb_vert(m, d, n, ud, vd);
    mb_tri_idx(m, i0, i1, i2);
    mb_tri_idx(m, i0, i2, i3);
}

// Параллелепипед со скруглёнными рёбрами (rounding = радиус фаски).
// Реализовано «сжатием» точек граней к скруглённому ядру — корректные фаски,
// сглаженные нормали на рёбрах, никаких плоских кубов.
static void mb_rounded_box(MB *m, Vector3 c, Vector3 half, float rounding, int seg, float uvScale) {
    float ch = rounding;
    if (ch > half.x) ch = half.x;
    if (ch > half.y) ch = half.y;
    if (ch > half.z) ch = half.z;
    if (seg < 1) seg = 1;
    if (seg > 6) seg = 6;
    Vector3 core = { half.x - ch, half.y - ch, half.z - ch };
    if (core.x < 0.0f) core.x = 0.0f;
    if (core.y < 0.0f) core.y = 0.0f;
    if (core.z < 0.0f) core.z = 0.0f;

    for (int face = 0; face < 6; face++) {
        int axis = face / 2;
        float sgn = (face % 2 == 0) ? 1.0f : -1.0f;
        for (int iu = 0; iu < seg; iu++) {
            for (int iv = 0; iv < seg; iv++) {
                float u0 = -1.0f + 2.0f * (float)iu / (float)seg;
                float u1 = -1.0f + 2.0f * (float)(iu + 1) / (float)seg;
                float v0 = -1.0f + 2.0f * (float)iv / (float)seg;
                float v1 = -1.0f + 2.0f * (float)(iv + 1) / (float)seg;
                Vector3 P[4];
                float U[4], V[4];
                float uu[4] = { u0, u1, u1, u0 };
                float vv[4] = { v0, v0, v1, v1 };
                for (int k = 0; k < 4; k++) {
                    Vector3 p = { 0, 0, 0 };
                    if (axis == 0) { p.x = sgn * half.x; p.y = uu[k] * half.y; p.z = vv[k] * half.z; }
                    else if (axis == 1) { p.y = sgn * half.y; p.x = uu[k] * half.x; p.z = vv[k] * half.z; }
                    else { p.z = sgn * half.z; p.x = uu[k] * half.x; p.y = vv[k] * half.y; }
                    Vector3 q = { p.x, p.y, p.z };
                    if (q.x > core.x) q.x = core.x; if (q.x < -core.x) q.x = -core.x;
                    if (q.y > core.y) q.y = core.y; if (q.y < -core.y) q.y = -core.y;
                    if (q.z > core.z) q.z = core.z; if (q.z < -core.z) q.z = -core.z;
                    Vector3 d = { p.x - q.x, p.y - q.y, p.z - q.z };
                    float dl = Vector3Length(d);
                    if (dl > 0.00001f) {
                        Vector3 nn = Vector3Scale(d, 1.0f / dl);
                        p = (Vector3){ q.x + nn.x * ch, q.y + nn.y * ch, q.z + nn.z * ch };
                    }
                    P[k] = Vector3Add(p, c);
                    U[k] = (uu[k] * 0.5f + 0.5f) * uvScale;
                    V[k] = (vv[k] * 0.5f + 0.5f) * uvScale;
                }
                Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(P[1], P[0]), Vector3Subtract(P[2], P[0])));
                // Проверка ориентации: нормаль должна смотреть от центра
                Vector3 outward = Vector3Normalize(Vector3Subtract(Vector3Scale(Vector3Add(P[0], P[2]), 0.5f), c));
                if (Vector3DotProduct(n, outward) < 0.0f) {
                    n = Vector3Scale(n, -1.0f);
                    int i0 = mb_vert(m, P[0], n, U[0], V[0]);
                    int i1 = mb_vert(m, P[3], n, U[3], V[3]);
                    int i2 = mb_vert(m, P[2], n, U[2], V[2]);
                    int i3 = mb_vert(m, P[1], n, U[1], V[1]);
                    mb_tri_idx(m, i0, i1, i2);
                    mb_tri_idx(m, i0, i2, i3);
                } else {
                    int i0 = mb_vert(m, P[0], n, U[0], V[0]);
                    int i1 = mb_vert(m, P[1], n, U[1], V[1]);
                    int i2 = mb_vert(m, P[2], n, U[2], V[2]);
                    int i3 = mb_vert(m, P[3], n, U[3], V[3]);
                    mb_tri_idx(m, i0, i1, i2);
                    mb_tri_idx(m, i0, i2, i3);
                }
            }
        }
    }
}

static void mb_box(MB *m, Vector3 c, Vector3 half, float uvScale, float chamfer) {
    mb_rounded_box(m, c, half, chamfer > 0.0005f ? chamfer : 0.002f, 2, uvScale);
}

// Цилиндр/усечённый конус вдоль Y с крышками и сглаженными нормалями
static void mb_cylinder(MB *m, Vector3 base, float rBottom, float rTop, float height,
                        int segs, float uvScale, bool caps) {
    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / (float)segs * 2.0f * PI;
        float a1 = (float)(i + 1) / (float)segs * 2.0f * PI;
        Vector3 b0 = { base.x + cosf(a0) * rBottom, base.y, base.z + sinf(a0) * rBottom };
        Vector3 b1 = { base.x + cosf(a1) * rBottom, base.y, base.z + sinf(a1) * rBottom };
        Vector3 t0 = { base.x + cosf(a0) * rTop, base.y + height, base.z + sinf(a0) * rTop };
        Vector3 t1 = { base.x + cosf(a1) * rTop, base.y + height, base.z + sinf(a1) * rTop };
        float slope = (rBottom - rTop) / (height > 0.0001f ? height : 0.0001f);
        Vector3 n0 = Vector3Normalize((Vector3){ cosf(a0), slope, sinf(a0) });
        Vector3 n1 = Vector3Normalize((Vector3){ cosf(a1), slope, sinf(a1) });
        float u0 = (float)i / (float)segs, u1 = (float)(i + 1) / (float)segs;
        float v0 = 0.0f, v1 = height * uvScale;
        int i0 = mb_vert(m, b0, n0, u0, v0);
        int i1 = mb_vert(m, b1, n1, u1, v0);
        int i2 = mb_vert(m, t1, n1, u1, v1);
        int i3 = mb_vert(m, t0, n0, u0, v1);
        mb_tri_idx(m, i0, i1, i2);
        mb_tri_idx(m, i0, i2, i3);
        if (caps) {
            if (rTop > 0.0005f) {
                Vector3 nu = { 0, 1, 0 };
                int c0 = mb_vert(m, (Vector3){ base.x, base.y + height, base.z }, nu, 0.5f, 0.5f);
                int p0 = mb_vert(m, t0, nu, 0.5f + cosf(a0) * 0.5f, 0.5f + sinf(a0) * 0.5f);
                int p1 = mb_vert(m, t1, nu, 0.5f + cosf(a1) * 0.5f, 0.5f + sinf(a1) * 0.5f);
                mb_tri_idx(m, c0, p0, p1);
            }
            if (rBottom > 0.0005f) {
                Vector3 nd = { 0, -1, 0 };
                int c0 = mb_vert(m, base, nd, 0.5f, 0.5f);
                int p0 = mb_vert(m, b0, nd, 0.5f + cosf(a0) * 0.5f, 0.5f + sinf(a0) * 0.5f);
                int p1 = mb_vert(m, b1, nd, 0.5f + cosf(a1) * 0.5f, 0.5f + sinf(a1) * 0.5f);
                mb_tri_idx(m, c0, p1, p0);
            }
        }
    }
}

// Эллипсоид/сфера с переменным масштабом по осям
static void mb_ellipsoid(MB *m, Vector3 c, Vector3 radii, int segs, int rings, float uvScale) {
    for (int r = 0; r < rings; r++) {
        float v0 = (float)r / (float)rings * PI;
        float v1 = (float)(r + 1) / (float)rings * PI;
        for (int s = 0; s < segs; s++) {
            float u0 = (float)s / (float)segs * 2.0f * PI;
            float u1 = (float)(s + 1) / (float)segs * 2.0f * PI;
            Vector3 p00 = { c.x + sinf(v0) * cosf(u0) * radii.x, c.y + cosf(v0) * radii.y, c.z + sinf(v0) * sinf(u0) * radii.z };
            Vector3 p01 = { c.x + sinf(v0) * cosf(u1) * radii.x, c.y + cosf(v0) * radii.y, c.z + sinf(v0) * sinf(u1) * radii.z };
            Vector3 p10 = { c.x + sinf(v1) * cosf(u0) * radii.x, c.y + cosf(v1) * radii.y, c.z + sinf(v1) * sinf(u0) * radii.z };
            Vector3 p11 = { c.x + sinf(v1) * cosf(u1) * radii.x, c.y + cosf(v1) * radii.y, c.z + sinf(v1) * sinf(u1) * radii.z };
            Vector3 n00 = Vector3Normalize((Vector3){ (p00.x - c.x) / (radii.x * radii.x), (p00.y - c.y) / (radii.y * radii.y), (p00.z - c.z) / (radii.z * radii.z) });
            Vector3 n01 = Vector3Normalize((Vector3){ (p01.x - c.x) / (radii.x * radii.x), (p01.y - c.y) / (radii.y * radii.y), (p01.z - c.z) / (radii.z * radii.z) });
            Vector3 n10 = Vector3Normalize((Vector3){ (p10.x - c.x) / (radii.x * radii.x), (p10.y - c.y) / (radii.y * radii.y), (p10.z - c.z) / (radii.z * radii.z) });
            Vector3 n11 = Vector3Normalize((Vector3){ (p11.x - c.x) / (radii.x * radii.x), (p11.y - c.y) / (radii.y * radii.y), (p11.z - c.z) / (radii.z * radii.z) });
            float tu0 = (float)s / (float)segs * 2.0f * uvScale;
            float tv0 = (float)r / (float)rings * uvScale;
            float tu1 = (float)(s + 1) / (float)segs * 2.0f * uvScale;
            float tv1 = (float)(r + 1) / (float)rings * uvScale;
            int a = mb_vert(m, p00, n00, tu0, tv0);
            int b = mb_vert(m, p01, n01, tu1, tv0);
            int d = mb_vert(m, p11, n11, tu1, tv1);
            int e = mb_vert(m, p10, n10, tu0, tv1);
            mb_tri_idx(m, a, b, d);
            mb_tri_idx(m, a, d, e);
        }
    }
}

// Труба по набору точек (для удочки, веток, тросов, лески)
static void mb_tube(MB *m, const Vector3 *pts, int count, float radius, int segs, bool taper) {
    for (int i = 0; i < count - 1; i++) {
        Vector3 a = pts[i], b = pts[i + 1];
        Vector3 dir = Vector3Normalize(Vector3Subtract(b, a));
        Vector3 up = fabsf(dir.y) > 0.95f ? (Vector3){ 1, 0, 0 } : (Vector3){ 0, 1, 0 };
        Vector3 side = Vector3Normalize(Vector3CrossProduct(dir, up));
        Vector3 up2 = Vector3Normalize(Vector3CrossProduct(side, dir));
        float ra = taper ? radius * (1.0f - 0.85f * (float)i / (float)(count > 1 ? count - 1 : 1)) : radius;
        float rb = taper ? radius * (1.0f - 0.85f * (float)(i + 1) / (float)(count > 1 ? count - 1 : 1)) : radius;
        for (int s = 0; s < segs; s++) {
            float t0 = (float)s / (float)segs * 2.0f * PI;
            float t1 = (float)(s + 1) / (float)segs * 2.0f * PI;
            Vector3 n0 = Vector3Add(Vector3Scale(side, cosf(t0)), Vector3Scale(up2, sinf(t0)));
            Vector3 n1 = Vector3Add(Vector3Scale(side, cosf(t1)), Vector3Scale(up2, sinf(t1)));
            Vector3 p00 = Vector3Add(a, Vector3Scale(n0, ra));
            Vector3 p01 = Vector3Add(a, Vector3Scale(n1, ra));
            Vector3 p10 = Vector3Add(b, Vector3Scale(n0, rb));
            Vector3 p11 = Vector3Add(b, Vector3Scale(n1, rb));
            int i0 = mb_vert(m, p00, n0, (float)s / segs, (float)i);
            int i1 = mb_vert(m, p01, n1, (float)(s + 1) / segs, (float)i);
            int i2 = mb_vert(m, p11, n1, (float)(s + 1) / segs, (float)(i + 1));
            int i3 = mb_vert(m, p10, n0, (float)s / segs, (float)(i + 1));
            mb_tri_idx(m, i0, i1, i2);
            mb_tri_idx(m, i0, i2, i3);
        }
    }
}

// Лофт по профилям: сечения вдоль оси Z (для корпуса лодки)
// profiles: массив (count) профилей, каждый — ring массив точек (по 2*ring-2 точкам)
static void mb_hull(MB *m, const Vector3 *sections, const Vector3 *scales, int sectionCount,
                    int ring, float uvScale) {
    // Каждое сечение — эллипс в плоскости XY на позиции sections[i] с масштабом scales[i]
    for (int i = 0; i < sectionCount - 1; i++) {
        Vector3 c0 = sections[i], s0 = scales[i];
        Vector3 c1 = sections[i + 1], s1 = scales[i + 1];
        for (int s = 0; s < ring; s++) {
            float t0 = (float)s / (float)ring * PI;          // половина эллипса (низ)
            float t1 = (float)(s + 1) / (float)ring * PI;
            // Нижняя половина корпуса и борта
            float a0 = -PI * 0.5f + t0;
            float a1 = -PI * 0.5f + t1;
            float y0 = sinf(a0), x0 = cosf(a0);
            float y1 = sinf(a1), x1 = cosf(a1);
            Vector3 p00 = { c0.x + x0 * s0.x, c0.y + y0 * s0.y, c0.z };
            Vector3 p01 = { c0.x + x1 * s0.x, c0.y + y1 * s0.y, c0.z };
            Vector3 p10 = { c1.x + x0 * s1.x, c1.y + y0 * s1.y, c1.z };
            Vector3 p11 = { c1.x + x1 * s1.x, c1.y + y1 * s1.y, c1.z };
            Vector3 n0 = Vector3Normalize((Vector3){ x0 * 0.6f, y0, 0.15f });
            Vector3 n1 = Vector3Normalize((Vector3){ x1 * 0.6f, y1, 0.15f });
            float u0 = (float)s / (float)ring * uvScale * 2.0f;
            float u1 = (float)(s + 1) / (float)ring * uvScale * 2.0f;
            float v0 = (float)i / (float)sectionCount * uvScale * 3.0f;
            float v1 = (float)(i + 1) / (float)sectionCount * uvScale * 3.0f;
            int i0 = mb_vert(m, p00, n0, u0, v0);
            int i1 = mb_vert(m, p01, n1, u1, v0);
            int i2 = mb_vert(m, p11, n1, u1, v1);
            int i3 = mb_vert(m, p10, n0, u0, v1);
            mb_tri_idx(m, i0, i1, i2);
            mb_tri_idx(m, i0, i2, i3);
        }
    }
}

// Сборка меша raylib из буфера
static Mesh mb_build(MB *m) {
    Mesh mesh = { 0 };
    mesh.vertexCount = m->vcnt;
    mesh.triangleCount = m->icnt / 3;
    mesh.vertices = (float *)malloc(sizeof(float) * 3 * m->vcnt);
    mesh.normals = (float *)malloc(sizeof(float) * 3 * m->vcnt);
    mesh.texcoords = (float *)malloc(sizeof(float) * 2 * m->vcnt);
    mesh.colors = (unsigned char *)malloc(4 * m->vcnt);
    mesh.indices = (unsigned short *)malloc(sizeof(unsigned short) * m->icnt);
    memcpy(mesh.vertices, m->pos, sizeof(float) * 3 * m->vcnt);
    memcpy(mesh.normals, m->nor, sizeof(float) * 3 * m->vcnt);
    memcpy(mesh.texcoords, m->uv, sizeof(float) * 2 * m->vcnt);
    memcpy(mesh.colors, m->col, 4 * m->vcnt);
    memcpy(mesh.indices, m->idx, sizeof(unsigned short) * m->icnt);
    UploadMesh(&mesh, false);
    return mesh;
}

#endif // TB_MB_H
