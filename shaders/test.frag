#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform sampler2D uBuffer;

float squared ( float value )
{
    return value * value;
}

float getAmp ( float frequency )
{
    return texture ( uBuffer, vec2 ( frequency / 2048.0, 0 ) ).x;
}

float getWeight ( float f )
{
    float ret = 0.0;
    for ( float i = 0; i < 400.0; i += 10.0 )
    {
        ret += getAmp ( f + i );
    }
    return ret / 40.0;
}

float sdBox ( in vec2 p, in vec2 b )
{
    vec2 d = abs ( p ) - b;
    return length ( max ( d, 0.0 ) ) + min ( max ( d.x, d.y ), 0.0 );
}

void main ( void )
{
    vec2 uv0 = fragTexCoord;
    vec2 uv = 2.0 * ( uv0 - 0.5 );

    float d = sdBox ( uv, vec2 ( 1.0, 0.5 ) );
    d += texture ( uBuffer, vec2 ( uv0.x, 0.0 ) ).x * uv0.y ;//cos ( 3.14159 * uv.x / 2.0 - 3.14159 );

    d = smoothstep ( 0.0, 0.01, d );
    vec3 col = mix(vec3(0.1, 0.2, 0.3), vec3(0.3, 0.5, 0.8), d);

    finalColor = vec4 ( col, 1.0 );
}

// void main ( void )
// {
//     vec2 uvTrue = fragTexCoord;
//     vec2 uv = - 1.0 + 2.0 * uvTrue;

//     float lineIntensity;
//     float glowWidth;
//     vec3 color = vec3 ( 0.0 );

//     for ( float i = 0.0; i < 5.0; i ++ )
//     {

//         uv.y += ( 0.125 * sin ( uv.x + i / 5.0 - uTime * 0.5 ) );
//         float Y = uv.y + getWeight ( i * 400.0) * ( 0.5* texture ( uBuffer, vec2 ( uvTrue.x, 1.0 ) ).y - 0.5 );
//         lineIntensity = 0.5 + squared ( abs ( mod ( uvTrue.x + i / 1.3 + uTime, 2.0 ) - 1.0 ) );
//         glowWidth = abs ( lineIntensity / ( 200.0 * Y ) );
//         color += vec3 ( glowWidth * ( 2.0 + sin ( uTime * 0.10 ) ), glowWidth * ( 2.0 - sin ( uTime * 0.20 ) ), glowWidth * ( 2.0 - cos ( uTime * 0.30 ) ) );
//     }

//     finalColor = vec4 ( color, 1.0 );
// }