#version 330
in vec3 vPos;
in vec2 vUv;
in vec2 vDepth;
in vec3 vNormal;

uniform sampler2D texture0;      // карта нормалей воды
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 skyColor;
uniform vec3 shallowColor;
uniform vec3 deepColor;
uniform vec3 foamColor;
uniform vec3 cameraPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float uTime;
uniform float uWind;
uniform float uFoam;

out vec4 finalColor;

void main() {
    float depth = vDepth.x;
    float shore = vDepth.y;

    // Нормали: волны + две прокручивающиеся карты нормалей
    vec3 n1 = texture(texture0, vUv * 1.7 + vec2(uTime * 0.011, uTime * 0.017)).rgb * 2.0 - 1.0;
    vec3 n2 = texture(texture0, vUv * 3.9 - vec2(uTime * 0.019, uTime * 0.009)).rgb * 2.0 - 1.0;
    vec3 detail = normalize(vec3((n1.x + n2.x) * 0.55, 1.0, (n1.z + n2.z) * 0.55));
    vec3 N = normalize(vNormal + vec3(detail.x, 0.0, detail.z) * (0.35 + uWind * 0.5));
    vec3 V = normalize(cameraPos - vPos);

    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);

    // Глубинный градиент
    float deep = clamp(depth / 14.0, 0.0, 1.0);
    vec3 water = mix(shallowColor, deepColor, deep);

    // Пена у берега и на гребнях
    float band = smoothstep(1.9, 0.05, depth);
    float ripple = 0.5 + 0.5 * sin(vPos.x * 1.6 + vPos.z * 1.3 + uTime * 1.7);
    float foam = band * (0.45 + 0.55 * ripple) * uFoam;
    float crest = smoothstep(0.16, 0.42, vNormal.y) * 0.25 * uWind;
    foam = clamp(foam + crest * ripple, 0.0, 1.0);

    vec3 L = normalize(sunDir);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 190.0) * 2.4 * max(L.y, 0.0);

    vec3 color = mix(water, skyColor, fres * 0.72);
    color += sunColor * spec;
    color = mix(color, foamColor, foam * 0.9);

    float dist = length(cameraPos - vPos);
    float fog = 1.0 - exp(-fogDensity * dist * 1.1);
    color = mix(color, fogColor, clamp(fog, 0.0, 0.9));

    float alpha = mix(0.70, 0.97, clamp(depth / 3.5, 0.0, 1.0));
    alpha = max(alpha, foam * 0.95);
    finalColor = vec4(color, alpha);
}
