#include "LightTiler.hpp"
#include <vector>
#include "ComputeShader.hpp"
#include "RenderTexture.hpp"
#include "System.hpp"
#include "GfxDevice.hpp"
#include "VulkanUtils.hpp"
// TODO: Some of these methods can be shared with D3D12 and possibly Metal.

namespace MathUtil
{
    int Max( int x, int y );
}

namespace GfxDeviceGlobal
{
    extern int backBufferWidth;
    extern int backBufferHeight;
    extern VkDevice device;
    extern PerObjectUboStruct perObjectUboStruct;
}

namespace LightTilerGlobal
{
    std::vector< VkBuffer > buffersToReleaseAtExit;
    std::vector< VkBufferView > bufferViewsToReleaseAtExit;
    std::vector< VkDeviceMemory > memoryToReleaseAtExit;
}

namespace ae3d
{
    void GetMemoryType( std::uint32_t typeBits, VkFlags properties, std::uint32_t* typeIndex );
}

void UploadPerObjectUbo();

void ae3d::LightTiler::DestroyObjects()
{
    for (std::size_t bufferIndex = 0; bufferIndex < LightTilerGlobal::buffersToReleaseAtExit.size(); ++bufferIndex)
    {
        vkDestroyBuffer( GfxDeviceGlobal::device, LightTilerGlobal::buffersToReleaseAtExit[ bufferIndex ], nullptr );
    }

    for (std::size_t bufferIndex = 0; bufferIndex < LightTilerGlobal::buffersToReleaseAtExit.size(); ++bufferIndex)
    {
        vkDestroyBufferView( GfxDeviceGlobal::device, LightTilerGlobal::bufferViewsToReleaseAtExit[ bufferIndex ], nullptr );
    }

    for (std::size_t bufferIndex = 0; bufferIndex < LightTilerGlobal::buffersToReleaseAtExit.size(); ++bufferIndex)
    {
        vkFreeMemory( GfxDeviceGlobal::device, LightTilerGlobal::memoryToReleaseAtExit[ bufferIndex ], nullptr );
    }
}

unsigned ae3d::LightTiler::GetMaxNumLightsPerTile() const
{
    const unsigned kAdjustmentMultipier = 32;

    // I haven't tested at greater than 1080p, so cap it
    const unsigned uHeight = (GfxDeviceGlobal::backBufferHeight > 1080) ? 1080 : GfxDeviceGlobal::backBufferHeight;

    // adjust max lights per tile down as height increases
    return (MaxLightsPerTile - (kAdjustmentMultipier * (uHeight / 120)));
}

