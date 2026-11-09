#version 330
// Фрагментный шейдер: направленный свет (солнце/луна), полусферический ambient,
// блики, подводная каустика, туман, AO из вершинных цветов.
in vec3 vPos;
in vec2 vUv;
in vec3 vNormal;
in vec4 vColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform int uMode;          // 0 — обычный, 1 — рельеф (детальный шум), 2 — свечение
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambientColor;
uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float waterLevel;
uniform float uTime;
uniform vec3 uTint;
uniform float uEmissive;
uniform float uGloss;

out vec4 finalColor;

float causticPattern(vec2 p, float t) {
    float c = 0.0;
    c += sin(p.x * 0.62 + t * 0.37) * sin(p.y * 0.71 - t * 0.29);
    c += sin((p.x + p.y) * 0.41 + t * 0.23) * 0.75;
    c += sin((p.x * 0.9 - p.y * 0.6) * 0.83 - t * 0.33) * 0.5;
    c = clamp(c * 0.42 + 0.5, 0.0, 1.0);
    return pow(c, 3.4);
}

void main() {
    vec3 N = normalize(vNormal);
    vec4 tex = texture(texture0, vUv);
    vec3 albedo;
    float ao = clamp(vColor.a, 0.0, 1.0);

    if (uMode == 1) {
        // Рельеф: цвет из вершинных цветов + мелкая детализация
        albedo = vColor.rgb * (0.72 + 0.55 * tex.r);
    } else {
        albedo = tex.rgb * colDiffuse.rgb * vColor.rgb;
    }
    albedo *= uTint;

    vec3 L = normalize(sunDir);
    vec3 V = normalize(cameraPos - vPos);
    float ndl = max(dot(N, L), 0.0);

    // Полусферический рассеянный свет: небо сверху, отражение от земли снизу
    vec3 skyAmb = ambientColor * 1.25;
    vec3 groundAmb = ambientColor * vec3(0.62, 0.58, 0.48) * 0.7;
    vec3 hemi = mix(groundAmb, skyAmb, N.y * 0.5 + 0.5);

    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 42.0) * uGloss * ndl;

    // Мягкий контровой свет (rim) для силуэтов
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.16;

    vec3 color = albedo * (sunColor * ndl + hemi) + sunColor * spec + sunColor * rim * albedo;
    color *= mix(0.62, 1.0, ao);

    // Подводное освещение и каустика
    if (vPos.y < waterLevel && uMode != 2) {
        float depth = waterLevel - vPos.y;
        float caus = causticPattern(vPos.xz, uTime);
        float atten = clamp(1.0 - depth * 0.055, 0.12, 1.0);
        color *= 1.0 + caus * 0.85 * atten;
        vec3 waterShade = vec3(0.055, 0.235, 0.285);
        color = mix(color, color * waterShade * 2.2 + waterShade * 0.16, clamp(depth * 0.075, 0.0, 0.85));
    }

    color += albedo * uEmissive;

    // Туман/дымка
    float dist = length(cameraPos - vPos);
    float fog = 1.0 - exp(-fogDensity * dist * 1.15);
    fog = clamp(fog, 0.0, 0.9);
    color = mix(color, fogColor, fog);

    float alpha = 1.0;
    if (uMode == 2) {
        alpha = tex.a * colDiffuse.a;
        color = albedo * (ambientColor + sunColor * 0.35);
    }
    finalColor = vec4(color, alpha);
}
