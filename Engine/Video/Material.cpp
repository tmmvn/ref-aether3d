#include "Material.hpp"
#include "GfxDevice.hpp"
#include "Texture2D.hpp"
#include "RenderTexture.hpp"
#include "Shader.hpp"

std::unordered_map< std::string, ae3d::RenderTexture* > ae3d::Material::sTexRTs;
std::unordered_map< std::string, ae3d::Texture2D* > ae3d::Material::sTex2ds;
std::unordered_map< std::string, float > ae3d::Material::sFloats;
std::unordered_map< std::string, int > ae3d::Material::sInts;
std::unordered_map< std::string, ae3d::Vec3 > ae3d::Material::sVec3s;

namespace GfxDeviceGlobal
{
    extern PerObjectUboStruct perObjectUboStruct;
}

bool ae3d::Material::IsValidShader() const
{
    return shader && shader->IsValid();
}

void ae3d::Material::Apply()
{
    if (shader == nullptr)
    {
        return;
    }

    shader->Use();

    for (const auto& i : ints)
    {
        shader->SetInt( i.first.c_str(), i.second );
    }

    for (const auto& f : floats)
    {
        shader->SetFloat( f.first.c_str(), f.second );
    }

    int texUnit = 1;
    
    for (int slot = 0; slot < TEXTURE_SLOT_COUNT; ++slot)
    {
        shader->SetTexture( "texture", tex2dSlots[ slot ], slot );
    }

    /*for (const auto& texCube : texCubes)
    {
        shader->SetTexture( texCube.first.c_str(), texCube.second, texUnit );
        ++texUnit;
        }*/

    for (const auto& texRT : texRTs)
    {
        shader->SetRenderTexture( texRT.second, texUnit );
        ++texUnit;
    }

    for (const auto& globalTexRT : sTexRTs)
    {
#if RENDERER_METAL
        if (globalTexRT.second && globalTexRT.second->IsCube())
        {
            shader->SetRenderTexture( globalTexRT.second, 2 );
        }
        else
        {
            shader->SetRenderTexture( globalTexRT.second, texUnit );
        }
#else
        shader->SetRenderTexture( globalTexRT.second, texUnit );
#endif
        ++texUnit;
    }

    for (const auto& globalTex2d : sTex2ds)
    {
        shader->SetTexture( globalTex2d.first.c_str(), globalTex2d.second, texUnit );
        ++texUnit;
    }

    for (const auto& globalFloat : sFloats)
    {
        shader->SetFloat( globalFloat.first.c_str(), globalFloat.second );
    }

    for (const auto& globalInt : sInts)
    {
        shader->SetInt( globalInt.first.c_str(), globalInt.second );
    }

    for (const auto& globalVec3 : sVec3s)
    {
        shader->SetVector3( globalVec3.first.c_str(), &globalVec3.second.x );
    }

    if (depthUnits > 0.0001f || depthUnits < -0.0001f || depthFactor > 0.0001f || depthFactor < -0.0001f)
    {
        GfxDevice::SetPolygonOffset( true, depthFactor, depthUnits );
    }
    else
    {
        GfxDevice::SetPolygonOffset( false, 0, 0 );
    }

    GfxDeviceGlobal::perObjectUboStruct.f0 = f0;
}

void ae3d::Material::SetShader( Shader* aShader )
{
    shader = aShader;
}

void ae3d::Material::SetF0( float af0 )
{
    f0 = af0;
}

void ae3d::Material::SetTexture( Texture2D* texture, int slot )
{
    if (0 <= slot && slot < TEXTURE_SLOT_COUNT)
    {
        tex2dSlots[ slot ] = texture;
    }
}

void ae3d::Material::SetTexture( TextureCube* texture, int slot )
{
    if (0 <= slot && slot < TEXTURE_SLOT_COUNT)
    {
        texCubeSlots[ slot ] = texture;
    }
}

void ae3d::Material::SetRenderTexture( const char* name, RenderTexture* renderTexture )
{
    texRTs[ name ] = renderTexture;
}

void ae3d::Material::SetGlobalRenderTexture( const char* name, RenderTexture* renderTexture )
{
    sTexRTs[ name ] = renderTexture;
}

void ae3d::Material::SetGlobalTexture2D( const char* name, Texture2D* texture2d )
{
    sTex2ds[ name ] = texture2d;
}

void ae3d::Material::SetGlobalFloat( const char* name, float value )
{
    sFloats[ name ] = value;
}

void ae3d::Material::SetGlobalInt( const char* name, int value )
{
    sInts[ name ] = value;
}

void ae3d::Material::SetGlobalVector( const char* name, const Vec3& value )
{
    sVec3s[ name ] = value;
}

void ae3d::Material::SetInt( const char* name, int value )
{
    ints[ name ] = value;
}

void ae3d::Material::SetFloat( const char* name, float value )
{
    floats[ name ] = value;
}
