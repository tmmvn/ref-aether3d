#include <metal_stdlib>
#include <simd/simd.h>

#define TILE_RES 16
#define NUM_THREADS_PER_TILE (TILE_RES * TILE_RES)
#define LIGHT_INDEX_BUFFER_SENTINEL 0x7fffffff

using namespace metal;

struct StandardUniforms
{
    matrix_float4x4 _ModelViewProjectionMatrix;
    matrix_float4x4 _ShadowProjectionMatrix;
    matrix_float4x4 _ModelViewMatrix;
    matrix_float4x4 _ModelMatrix;
    float4 tintColor;
};
static_assert( sizeof( StandardUniforms ) < 512, "" );

struct StandardColorInOut
{
    float4 position [[position]];
    float4 projCoord;
    float2 texCoords;
    float3 positionVS;
    float3 positionWS;
    float3 tangentVS;
    float3 bitangentVS;
    float3 normalVS;
    half4  color;
};

struct StandardVertex
{
    float3 position [[attribute(0)]];
    float2 texcoord [[attribute(1)]];
    float3 normal [[attribute(3)]];
    float4 tangent [[attribute(4)]];
    float4 color [[attribute(2)]];
};

struct CullerUniforms
{
    matrix_float4x4 invProjection;
    matrix_float4x4 viewMatrix;
    uint windowWidth;
    uint windowHeight;
    uint numLights;
    int maxNumLightsPerTile;
};

static uint GetNumLightsInThisTile( uint tileIndex, uint maxNumLightsPerTile, const device uint* perTileLightIndexBuffer )
{
    uint numLightsInThisTile = 0;
    uint index = maxNumLightsPerTile * tileIndex;
    uint nextLightIndex = perTileLightIndexBuffer[ index ];

    // count point lights
    while (nextLightIndex != LIGHT_INDEX_BUFFER_SENTINEL)
    {
        ++numLightsInThisTile;
        ++index;
        nextLightIndex = perTileLightIndexBuffer[ index ];
    }

    // count spot lights
    
    // Moves past sentinel
    ++index;

    while (nextLightIndex != LIGHT_INDEX_BUFFER_SENTINEL)
    {
        ++numLightsInThisTile;
        ++index;
        nextLightIndex = perTileLightIndexBuffer[ index ];
    }

    return numLightsInThisTile;
}

static uint GetTileIndex( float2 ScreenPos, int windowWidth )
{
    float tileRes = float( TILE_RES );
    uint numCellsX = (windowWidth + TILE_RES - 1) / TILE_RES;
    uint tileIdx = uint( floor( ScreenPos.x / tileRes ) + floor( ScreenPos.y / tileRes ) * numCellsX );
    return tileIdx;
}

// calculate the number of tiles in the horizontal direction
static uint GetNumTilesX( int windowWidth )
{
    return uint(((windowWidth + TILE_RES - 1) / float(TILE_RES)));
}

// calculate the number of tiles in the vertical direction
static uint GetNumTilesY( int windowHeight )
{
    return uint(((windowHeight + TILE_RES - 1) / float(TILE_RES)));
}

float3 tangentSpaceTransform( float3 tangent, float3 bitangent, float3 normal, float3 v )
{
    return normalize( v.x * normalize( tangent ) + v.y * normalize( bitangent ) + v.z * normalize( normal ) );
}

vertex StandardColorInOut standard_vertex( StandardVertex vert [[stage_in]],
                               constant StandardUniforms& uniforms [[ buffer(5) ]],
                               unsigned int vid [[ vertex_id ]] )
{
    StandardColorInOut out;
    
    float4 in_position = float4( vert.position, 1.0 );
    out.position = uniforms._ModelViewProjectionMatrix * in_position;
    out.positionVS = (uniforms._ModelViewMatrix * in_position).xyz;
    out.positionWS = (uniforms._ModelMatrix * in_position).xyz;
    
    out.color = half4( vert.color );
    out.texCoords = vert.texcoord;
    out.projCoord = uniforms._ShadowProjectionMatrix * in_position;
    
    out.tangentVS = (uniforms._ModelViewMatrix * float4( vert.tangent.xyz, 0 )).xyz;
    float3 ct = cross( vert.normal, vert.tangent.xyz ) * vert.tangent.w;
    out.bitangentVS = normalize( uniforms._ModelViewMatrix * float4( ct, 0 ) ).xyz;
    out.normalVS = (uniforms._ModelViewMatrix * float4( vert.normal.xyz, 0 )).xyz;
    
    return out;
}

fragment float4 standard_fragment( StandardColorInOut in [[stage_in]],
                               texture2d<float, access::sample> albedoSmoothnessMap [[texture(0)]],
                               texture2d<float, access::sample> _ShadowMap [[texture(1)]],
                               texture2d<float, access::sample> normalMap [[texture(2)]],
                               texture2d<float, access::sample> specularMap [[texture(3)]],
                               constant StandardUniforms& uniforms [[ buffer(5) ]],
                               const device uint* perTileLightIndexBuffer [[ buffer(6) ]],
                               constant float* pointLightBufferCenterAndRadius [[ buffer(7) ]],
                               constant CullerUniforms& cullerUniforms  [[ buffer(8) ]],
                               sampler sampler0 [[sampler(0)]] )
{
    const float4 albedoColor = float4( albedoSmoothnessMap.sample( sampler0, in.texCoords ) );
    const float smoothness = albedoColor.a;
    const float4 normalTS = float4( normalMap.sample( sampler0, in.texCoords ) );
    const float4 specular = float4( specularMap.sample( sampler0, in.texCoords ) );
    
    const float3 normalVS = tangentSpaceTransform( in.tangentVS, in.bitangentVS, in.normalVS, normalTS.xyz );
    
    const uint tileIndex = GetTileIndex( in.position.xy, cullerUniforms.windowWidth );
    uint index = cullerUniforms.maxNumLightsPerTile * tileIndex;
    uint nextLightIndex = perTileLightIndexBuffer[ index ];

    float4 outColor = float4( 0.25, 0.25, 0.25, 1 );

    while (nextLightIndex != LIGHT_INDEX_BUFFER_SENTINEL)
    {
        uint lightIndex = nextLightIndex;
        index++;
        nextLightIndex = perTileLightIndexBuffer[ index ];

        float4 center = pointLightBufferCenterAndRadius[ lightIndex ];
        float radius = center.w;
        
        float3 vecToLightVS = (uniforms._ModelViewMatrix * float4( center.xyz, 1 )).xyz - in.positionVS.xyz;
        float3 vecToLightWS = center.xyz - in.positionWS.xyz;
        float3 lightDirVS = normalize( vecToLightVS );
        
        float lightDistance = length( vecToLightWS );
        //outColor.rgb = lightDistance < radius ? -dot( lightDirVS, normalize( in.normalVS ) ) : 0.25;
        outColor.rgb += dot( lightDirVS, normalize( in.normalVS ) );
        //outColor.rgb = lightDistance < radius ? 1 : 0.25;
        //outColor.rgb = float3(1, 0, 0 );
    }
    
    /*
     const uint numLights = GetNumLightsInThisTile( tileIndex, cullerUniforms.maxNumLightsPerTile, perTileLightIndexBuffer );

    if (numLights == 0)
    {
        outColor = float4( 0, 0, 1, 1 );
    }
    else if (numLights == 1)
    {
        //outColor = float4( 0, 1, 0, 1 );
    }
    else if (numLights == 2)
    {
        outColor = float4( 1, 1, 0, 1 );
    }
    else
    {
        outColor = float4( 1, 0, 0, 1 );
    }*/

    return albedoColor * outColor;
}
