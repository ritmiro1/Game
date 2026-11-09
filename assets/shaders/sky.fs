#version 330
in vec3 vDir;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 skyTop;
uniform vec3 skyHorizon;
uniform vec3 moonDir;
uniform float cloudAmount;
uniform float nightAmount;
uniform float uTime;

out vec4 finalColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += vnoise(p) * a; p *= 2.03; a *= 0.5; }
    return s;
}

void main() {
    vec3 d = normalize(vDir);
    float up = clamp(d.y, -1.0, 1.0);

    // Основной градиент неба
    vec3 col = mix(skyHorizon, skyTop, pow(clamp(up * 1.15 + 0.06, 0.0, 1.0), 0.62));

    // Солнце и его ореол
    vec3 L = normalize(sunDir);
    float sd = max(dot(d, L), 0.0);
    float disc = smoothstep(0.9992, 0.9997, sd);
    float glow = pow(sd, 26.0) * 0.55 + pow(sd, 5.0) * 0.16;
    col += sunColor * (disc * 4.0 + glow) * (1.0 - cloudAmount * 0.55);

    // Луна и звёзды
    vec3 M = normalize(moonDir);
    float md = max(dot(d, M), 0.0);
    float moonDisc = smoothstep(0.9994, 0.9998, md);
    col += vec3(0.85, 0.90, 1.0) * moonDisc * nightAmount * 1.6;
    col += vec3(0.30, 0.36, 0.52) * pow(md, 60.0) * nightAmount * 0.35;

    if (nightAmount > 0.02 && up > -0.02) {
        vec2 sp = d.xz / max(up + 0.25, 0.08);
        float st = hash21(floor(sp * 220.0));
        float twinkle = 0.6 + 0.4 * sin(uTime * 2.3 + st * 40.0);
        float stars = smoothstep(0.9975, 1.0, st) * twinkle;
        col += vec3(0.85, 0.90, 1.0) * stars * nightAmount * 1.2 * smoothstep(-0.02, 0.35, up);
    }

    // Облака: слой на сфере
    if (up > 0.01 && cloudAmount > 0.01) {
        vec2 cuv = d.xz / (up + 0.22);
        float t = uTime * 0.004;
        float c = fbm(cuv * 1.35 + vec2(t, t * 0.6));
        c = c * 0.72 + fbm(cuv * 3.1 - vec2(t * 1.7, t)) * 0.28;
        float cover = smoothstep(0.52 - cloudAmount * 0.34, 0.86, c);
        float shade = smoothstep(0.30, 0.95, c);
        vec3 cloudCol = mix(skyHorizon * 0.86, sunColor * 1.05 + 0.18, 0.35 + 0.5 * shade);
        cloudCol = mix(cloudCol, skyTop * 0.75, nightAmount * 0.7);
        col = mix(col, cloudCol, cover * clamp(cloudAmount * 1.15, 0.0, 0.96) * smoothstep(0.01, 0.16, up));
    }

    // Дымка у горизонта
    float haze = pow(1.0 - clamp(up, 0.0, 1.0), 7.0);
    col = mix(col, skyHorizon, haze * 0.65);
    if (up < 0.0) col = mix(col, skyHorizon * 0.72, clamp(-up * 2.2, 0.0, 1.0));

    finalColor = vec4(col, 1.0);
}
