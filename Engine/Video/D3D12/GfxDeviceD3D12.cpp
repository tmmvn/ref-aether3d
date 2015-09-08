#include "GfxDevice.hpp"
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dx12.h>
#include <vector>
#include <unordered_map>
#include <string>
#include "System.hpp"
#include "RenderTexture.hpp"
#include "Shader.hpp"
#include "VertexBuffer.hpp"
#include "CommandListManager.hpp"

#define AE3D_SAFE_RELEASE(x) if (x) { x->Release(); x = nullptr; }
#define AE3D_CHECK_D3D(x, msg) if (x != S_OK) { ae3d::System::Assert( false, msg ); }

void DestroyVertexBuffers(); // Defined in VertexBufferD3D12.cpp
void DestroyShaders(); // Defined in ShaderD3D12.cpp
void DestroyTextures(); // Defined in Texture2D_D3D12.cpp

namespace WindowGlobal
{
    extern HWND hwnd;
}

namespace GfxDeviceGlobal
{
    const unsigned BufferCount = 2;
    int drawCalls = 0;
    int vaoBinds = 0;
    int textureBinds = 0;
    int backBufferWidth = 640;
    int backBufferHeight = 400;
    ID3D12Device* device = nullptr;
    IDXGISwapChain3* swapChain = nullptr;
    ID3D12Resource* renderTargets[ 2 ] = { nullptr, nullptr };
    ID3D12Resource* depthTexture = nullptr;
    ID3D12CommandAllocator* commandListAllocator = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    unsigned frameIndex = 0;
    ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
    ID3D12DescriptorHeap* dsvDescriptorHeap = nullptr;
    ID3D12DescriptorHeap* samplerDescriptorHeap = nullptr;
    float clearColor[ 4 ] = { 0, 0, 0, 1 };
    std::unordered_map< std::string, ID3D12PipelineState* > psoCache;
    CommandListManager commandListManager;
    // FIXME: This is related to texturing and shader constant buffers, so try to move somewhere else.
    ID3D12DescriptorHeap* descHeapCbvSrvUav = nullptr;
}

void setResourceBarrier( ID3D12GraphicsCommandList* commandList, ID3D12Resource* res,
                         D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after )
{
    D3D12_RESOURCE_BARRIER desc = {};
    desc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    desc.Transition.pResource = res;
    desc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    desc.Transition.StateBefore = before;
    desc.Transition.StateAfter = after;
    desc.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    commandList->ResourceBarrier( 1, &desc );
}
namespace ae3d
{
    void CreateRenderer( int samples );
}

void CreateDescriptorHeap()
{
    for (int i = 0; i < GfxDeviceGlobal::BufferCount; ++i)
    {
        const HRESULT hr = GfxDeviceGlobal::swapChain->GetBuffer( i, IID_PPV_ARGS( &GfxDeviceGlobal::renderTargets[ i ] ) );
        AE3D_CHECK_D3D( hr, "Failed to create RTV" );

        GfxDeviceGlobal::renderTargets[ i ]->SetName( L"SwapChain_Buffer" );
        GfxDeviceGlobal::backBufferWidth = int( GfxDeviceGlobal::renderTargets[ i ]->GetDesc().Width );
        GfxDeviceGlobal::backBufferHeight = int( GfxDeviceGlobal::renderTargets[ i ]->GetDesc().Height );
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    desc.NumDescriptors = 10;
    desc.NodeMask = 0;
    HRESULT hr = GfxDeviceGlobal::device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &GfxDeviceGlobal::rtvDescriptorHeap ) );
    AE3D_CHECK_D3D( hr, "Failed to create RTV descriptor heap" );

    auto rtvStep = GfxDeviceGlobal::device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    for (auto i = 0u; i < GfxDeviceGlobal::BufferCount; ++i)
    {
        auto d = GfxDeviceGlobal::rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        d.ptr += i * rtvStep;
        GfxDeviceGlobal::device->CreateRenderTargetView( GfxDeviceGlobal::renderTargets[ i ], nullptr, d );
    }

    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 100;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    hr = GfxDeviceGlobal::device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &GfxDeviceGlobal::descHeapCbvSrvUav ) );
    AE3D_CHECK_D3D( hr, "Failed to create shader descriptor heap" );
}

