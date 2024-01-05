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
    return texture ( uBuffer, vec2 ( frequency / 512.0, 0 ) ).x;
}

float getWeight ( float f )
{
    return ( getAmp ( f - 2.0 ) + getAmp ( f - 1.0 ) + getAmp ( f + 2.0 ) + getAmp ( f + 1.0 ) + getAmp ( f ) ) / 5.0;
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

        uv.y += ( 0.2 * sin ( uv.x + i / 7.0 - uTime * 0.6 ) );
        float Y = uv.y + getWeight ( squared ( i ) * 20.0 ) *
            ( texture ( uBuffer, vec2 ( uvTrue.x, 0 ) ).y - 0.5 );
        lineIntensity = 0.4 + squared ( 1.6 * abs ( mod ( uvTrue.x + i / 1.3 + uTime, 2.0 ) - 1.0 ) );
        lineIntensity *= 0.5;
        glowWidth = abs ( lineIntensity / ( 150.0 * Y ) );
        glowWidth *= 0.7;
        color += vec3 ( glowWidth * ( 2.0 + sin ( uTime * 0.13 ) ), glowWidth * ( 2.0 - sin ( uTime * 0.23 ) ), glowWidth * ( 2.0 - cos ( uTime * 0.19 ) ) );
    }

    finalColor = vec4 ( color, 1.0 );
}