#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 cameraPos;
out vec3 vDir;
void main() {
    vec3 wp = vec3(matModel * vec4(vertexPosition, 1.0));
    vDir = normalize(wp - cameraPos);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
    gl_Position.z = gl_Position.w * 0.999999;
}