void CreateSampler()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    desc.NumDescriptors = 100;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    HRESULT hr = GfxDeviceGlobal::device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &GfxDeviceGlobal::samplerDescriptorHeap ) );
    AE3D_CHECK_D3D( hr, "Failed to create sampler descriptor heap" );

    D3D12_SAMPLER_DESC samplerDesc;
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MinLOD = -FLT_MAX;
    samplerDesc.MaxLOD = FLT_MAX;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 0;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    GfxDeviceGlobal::device->CreateSampler( &samplerDesc, GfxDeviceGlobal::samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart() );
}

void CreateRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE descRange1[ 2 ];
    descRange1[ 0 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0 );
    descRange1[ 1 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 );

    CD3DX12_DESCRIPTOR_RANGE descRange2[ 1 ];
    descRange2[ 0 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0 );

    CD3DX12_ROOT_PARAMETER rootParam[ 2 ];
    rootParam[ 0 ].InitAsDescriptorTable( 2, descRange1 );
    rootParam[ 1 ].InitAsDescriptorTable( 1, descRange2, D3D12_SHADER_VISIBILITY_PIXEL );

    ID3DBlob* pOutBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;
    D3D12_ROOT_SIGNATURE_DESC descRootSignature;
    descRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    descRootSignature.NumParameters = 2;
    descRootSignature.NumStaticSamplers = 0;
    descRootSignature.pParameters = rootParam;
    descRootSignature.pStaticSamplers = nullptr;

    HRESULT hr = D3D12SerializeRootSignature( &descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, &pOutBlob, &pErrorBlob );
    AE3D_CHECK_D3D( hr, "Failed to serialize root signature" );

    hr = GfxDeviceGlobal::device->CreateRootSignature( 0, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(), IID_PPV_ARGS( &GfxDeviceGlobal::rootSignature ) );
    AE3D_CHECK_D3D( hr, "Failed to create root signature" );
}

std::string GetPSOHash( ae3d::VertexBuffer& vertexBuffer, ae3d::Shader& shader, ae3d::GfxDevice::BlendMode blendMode, ae3d::GfxDevice::DepthFunc depthFunc )
{
    std::string hashString;
    hashString += std::to_string( (ptrdiff_t)&vertexBuffer );
    hashString += std::to_string( (ptrdiff_t)&shader.blobShaderVertex );
    hashString += std::to_string( (ptrdiff_t)&shader.blobShaderPixel );
    hashString += std::to_string( (unsigned)blendMode );
    hashString += std::to_string( ((unsigned)depthFunc) + 4 );
    return hashString;
}

