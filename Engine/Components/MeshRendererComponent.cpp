#include "MeshRendererComponent.hpp"
#include <vector>
#include "Frustum.hpp"
#include "Matrix.hpp"
#include "Mesh.hpp"
#include "GfxDevice.hpp"
#include "Material.hpp"
#include "VertexBuffer.hpp"
#include "Shader.hpp"
#include "System.hpp"
#include "SubMesh.hpp"
#include "Vec3.hpp"

using namespace ae3d;

namespace GfxDeviceGlobal
{
    extern PerObjectUboStruct perObjectUboStruct;
}

namespace MathUtil
{
    void GetMinMax( const Vec3* aPoints, int count, Vec3& outMin, Vec3& outMax );
    void GetCorners( const Vec3& min, const Vec3& max, Vec3 outCorners[ 8 ] );
}

std::vector< ae3d::MeshRendererComponent > meshRendererComponents;
unsigned nextFreeMeshRendererComponent = 0;

unsigned ae3d::MeshRendererComponent::New()
{
    if (nextFreeMeshRendererComponent == meshRendererComponents.size())
    {
        meshRendererComponents.resize( meshRendererComponents.size() + 10 );
    }
    
    return nextFreeMeshRendererComponent++;
}

ae3d::MeshRendererComponent* ae3d::MeshRendererComponent::Get( unsigned index )
{
    return &meshRendererComponents[ index ];
}

std::string ae3d::MeshRendererComponent::GetSerialized() const
{
    return "meshrenderer\n\n";
}

void ae3d::MeshRendererComponent::Cull( const class Frustum& cameraFrustum, const struct Matrix44& localToWorld )
{
    if (!mesh)
    {
        return;
    }

    isCulled = false;
    
    Vec3 aabbWorld[ 8 ];
    MathUtil::GetCorners( mesh->GetAABBMin(), mesh->GetAABBMax(), aabbWorld );
    
    for (std::size_t v = 0; v < 8; ++v)
    {
        Matrix44::TransformPoint( aabbWorld[ v ], localToWorld, &aabbWorld[ v ] );
    }
    
    Vec3 aabbMinWorld, aabbMaxWorld;
    MathUtil::GetMinMax( aabbWorld, 8, aabbMinWorld, aabbMaxWorld );
    
    if (!cameraFrustum.BoxInFrustum( aabbMinWorld, aabbMaxWorld ))
    {
        isCulled = true;
        return;
    }

    std::vector< SubMesh >& subMeshes = mesh->GetSubMeshes();

    for (std::size_t subMeshIndex = 0; subMeshIndex < subMeshes.size(); ++subMeshIndex)
    {
        isSubMeshCulled[ subMeshIndex ] = false;

        if (materials[ subMeshIndex ] == nullptr || !materials[ subMeshIndex ]->IsValidShader())
        {
            isSubMeshCulled[ subMeshIndex ] = true;
            continue;
        }
        
        Vec3 meshAabbMinWorld = subMeshes[ subMeshIndex ].aabbMin;
        Vec3 meshAabbMaxWorld = subMeshes[ subMeshIndex ].aabbMax;
        
        Vec3 meshAabbWorld[ 8 ];
        MathUtil::GetCorners( meshAabbMinWorld, meshAabbMaxWorld, meshAabbWorld );
        
        for (std::size_t v = 0; v < 8; ++v)
        {
            Matrix44::TransformPoint( meshAabbWorld[ v ], localToWorld, &meshAabbWorld[ v ] );
        }
        
        MathUtil::GetMinMax( meshAabbWorld, 8, meshAabbMinWorld, meshAabbMaxWorld );
        
        if (!cameraFrustum.BoxInFrustum( meshAabbMinWorld, meshAabbMaxWorld ))
        {
            isSubMeshCulled[ subMeshIndex ] = true;
        }
    }
}