void ae3d::LightTiler::Init()
{
    pointLightCenterAndRadius.resize( MaxLights );
    spotLightCenterAndRadius.resize( MaxLights );

    // Light index buffer
    {
        const unsigned numTiles = GetNumTilesX() * GetNumTilesY();
        const unsigned maxNumLightsPerTile = GetMaxNumLightsPerTile();

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = maxNumLightsPerTile * numTiles * sizeof( unsigned );
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        VkResult err = vkCreateBuffer( GfxDeviceGlobal::device, &bufferInfo, nullptr, &perTileLightIndexBuffer );
        AE3D_CHECK_VULKAN( err, "vkCreateBuffer" );
        debug::SetObjectName( GfxDeviceGlobal::device, (std::uint64_t)perTileLightIndexBuffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "perTileLightIndexBuffer" );
        LightTilerGlobal::buffersToReleaseAtExit.push_back( perTileLightIndexBuffer );

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements( GfxDeviceGlobal::device, perTileLightIndexBuffer, &memReqs );

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.allocationSize = memReqs.size;
        GetMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &allocInfo.memoryTypeIndex );
        err = vkAllocateMemory( GfxDeviceGlobal::device, &allocInfo, nullptr, &perTileLightIndexBufferMemory );
        AE3D_CHECK_VULKAN( err, "vkAllocateMemory perTileLightIndexBuffer" );
        LightTilerGlobal::memoryToReleaseAtExit.push_back( perTileLightIndexBufferMemory );

        err = vkBindBufferMemory( GfxDeviceGlobal::device, perTileLightIndexBuffer, perTileLightIndexBufferMemory, 0 );
        AE3D_CHECK_VULKAN( err, "vkBindBufferMemory perTileLightIndexBuffer" );

        VkBufferViewCreateInfo bufferViewInfo = {};
        bufferViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        bufferViewInfo.flags = 0;
        bufferViewInfo.buffer = perTileLightIndexBuffer;
		bufferViewInfo.range = VK_WHOLE_SIZE;
		bufferViewInfo.format = VK_FORMAT_R32_UINT;

        err = vkCreateBufferView( GfxDeviceGlobal::device, &bufferViewInfo, nullptr, &perTileLightIndexBufferView );
        AE3D_CHECK_VULKAN( err, "light index buffer view" );
        LightTilerGlobal::bufferViewsToReleaseAtExit.push_back( perTileLightIndexBufferView );
    }

    // Point light buffer
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = pointLightCenterAndRadius.size() * 4 * sizeof( float );
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        VkResult err = vkCreateBuffer( GfxDeviceGlobal::device, &bufferInfo, nullptr, &pointLightCenterAndRadiusBuffer );
        AE3D_CHECK_VULKAN( err, "vkCreateBuffer" );
        debug::SetObjectName( GfxDeviceGlobal::device, (std::uint64_t)pointLightCenterAndRadiusBuffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "pointLightCenterAndRadiusBuffer" );
        LightTilerGlobal::buffersToReleaseAtExit.push_back( pointLightCenterAndRadiusBuffer );

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements( GfxDeviceGlobal::device, pointLightCenterAndRadiusBuffer, &memReqs );

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.allocationSize = memReqs.size;
        GetMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &allocInfo.memoryTypeIndex );
        err = vkAllocateMemory( GfxDeviceGlobal::device, &allocInfo, nullptr, &pointLightCenterAndRadiusMemory );
        AE3D_CHECK_VULKAN( err, "vkAllocateMemory pointLightCenterAndRadiusMemory" );
        LightTilerGlobal::memoryToReleaseAtExit.push_back( pointLightCenterAndRadiusMemory );

        err = vkBindBufferMemory( GfxDeviceGlobal::device, pointLightCenterAndRadiusBuffer, pointLightCenterAndRadiusMemory, 0 );
        AE3D_CHECK_VULKAN( err, "vkBindBufferMemory pointLightCenterAndRadiusBuffer" );

        VkBufferViewCreateInfo bufferViewInfo = {};
        bufferViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        bufferViewInfo.flags = 0;
        bufferViewInfo.buffer = pointLightCenterAndRadiusBuffer;
        bufferViewInfo.range = VK_WHOLE_SIZE;
		bufferViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;

        err = vkCreateBufferView( GfxDeviceGlobal::device, &bufferViewInfo, nullptr, &pointLightBufferView );
        AE3D_CHECK_VULKAN( err, "point light buffer view" );
        LightTilerGlobal::bufferViewsToReleaseAtExit.push_back( pointLightBufferView );
    }

    // Spot light buffer
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = spotLightCenterAndRadius.size() * 4 * sizeof( float );
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        VkResult err = vkCreateBuffer( GfxDeviceGlobal::device, &bufferInfo, nullptr, &spotLightCenterAndRadiusBuffer );
        AE3D_CHECK_VULKAN( err, "vkCreateBuffer" );
        debug::SetObjectName( GfxDeviceGlobal::device, (std::uint64_t)spotLightCenterAndRadiusBuffer, VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, "spotLightCenterAndRadiusBuffer" );
        LightTilerGlobal::buffersToReleaseAtExit.push_back( spotLightCenterAndRadiusBuffer );

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements( GfxDeviceGlobal::device, spotLightCenterAndRadiusBuffer, &memReqs );

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.allocationSize = memReqs.size;
        GetMemoryType( memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &allocInfo.memoryTypeIndex );
        err = vkAllocateMemory( GfxDeviceGlobal::device, &allocInfo, nullptr, &spotLightCenterAndRadiusMemory );
        AE3D_CHECK_VULKAN( err, "vkAllocateMemory spotLightCenterAndRadiusMemory" );
        LightTilerGlobal::memoryToReleaseAtExit.push_back( spotLightCenterAndRadiusMemory );

        err = vkBindBufferMemory( GfxDeviceGlobal::device, spotLightCenterAndRadiusBuffer, spotLightCenterAndRadiusMemory, 0 );
        AE3D_CHECK_VULKAN( err, "vkBindBufferMemory spotLightCenterAndRadiusBuffer" );

        VkBufferViewCreateInfo bufferViewInfo = {};
        bufferViewInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        bufferViewInfo.flags = 0;
        bufferViewInfo.buffer = spotLightCenterAndRadiusBuffer;
        bufferViewInfo.range = VK_WHOLE_SIZE;
		bufferViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;

        err = vkCreateBufferView( GfxDeviceGlobal::device, &bufferViewInfo, nullptr, &spotLightBufferView );
        AE3D_CHECK_VULKAN( err, "spot light buffer view" );
        LightTilerGlobal::bufferViewsToReleaseAtExit.push_back( spotLightBufferView );
    }
}

