/* This shader is a porting of https://www.shadertoy.com/view/4ljGD1.
 * Original description:
 * Simple audio visualizer (also pretty without audio) based upon "waves" by bonniem, 
 * with added travelling pulse effect, color cycling, and of course, the requested audio sensitivity. 
 * Each wave is particularly responsive to a specific range of frequencies.
 * 
 * I tweaked it a bit so it works with this raylib setup, and my test songs.
 */


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
  float idx = remap(pos.x, 0.0, 1.0, 0.0, BUFFER_SIZE - 0.01); // Change this remapping 
  float val1 = uBuffer[int(floor(idx))];
  float val2 = uBuffer[int(ceil(idx))];
  return mix(val1, val2, idx - trunc(idx));
}

float squared(float value) {
  return value * value;
}

float getAmp(float frequency) {
  float idx = remap(frequency, 0.0, 512.0, 0.0, BUFFER_SIZE - 0.01);
  float val1 = uBuffer[int(floor(idx))];
  float val2 = uBuffer[int(ceil(idx))];
  return mix(val1, val2, idx - trunc(idx));
}

float getWeight(float f) {
  return (getAmp(f - 2.0) + getAmp(f - 1.0) + getAmp(f + 2.0) + getAmp(f + 1.0) + getAmp(f)) / 5.0;
}

void main(void) {
  vec2 uvTrue = fragTexCoord;
  vec2 uv = -1.0 + 2.0 * uvTrue;

  float lineIntensity;
  float glowWidth;
  vec3 color = vec3(0.0);

  for(float i = 0.0; i < 5.0; i++) {

    uv.y += (0.2 * sin(uv.x + i / 7.0 - uTime * 0.6));
    float Y = uv.y + getWeight(squared(i) * 20.0) * (fetch_buffer(uvTrue) - 0.5);
    lineIntensity = 0.4 + squared(1.6 * abs(mod(uvTrue.x + i / 1.3 + uTime, 2.0) - 1.0));
    lineIntensity *= 0.5;
    glowWidth = abs(lineIntensity / (150.0 * Y));
    color += vec3(glowWidth * (2.0 + sin(uTime * 0.13)), glowWidth * (2.0 - sin(uTime * 0.23)), glowWidth * (2.0 - cos(uTime * 0.19)));
  }

  finalColor = vec4(color, 1.0);
}