void ae3d::MeshRendererComponent::Render( const Matrix44& localToView, const Matrix44& localToClip, const Matrix44& localToWorld,
                                          const Matrix44& shadowView, const Matrix44& shadowProjection, Shader* overrideShader, RenderType renderType )
{
    if (isCulled || !mesh || !isEnabled)
    {
        return;
    }
    
    std::vector< SubMesh >& subMeshes = mesh->GetSubMeshes();

    for (std::size_t subMeshIndex = 0; subMeshIndex < subMeshes.size(); ++subMeshIndex)
    {
        if (isSubMeshCulled[ subMeshIndex ])
        {
            continue;
        }
        
        if (materials[ subMeshIndex ]->GetBlendingMode() != Material::BlendingMode::Off && renderType == RenderType::Opaque)
        {
            continue;
        }

        if (materials[ subMeshIndex ]->GetBlendingMode() == Material::BlendingMode::Off && renderType == RenderType::Transparent)
        {
            continue;
        }

        Shader* shader = overrideShader ? overrideShader : materials[ subMeshIndex ]->GetShader();
        GfxDevice::CullMode cullMode = GfxDevice::CullMode::Back;
        GfxDevice::BlendMode blendMode = GfxDevice::BlendMode::Off;

        if (overrideShader)
        {
            shader->Use();
            GfxDeviceGlobal::perObjectUboStruct.localToClip = localToClip;
            GfxDeviceGlobal::perObjectUboStruct.localToView = localToView;
        }
        else
        {
            Matrix44 localToShadowClip;
            
            Matrix44::Multiply( localToWorld, shadowView, localToShadowClip );
            Matrix44::Multiply( localToShadowClip, shadowProjection, localToShadowClip );
#ifndef RENDERER_METAL
            Matrix44::Multiply( localToShadowClip, Matrix44::bias, localToShadowClip );
#endif
            materials[ subMeshIndex ]->Apply();
            
            GfxDeviceGlobal::perObjectUboStruct.localToClip = localToClip;
            GfxDeviceGlobal::perObjectUboStruct.localToView = localToView;
            GfxDeviceGlobal::perObjectUboStruct.localToWorld = localToWorld;
            GfxDeviceGlobal::perObjectUboStruct.localToShadowClip = localToShadowClip;
            
            if (!subMeshes[ subMeshIndex ].joints.empty())
            {
                std::vector< Matrix44 > bones( subMeshes[ subMeshIndex ].joints.size() );

                for (size_t j = 0; j < bones.size(); ++j)
                {
                    if (!subMeshes[ subMeshIndex ].joints[ j ].animTransforms.empty())
                    {
                        const std::size_t frames = subMeshes[ subMeshIndex ].joints[ j ].animTransforms.size();
                        Matrix44::Multiply( subMeshes[ subMeshIndex ].joints[ j ].globalBindposeInverse,
                                           subMeshes[ subMeshIndex ].joints[ j ].animTransforms[ animFrame % frames ],
                                           bones[ j ] );
                    }
                }
#ifndef RENDERER_VULKAN
                // FIXME: Disabled on Vulkan backend because uniform code is not complete and this would overwrite MVP.
#ifdef RENDERER_OPENGL
                materials[ subMeshIndex ]->GetShader()->SetMatrixArray( "boneMatrices[0]", &bones[ 0 ].m[ 0 ], (int)bones.size() );
#else
                materials[ subMeshIndex ]->GetShader()->SetMatrixArray( "boneMatrices", &bones[ 0 ].m[ 0 ], (int)bones.size() );
#endif
#endif
            }

            if (!materials[ subMeshIndex ]->IsBackFaceCulled())
            {
                cullMode = GfxDevice::CullMode::Off;
            }
            
            if (materials[ subMeshIndex ]->GetBlendingMode() == Material::BlendingMode::Alpha)
            {
                blendMode = GfxDevice::BlendMode::AlphaBlend;
            }
        }
        
        GfxDevice::DepthFunc depthFunc;
        
        if (materials[ subMeshIndex ]->GetDepthFunction() == Material::DepthFunction::LessOrEqualWriteOn)
        {
            depthFunc = GfxDevice::DepthFunc::LessOrEqualWriteOn;
        }
        else if (materials[ subMeshIndex ]->GetDepthFunction() == Material::DepthFunction::NoneWriteOff)
        {
            depthFunc = GfxDevice::DepthFunc::NoneWriteOff;
        }
        else
        {
            System::Assert( false, "material has unhandled depth function" );
            depthFunc = GfxDevice::DepthFunc::NoneWriteOff;
        }
        
        GfxDevice::Draw( subMeshes[ subMeshIndex ].vertexBuffer, 0, subMeshes[ subMeshIndex ].vertexBuffer.GetFaceCount() / 3,
                         *shader, blendMode, depthFunc, cullMode, isWireframe ? GfxDevice::FillMode::Wireframe : GfxDevice::FillMode::Solid, GfxDevice::PrimitiveTopology::Triangles );
    }
}

void ae3d::MeshRendererComponent::SetMaterial( Material* material, int subMeshIndex )
{
    if (subMeshIndex >= 0 && subMeshIndex < int( materials.size() ))
    {
        materials[ subMeshIndex ] = material;
    }
}

void ae3d::MeshRendererComponent::SetMesh( Mesh* aMesh )
{
    mesh = aMesh;

    if (mesh != nullptr)
    {
        materials.resize( mesh->GetSubMeshes().size() );
        isSubMeshCulled.resize( mesh->GetSubMeshes().size() );
    }
}