void ae3d::LightTiler::SetPointLightPositionAndRadius( int bufferIndex, Vec3& position, float radius )
{
    System::Assert( bufferIndex < MaxLights, "tried to set a too high light index" );

    if (bufferIndex < MaxLights)
    {
        activePointLights = MathUtil::Max( bufferIndex + 1, activePointLights );
        pointLightCenterAndRadius[ bufferIndex ] = Vec4( position.x, position.y, position.z, radius );
    }
}

void ae3d::LightTiler::SetSpotLightPositionAndRadius( int bufferIndex, Vec3& position, float radius )
{
    System::Assert( bufferIndex < MaxLights, "tried to set a too high light index" );

    if (bufferIndex < MaxLights)
    {
        activeSpotLights = MathUtil::Max( bufferIndex + 1, activeSpotLights );
        spotLightCenterAndRadius[ bufferIndex ] = Vec4( position.x, position.y, position.z, radius );
    }
}

void ae3d::LightTiler::UpdateLightBuffers()
{

}

unsigned ae3d::LightTiler::GetNumTilesX() const
{
    return (unsigned)((GfxDeviceGlobal::backBufferWidth + TileRes - 1) / (float)TileRes);
}

unsigned ae3d::LightTiler::GetNumTilesY() const
{
    return (unsigned)((GfxDeviceGlobal::backBufferHeight + TileRes - 1) / (float)TileRes);
}

void ae3d::LightTiler::CullLights( ComputeShader& shader, const Matrix44& projection, const Matrix44& localToView, RenderTexture& depthNormalTarget )
{
    Matrix44::Invert( projection, GfxDeviceGlobal::perObjectUboStruct.clipToView );

    GfxDeviceGlobal::perObjectUboStruct.localToView = localToView;
    GfxDeviceGlobal::perObjectUboStruct.windowWidth = depthNormalTarget.GetWidth();
    GfxDeviceGlobal::perObjectUboStruct.windowHeight = depthNormalTarget.GetHeight();
    GfxDeviceGlobal::perObjectUboStruct.numLights = (((unsigned)activeSpotLights & 0xFFFFu) << 16) | ((unsigned)activePointLights & 0xFFFFu);
    GfxDeviceGlobal::perObjectUboStruct.maxNumLightsPerTile = GetMaxNumLightsPerTile();
    
    UploadPerObjectUbo();

    cullerUniformsCreated = true;

    /*shader.SetUniformBuffer( 0, uniformBuffer );
    shader.SetTextureBuffer( 0, pointLightCenterAndRadiusBuffer );
    shader.SetTextureBuffer( 1, depthNormalTarget.GetGpuResource()->resource );
    shader.SetTextureBuffer( 2, spotLightCenterAndRadiusBuffer );
    shader.SetUAVBuffer( 0, perTileLightIndexBuffer );*/

    //shader.Dispatch( GetNumTilesX(), GetNumTilesY(), 1 );
}