void CreatePSO( ae3d::VertexBuffer& vertexBuffer, ae3d::Shader& shader, ae3d::GfxDevice::BlendMode blendMode, ae3d::GfxDevice::DepthFunc depthFunc )
{
    D3D12_RASTERIZER_DESC descRaster;
    ZeroMemory( &descRaster, sizeof( descRaster ) );
    descRaster.CullMode = D3D12_CULL_MODE_BACK;
    descRaster.DepthBias = 0;
    descRaster.DepthBiasClamp = 0;
    descRaster.DepthClipEnable = TRUE;
    descRaster.FillMode = D3D12_FILL_MODE_SOLID;
    descRaster.FrontCounterClockwise = FALSE;
    descRaster.MultisampleEnable = FALSE;
    descRaster.SlopeScaledDepthBias = 0;

    D3D12_BLEND_DESC descBlend = {};
    descBlend.AlphaToCoverageEnable = FALSE;
    descBlend.IndependentBlendEnable = FALSE;

    const D3D12_RENDER_TARGET_BLEND_DESC blendOff =
    {
        FALSE, FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };

    D3D12_RENDER_TARGET_BLEND_DESC blendAlpha;
    ZeroMemory( &blendAlpha, sizeof( D3D12_RENDER_TARGET_BLEND_DESC ) );
    blendAlpha.BlendEnable = TRUE;
    blendAlpha.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendAlpha.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendAlpha.SrcBlendAlpha = D3D12_BLEND_ONE;
    blendAlpha.DestBlendAlpha = D3D12_BLEND_ZERO;
    blendAlpha.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendAlpha.BlendOp = D3D12_BLEND_OP_ADD;
    blendAlpha.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RENDER_TARGET_BLEND_DESC blendAdd;
    ZeroMemory( &blendAdd, sizeof( D3D12_RENDER_TARGET_BLEND_DESC ) );
    blendAdd.BlendEnable = TRUE;
    blendAdd.SrcBlend = D3D12_BLEND_ONE;
    blendAdd.DestBlend = D3D12_BLEND_ONE;
    blendAdd.SrcBlendAlpha = D3D12_BLEND_ONE;
    blendAdd.DestBlendAlpha = D3D12_BLEND_ONE;
    blendAdd.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendAdd.BlendOp = D3D12_BLEND_OP_ADD;
    blendAdd.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    if (blendMode == ae3d::GfxDevice::BlendMode::Off)
    {
        descBlend.RenderTarget[ 0 ] = blendOff;
    }
    else if (blendMode == ae3d::GfxDevice::BlendMode::AlphaBlend)
    {
        descBlend.RenderTarget[ 0 ] = blendAlpha;
    }
    else if (blendMode == ae3d::GfxDevice::BlendMode::Additive)
    {
        descBlend.RenderTarget[ 0 ] = blendAdd;
    }
    else
    {
        ae3d::System::Assert( false, "unhandled blend mode" );
    }

    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    
    const UINT numElements = sizeof( layout ) / sizeof( layout[ 0 ] );

    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPso;
    ZeroMemory( &descPso, sizeof( descPso ) );
    descPso.InputLayout = { layout, numElements };
    descPso.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    descPso.pRootSignature = GfxDeviceGlobal::rootSignature;
    descPso.VS = { reinterpret_cast<BYTE*>(shader.blobShaderVertex->GetBufferPointer()), shader.blobShaderVertex->GetBufferSize() };
    descPso.PS = { reinterpret_cast<BYTE*>(shader.blobShaderPixel->GetBufferPointer()), shader.blobShaderPixel->GetBufferSize() };
    descPso.RasterizerState = descRaster;
    descPso.BlendState = descBlend;
    descPso.DepthStencilState.StencilEnable = FALSE;
    descPso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    
    if (depthFunc == ae3d::GfxDevice::DepthFunc::LessOrEqualWriteOff)
    {
        descPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        descPso.DepthStencilState.DepthEnable = TRUE;
    }
    else if (depthFunc == ae3d::GfxDevice::DepthFunc::LessOrEqualWriteOn)
    {
        descPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        descPso.DepthStencilState.DepthEnable = TRUE;
    }
    else if (depthFunc == ae3d::GfxDevice::DepthFunc::NoneWriteOff)
    {
        descPso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        descPso.DepthStencilState.DepthEnable = FALSE;
    }
    else
    {
        ae3d::System::Assert( false, "unhandled depth mode" );
    }

    descPso.SampleMask = UINT_MAX;
    descPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    descPso.NumRenderTargets = 1;
    descPso.RTVFormats[ 0 ] = DXGI_FORMAT_R8G8B8A8_UNORM;
    descPso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    descPso.SampleDesc.Count = 1;

    const std::string hash = GetPSOHash( vertexBuffer, shader, blendMode, depthFunc );
    ID3D12PipelineState* pso;
    HRESULT hr = GfxDeviceGlobal::device->CreateGraphicsPipelineState( &descPso, IID_PPV_ARGS( &pso ) );
    AE3D_CHECK_D3D( hr, "Failed to create PSO" );

    GfxDeviceGlobal::psoCache[ hash ] = pso;
}

