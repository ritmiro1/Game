#version 330
// Вершинный шейдер освещения: мировые координаты, нормали, AO в альфе цвета
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 vPos;
out vec2 vUv;
out vec3 vNormal;
out vec4 vColor;

void main() {
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    vPos = wp.xyz;
    vNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    vUv = vertexTexCoord;
    vColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
