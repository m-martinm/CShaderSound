#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform sampler2D uBuffer;

void main ( )
{
    vec2 uv = fragTexCoord;

    const float bands = 30.0;
    const float segs = 40.0;
    vec2 p;
    p.x = floor ( uv.x * bands ) / bands;
    p.y = floor ( uv.y * segs ) / segs;

    // read frequency data from first row of texture
    float fft = texture ( uBuffer, vec2 ( p.x, 0.0 ) ).x;

    // led color
    vec3 color = mix ( vec3 ( 0.0, 2.0, 0.0 ), vec3 ( 2.0, 0.0, 0.0 ), sqrt ( uv.y ) );

    // mask for bar graph
    float mask = ( p.y < fft ) ? 1.0 : 0.1;

    // led shape
    vec2 d = fract ( ( uv - p ) * vec2 ( bands, segs ) ) - 0.5;
    float led = smoothstep ( 0.5, 0.35, abs ( d.x ) ) * smoothstep ( 0.5, 0.35, abs ( d.y ) );
    vec3 ledColor = led * color * mask;

    // output final color
    finalColor = vec4 ( ledColor, 1.0 );
}