void CreateDepthStencilView()
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    desc.NumDescriptors = 10;
    desc.NodeMask = 0;
    HRESULT hr = GfxDeviceGlobal::device->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &GfxDeviceGlobal::dsvDescriptorHeap ) );
    AE3D_CHECK_D3D( hr, "Failed to create depth-stencil descriptor heap" );

    auto resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32_TYPELESS, GfxDeviceGlobal::backBufferWidth, GfxDeviceGlobal::backBufferHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
        D3D12_TEXTURE_LAYOUT_UNKNOWN, 0 );

    auto prop = CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT );

    D3D12_CLEAR_VALUE dsvClearValue;
    dsvClearValue.Format = DXGI_FORMAT_D32_FLOAT;
    dsvClearValue.DepthStencil.Depth = 1.0f;
    dsvClearValue.DepthStencil.Stencil = 0;
    hr = GfxDeviceGlobal::device->CreateCommittedResource(
        &prop, // No need to read/write by CPU
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &dsvClearValue,
        IID_PPV_ARGS( &GfxDeviceGlobal::depthTexture ) );
    AE3D_CHECK_D3D( hr, "Failed to create depth texture" );

    GfxDeviceGlobal::depthTexture->SetName( L"DepthTexture" );

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.Texture2D.MipSlice = 0;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    GfxDeviceGlobal::device->CreateDepthStencilView( GfxDeviceGlobal::depthTexture, &dsvDesc, GfxDeviceGlobal::dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() );
}

void ae3d::CreateRenderer( int /*samples*/ )
{
#ifdef DEBUG
    ID3D12Debug* debugController;
    const HRESULT dhr = D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) );
    if (dhr == S_OK)
    {
        debugController->EnableDebugLayer();
    }
    else
    {
        OutputDebugStringA( "Failed to create debug layer!\n" );
    }
#endif
    HRESULT hr = D3D12CreateDevice( nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &GfxDeviceGlobal::device ) );
    AE3D_CHECK_D3D( hr, "Failed to create D3D12 device with feature level 11.0" );
#ifdef DEBUG
    // Prevents GPU from over/underclocking to get consistent timing information.
    GfxDeviceGlobal::device->SetStablePowerState( TRUE );
#endif

    GfxDeviceGlobal::commandListManager.Create( GfxDeviceGlobal::device );
    GfxDeviceGlobal::commandListManager.CreateNewCommandList( &GfxDeviceGlobal::commandList, &GfxDeviceGlobal::commandListAllocator );

    DXGI_SWAP_CHAIN_DESC swapChainDesc{ {},{ 1, 0 }, DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, WindowGlobal::hwnd, TRUE, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH };
    ZeroMemory( &swapChainDesc.BufferDesc, sizeof( swapChainDesc.BufferDesc ) );
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    IDXGIFactory2 *dxgiFactory = nullptr;
    unsigned factoryFlags = 0;
#if DEBUG
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    hr = CreateDXGIFactory2( factoryFlags, IID_PPV_ARGS( &dxgiFactory ) );
    AE3D_CHECK_D3D( hr, "Failed to create DXGI factory" );

    hr = dxgiFactory->CreateSwapChain( GfxDeviceGlobal::commandListManager.GetCommandQueue(), &swapChainDesc, (IDXGISwapChain**)&GfxDeviceGlobal::swapChain );
    AE3D_CHECK_D3D( hr, "Failed to create swap chain" );
    dxgiFactory->Release();

    CreateDescriptorHeap();
    CreateRootSignature();
    CreateDepthStencilView();
    CreateSampler();
}

