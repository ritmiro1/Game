#version 330
// Разделимое гауссово размытие + выделение ярких участков для блума
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uDir;         // шаг в UV с учётом направления
uniform float uThreshold;  // 0 — простое размытие, >0 — яркий проход
out vec4 finalColor;
void main() {
    vec4 c = texture(texture0, fragTexCoord) * 0.2270270270;
    c += texture(texture0, fragTexCoord + uDir * 1.3846153846) * 0.3162162162;
    c += texture(texture0, fragTexCoord - uDir * 1.3846153846) * 0.3162162162;
    c += texture(texture0, fragTexCoord + uDir * 3.2307692308) * 0.0702702703;
    c += texture(texture0, fragTexCoord - uDir * 3.2307692308) * 0.0702702703;
    if (uThreshold > 0.0) {
        float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
        float k = smoothstep(uThreshold, uThreshold + 0.35, lum);
        c.rgb *= k;
    }
    finalColor = vec4(c.rgb, 1.0);
}
