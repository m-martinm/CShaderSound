#version 330
#define BUFFER_SIZE 512 // Should be equal with the c file

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uBuffer[BUFFER_SIZE];

float remap(float value, float omin, float omax, float nmin, float nmax) {
  return nmin + (value - omin) * (nmax - nmin) / (omax - omin);
}

float fetch_buffer(const vec2 pos) {
  float idx = remap(pos.x, -1.0, 1.0, 0.0, BUFFER_SIZE - 0.1);
  float val1 = uBuffer[int(floor(idx))];
  float val2 = uBuffer[int(ceil(idx))];
  return mix(val1, val2, idx - trunc(idx));
}

vec3 palette(float t) {
  vec3 a = vec3(1.000, 0.500, 0.500);
  vec3 b = vec3(0.500, 0.500, 0.500);
  vec3 c = vec3(0.750, 1.000, 0.667);
  vec3 d = vec3(0.800, 1.000, 0.333);
  return a + b * cos(6.28318 * (c * t + d));
}

const float glow = 0.03;

void main(void) {
  vec2 uv = 2.0 * (fragTexCoord - vec2(0.5));
  uv.y = -uv.y;
  uv.x *= uResolution.x/uResolution.y;

  float amplitude = fetch_buffer(uv);
  float d = smoothstep(0.3, 0.0, abs(length(uv) - amplitude));
  vec3 col = palette(amplitude) * d;
  finalColor = vec4(vec3(d), 1.0) * vec4(col, 1.0);
}