void ae3d::GfxDevice::Draw( VertexBuffer& vertexBuffer, int startFace, int endFace, Shader& shader, BlendMode blendMode, DepthFunc depthFunc )
{
    const std::string psoHash = GetPSOHash( vertexBuffer, shader, blendMode, depthFunc );
    
    if (GfxDeviceGlobal::psoCache.find( psoHash ) == std::end( GfxDeviceGlobal::psoCache ))
    {
        CreatePSO( vertexBuffer, shader, blendMode, depthFunc );
    }
    
    GfxDeviceGlobal::commandList->SetGraphicsRootSignature( GfxDeviceGlobal::rootSignature );
    ID3D12DescriptorHeap* descHeaps[] = { GfxDeviceGlobal::descHeapCbvSrvUav, GfxDeviceGlobal::samplerDescriptorHeap };
    GfxDeviceGlobal::commandList->SetDescriptorHeaps( 2, descHeaps );
    GfxDeviceGlobal::commandList->SetGraphicsRootDescriptorTable( 0, GfxDeviceGlobal::descHeapCbvSrvUav->GetGPUDescriptorHandleForHeapStart() );
    GfxDeviceGlobal::commandList->SetGraphicsRootDescriptorTable( 1, GfxDeviceGlobal::samplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart() );
    GfxDeviceGlobal::commandList->SetPipelineState( GfxDeviceGlobal::psoCache[ psoHash ] );

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    vertexBufferView.BufferLocation = vertexBuffer.GetVBResource()->GetGPUVirtualAddress();
    vertexBufferView.StrideInBytes = vertexBuffer.GetStride();
    vertexBufferView.SizeInBytes = vertexBuffer.GetIBOffset();

    D3D12_INDEX_BUFFER_VIEW indexBufferView;
    indexBufferView.BufferLocation = vertexBuffer.GetVBResource()->GetGPUVirtualAddress() + vertexBuffer.GetIBOffset();
    indexBufferView.SizeInBytes = vertexBuffer.GetIBSize();
    indexBufferView.Format = DXGI_FORMAT_R16_UINT;

    GfxDeviceGlobal::commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    GfxDeviceGlobal::commandList->IASetVertexBuffers( 0, 1, &vertexBufferView );
    GfxDeviceGlobal::commandList->IASetIndexBuffer( &indexBufferView );
    GfxDeviceGlobal::commandList->DrawIndexedInstanced( endFace - startFace, 1, startFace, 0, 0 );
}

void ae3d::GfxDevice::Init( int /*width*/, int /*height*/ )
{
}

void ae3d::GfxDevice::SetMultiSampling( bool /*enable*/ )
{
}

void ae3d::GfxDevice::IncDrawCalls()
{
    ++GfxDeviceGlobal::drawCalls;
}

void ae3d::GfxDevice::IncTextureBinds()
{
    ++GfxDeviceGlobal::textureBinds;
}

void ae3d::GfxDevice::IncVertexBufferBinds()
{
    ++GfxDeviceGlobal::vaoBinds;
}

void ae3d::GfxDevice::ResetFrameStatistics()
{
    GfxDeviceGlobal::drawCalls = 0;
    GfxDeviceGlobal::vaoBinds = 0;
    GfxDeviceGlobal::textureBinds = 0;

    // TODO: Create BeginFrame() etc.
    ++GfxDeviceGlobal::frameIndex;
}

int ae3d::GfxDevice::GetDrawCalls()
{
    return GfxDeviceGlobal::drawCalls;
}

int ae3d::GfxDevice::GetTextureBinds()
{
    return GfxDeviceGlobal::textureBinds;
}

int ae3d::GfxDevice::GetVertexBufferBinds()
{
    return GfxDeviceGlobal::vaoBinds;
}

void ae3d::GfxDevice::ReleaseGPUObjects()
{
    DestroyVertexBuffers();
    DestroyShaders();
    DestroyTextures();
    GfxDeviceGlobal::commandListManager.Destroy();
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::rtvDescriptorHeap );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::dsvDescriptorHeap );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::depthTexture );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::commandList );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::commandListAllocator );
    auto commandQueue = GfxDeviceGlobal::commandListManager.GetCommandQueue();
    AE3D_SAFE_RELEASE( commandQueue );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::descHeapCbvSrvUav );

    for (auto& pso : GfxDeviceGlobal::psoCache)
    {
        AE3D_SAFE_RELEASE( pso.second );
    }

    AE3D_SAFE_RELEASE( GfxDeviceGlobal::rootSignature );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::device );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::renderTargets[ 0 ] );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::renderTargets[ 1 ] );
    AE3D_SAFE_RELEASE( GfxDeviceGlobal::swapChain );
}

