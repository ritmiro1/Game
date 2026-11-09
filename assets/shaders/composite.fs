#version 330
// Финальная композиция: блум, Tilt-Shift (через размытую копию), виньетка,
// цветокоррекция, подводный тинт, вспышка молнии, зерно.
in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;      // сцена
uniform sampler2D textureBloom;  // размытые яркие участки
uniform sampler2D textureBlur;   // размытая копия сцены (для Tilt-Shift)
uniform vec2 uResolution;
uniform float uBloom;
uniform float uDof;              // сила Tilt-Shift
uniform float uVignette;
uniform float uSaturation;
uniform float uContrast;
uniform float uTime;
uniform float uUnderwater;
uniform float uFlash;
uniform vec3 uTint;

out vec4 finalColor;

void main() {
    vec2 uv = fragTexCoord;
    // Подводное искажение
    if (uUnderwater > 0.01) {
        uv += vec2(sin(uv.y * 26.0 + uTime * 1.7), cos(uv.x * 22.0 + uTime * 1.3)) * 0.0016 * uUnderwater;
    }
    vec3 col = texture(texture0, uv).rgb;

    vec3 bloom = texture(textureBloom, uv).rgb;
    col += bloom * uBloom;

    if (uDof > 0.001) {
        float d = abs(uv.y - 0.46);
        float mask = smoothstep(0.16, 0.62, d) * uDof;
        vec3 blurred = texture(textureBlur, uv).rgb;
        col = mix(col, blurred, clamp(mask, 0.0, 0.85));
    }

    // Цветокоррекция
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(vec3(lum), col, uSaturation);
    col = (col - 0.5) * uContrast + 0.5;
    col *= uTint;

    // Виньетка
    vec2 q = uv - 0.5;
    float vig = 1.0 - dot(q, q) * uVignette;
    col *= clamp(vig, 0.0, 1.0);

    // Подводный слой
    if (uUnderwater > 0.01) {
        col = mix(col, col * vec3(0.42, 0.72, 0.86) + vec3(0.02, 0.09, 0.12), uUnderwater * 0.75);
    }

    col += vec3(0.75, 0.82, 1.0) * uFlash;

    // Зерно плёнки
    float g = fract(sin(dot(uv * uResolution + uTime, vec2(12.9898, 78.233))) * 43758.5453);
    col += (g - 0.5) * 0.016;

    col = clamp(col, 0.0, 1.0);
    finalColor = vec4(col, 1.0);
}
