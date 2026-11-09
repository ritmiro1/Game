#version 330
// Вода: волны (та же формула, что и в симуляции), глубина и дистанция до берега в UV2
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;   // x = глубина дна (м), y = 0..1 близость берега
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform float uTime;
uniform float uWind;

out vec3 vPos;
out vec2 vUv;
out vec2 vDepth;
out vec3 vNormal;

float waveHeight(vec2 p, float t, float wind) {
    float w = 0.35 + wind * 0.85;
    float h = 0.0;
    h += 0.210 * w * sin(p.x * 0.0820 + t * 0.92);
    h += 0.160 * w * sin(p.y * 0.1010 - t * 1.13 + 1.37);
    h += 0.115 * w * sin((p.x + p.y) * 0.0560 + t * 0.71 + 2.11);
    h += 0.062 * w * sin((p.x * 0.92 - p.y * 0.62) * 0.1310 + t * 1.87 + 4.02);
    h += 0.034 * w * sin((p.x * 0.4 + p.y * 0.9) * 0.2210 - t * 2.55 + 0.83);
    return h;
}

void main() {
    // Меш воды строится в мировых координатах, модель — единичная матрица
    vec2 xz = vertexPosition.xz;
    float h = waveHeight(xz, uTime, uWind);
    vec3 pos = vec3(xz.x, vertexPosition.y + h, xz.y);

    float e = 1.2;
    float hL = waveHeight(xz + vec2(-e, 0.0), uTime, uWind);
    float hR = waveHeight(xz + vec2(e, 0.0), uTime, uWind);
    float hD = waveHeight(xz + vec2(0.0, -e), uTime, uWind);
    float hU = waveHeight(xz + vec2(0.0, e), uTime, uWind);
    vNormal = normalize(vec3((hL - hR) / (2.0 * e), 1.0, (hD - hU) / (2.0 * e)));

    vPos = pos;
    vUv = vertexTexCoord;
    vDepth = vertexTexCoord2;
    gl_Position = mvp * vec4(pos, 1.0);
}