void ae3d::GfxDevice::ClearScreen( unsigned clearFlags )
{
    if (clearFlags == 0) // TODO: replace 0 with enum
    {
        return;
    }
    
    // Barrier Present -> RenderTarget
    ID3D12Resource* d3dBuffer = GfxDeviceGlobal::renderTargets[ (GfxDeviceGlobal::frameIndex - 1) % GfxDeviceGlobal::BufferCount ];
    setResourceBarrier( GfxDeviceGlobal::commandList, d3dBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );

    // Viewport
    D3D12_VIEWPORT mViewPort{ 0, 0, static_cast<float>(GfxDeviceGlobal::backBufferWidth), static_cast<float>(GfxDeviceGlobal::backBufferHeight), 0, 1 };
    GfxDeviceGlobal::commandList->RSSetViewports( 1, &mViewPort );

    D3D12_RECT scissor = {};
    scissor.right = (LONG)GfxDeviceGlobal::backBufferWidth;
    scissor.bottom = (LONG)GfxDeviceGlobal::backBufferHeight;
    GfxDeviceGlobal::commandList->RSSetScissorRects( 1, &scissor );

    D3D12_CPU_DESCRIPTOR_HANDLE descHandleRtv = GfxDeviceGlobal::rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    auto descHandleRtvStep = GfxDeviceGlobal::device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    descHandleRtv.ptr += ((GfxDeviceGlobal::frameIndex - 1) % GfxDeviceGlobal::BufferCount) * descHandleRtvStep;
    GfxDeviceGlobal::commandList->ClearRenderTargetView( descHandleRtv, GfxDeviceGlobal::clearColor, 0, nullptr );

    D3D12_CPU_DESCRIPTOR_HANDLE descHandleDsv = GfxDeviceGlobal::dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    GfxDeviceGlobal::commandList->ClearDepthStencilView( descHandleDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
    GfxDeviceGlobal::commandList->OMSetRenderTargets( 1, &descHandleRtv, TRUE, &descHandleDsv );
}

void ae3d::GfxDevice::Present()
{
    // Barrier RenderTarget -> Present
    ID3D12Resource* d3dBuffer = GfxDeviceGlobal::renderTargets[ (GfxDeviceGlobal::frameIndex - 1) % GfxDeviceGlobal::BufferCount ];
    setResourceBarrier( GfxDeviceGlobal::commandList, d3dBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );

    HRESULT hr = GfxDeviceGlobal::commandList->Close();
    AE3D_CHECK_D3D( hr, "Failed to close command list" );

    std::uint64_t fenceValue = GfxDeviceGlobal::commandListManager.ExecuteCommandList( (ID3D12CommandList*)GfxDeviceGlobal::commandList );

    hr = GfxDeviceGlobal::swapChain->Present( 1, 0 );
    if (FAILED( hr ))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED)
        {
            ae3d::System::Assert( false, "Present failed. Reason: device removed." );
        }
        else if (hr == DXGI_ERROR_DEVICE_RESET)
        {
            ae3d::System::Assert( false, "Present failed. Reason: device reset." );
        }
        else
        {
            ae3d::System::Assert( false, "Present failed. Reason: unknown." );
        }
    }

    GfxDeviceGlobal::commandListManager.WaitForFence( fenceValue );

    GfxDeviceGlobal::commandListAllocator->Reset();
    GfxDeviceGlobal::commandList->Reset( GfxDeviceGlobal::commandListAllocator, nullptr );
}

void ae3d::GfxDevice::SetBackFaceCulling( bool /*enable*/ )
{
}

void ae3d::GfxDevice::SetClearColor( float red, float green, float blue )
{
    GfxDeviceGlobal::clearColor[ 0 ] = red;
    GfxDeviceGlobal::clearColor[ 1 ] = green;
    GfxDeviceGlobal::clearColor[ 2 ] = blue;
}

void ae3d::GfxDevice::ErrorCheck(const char* info)
{
        (void)info;
#if defined _DEBUG || defined DEBUG

#endif
}

void ae3d::GfxDevice::SetRenderTarget( RenderTexture* /*target*/, unsigned cubeMapFace )
{
    cubeMapFace;
}