#version 330

#define ITER 30.0
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform sampler2D uBuffer;

float sdShape ( in vec2 p, in vec2 a, in vec2 b )
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp ( dot ( pa, ba ) / dot ( ba, ba ), 0.0, 1.0 );
    float segment = length ( pa - ba * h );
    float s = smoothstep ( 0.0, 0.005, segment - 0.005 );
    float c = smoothstep ( 0.0, 0.005, length ( p - b ) - 0.03 ); // circle
    return min ( s, c );
}

vec3 palette ( in float t )
{
    vec3 a = vec3 ( 0.611, 0.498, 0.650 );
    vec3 b = vec3 ( 0.388, 0.498, 0.350 );
    vec3 c = vec3 ( 0.530, 0.498, 0.620 );
    vec3 d = vec3 ( 3.438, 3.012, 4.025 );
    return a + b * cos ( 6.28318 * ( c * t + d ) );
}

void main ( void )
{
    vec2 uv = fragTexCoord; // 0..1
    float asp = uResolution.x / uResolution.y; // aspect ratio of screen
    uv.x *= asp;

    float d = 1.0;
    float s = asp / ITER;
    for ( float i = 0.0; i < ITER; i ++ )
    {
        float amp = texture ( uBuffer, vec2 ( i / ITER, 0.0 ) ).x;
        d = min ( d, sdShape ( uv, vec2 ( i * s + s / 2.0, 0.1 ), vec2 ( i * s + s / 2.0, 0.1 +clamp(amp , 0.0, 0.9) ) ) );
    }
    vec3 col = palette ( uv.x );
    col = mix ( col, vec3 ( 0.0 ), d );
    // Output to screen
    finalColor = vec4 ( col, 1.0 );
}