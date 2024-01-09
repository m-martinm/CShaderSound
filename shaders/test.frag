#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform sampler2D uBuffer;

float sq ( float value )
{
    return value * value;
}

float get_amp ( float frequency )
{
    return texture ( uBuffer, vec2 ( frequency / 2048.0, 0 ) ).x;
}

float get_weight ( float f )
{
    float ret = 0.0;
    for ( float i = 0; i < 400.0; i += 10.0 )
    {
        ret += get_amp ( f + i );
    }
    return ret / 40.0;
}

void main ( void )
{
    vec2 uvTrue = fragTexCoord;
    vec2 uv = - 1.0 + 2.0 * uvTrue;

    float lineIntensity;
    float glowWidth;
    vec3 color = vec3 ( 0.0 );

    for ( float i = 0.0; i < 5.0; i ++ )
    {

        uv.y += ( 0.125 * sin ( uv.x + i / 5.0 - uTime * 0.5 ) );
        float Y = uv.y + get_weight ( i * 400.0 ) * ( 0.5 * texture ( uBuffer, vec2 ( uvTrue.x, 1.0 ) ).y - 0.5 );
        lineIntensity = 0.5 + sq ( abs ( mod ( uvTrue.x + i / 1.3 + uTime, 2.0 ) - 1.0 ) );
        glowWidth = abs ( lineIntensity / ( 200.0 * Y ) );
        color += vec3 ( glowWidth * ( 2.0 + sin ( uTime * 0.10 ) ), glowWidth * ( 2.0 - sin ( uTime * 0.20 ) ), glowWidth * ( 2.0 - cos ( uTime * 0.30 ) ) );
    }

    finalColor = vec4 ( color, 1.0 );
}