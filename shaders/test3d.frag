#version 330
#define MAX_STEPS 80
#define MAX_DIST 100.0
#define MIN_DIST 0.001

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform sampler2D uBuffer;

float smin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * (1.0 / 4.0);
}

mat3 rotY(float t) {
    mat3 ret;
    ret[0] = vec3(cos(t), 0.0, -sin(t));
    ret[1] = vec3(0.0, 1.0, 0.0);
    ret[2] = vec3(sin(t), 0.0, cos(t));
    return ret;
}

float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float map(vec3 p) {
    float sphere = sdSphere(p, 3);
    float bass = texture(uBuffer, vec2(0.0, 0.0)).x;
    float mid = texture(uBuffer, vec2(0.3, 0.0)).x;
    float high = texture(uBuffer, vec2(0.9, 0.0)).x;
    float box = sdBox(p - vec3(0.0, 1.0, 0.0), vec3(bass * 2.0 + 1.0, mid + 1.0, high + 1.0));
    float plane = p.y;
    return min(plane, smin(box, sphere, 5.0));
}

float raymarch(vec3 ro, vec3 rd) {
    float t = 0.0; // distance traveled

    for(int i = 0; i < 80; i++) {
        vec3 p = ro + rd * t;
        float d = map(p);
        t += d;

        if(d < 0.001 || t > 100.0)
            break;
    }
    return t;
}

vec3 get_normals(vec3 p) {
    float d = map(p);
    vec2 e = vec2(.001, 0);

    vec3 n = d - vec3(map(p - e.xyy), map(p - e.yxy), map(p - e.yyx));

    return normalize(n);
}

float lighting(vec3 p) {
    vec3 lp = vec3(2.0, 10.0, 2.0);
    lp *= rotY(uTime * 0.4);
    vec3 l = normalize(lp - p);
    vec3 n = get_normals(p);

    float diffuse = clamp(dot(l, n), 0.0, 1.0);
    float d = raymarch(p + n * MIN_DIST * 2.0, l);
    if(d < length(lp - p))
        diffuse *= .1;

    return diffuse;
}

void main() {
    // Normalized pixel coordinates (from -1 to 1)
    vec2 uv = (fragTexCoord - 0.5) * 2.0;
    uv.x *= uResolution.x / uResolution.y;

    vec3 ro = vec3(0.0, 2.0, -8.0); // ray origin
    ro *= rotY(uTime);
    vec3 rd = normalize(vec3(uv, 1)); // ray direction
    rd *= rotY(uTime);
    vec3 col = vec3(0.0);

    float t = raymarch(ro, rd);
    vec3 p = ro + rd * t;
    float diffuse = lighting(p);
    col = vec3(diffuse);
    col = pow(col, vec3(.4545));
    // Output to screen
    finalColor = vec4(col, 1.0);
}