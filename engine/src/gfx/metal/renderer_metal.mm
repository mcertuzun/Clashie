#include "renderer_metal.h"

#ifdef PEBBLE_PLATFORM_MACOS

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <Cocoa/Cocoa.h>
#include "pebble/platform/platform.h"
#include <vector>
#include <cstring>

// Forward declaration from window_cocoa.mm
struct CocoaWindowData {
    NSWindow* window;
    MTKView* metalView;
    id<MTLDevice> device;
    void* appDelegate;
    void* windowDelegate;
    bool shouldClose;
};

// Metal shader source (MSL)
static const char* SPRITE_SHADER_SRC = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texcoord [[attribute(1)]];
    uchar4 color    [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texcoord;
    float4 color;
};

struct Uniforms {
    float4x4 projection;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                              constant Uniforms& uniforms [[buffer(1)]]) {
    VertexOut out;
    out.position = uniforms.projection * float4(in.position, 0.0, 1.0);
    out.texcoord = in.texcoord;
    out.color = float4(in.color) / 255.0;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler samp [[sampler(0)]]) {
    float4 texColor = tex.sample(samp, in.texcoord);
    return texColor * in.color;
}

fragment float4 fragment_notex(VertexOut in [[stage_in]]) {
    return in.color;
}
)";

namespace pebble::gfx {

struct TextureEntry {
    id<MTLTexture> texture;
    bool active;
};

struct MetalRenderer::Impl {
    CocoaWindowData* window_data = nullptr;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> command_queue = nil;
    id<MTLRenderPipelineState> pipeline_textured = nil;
    id<MTLRenderPipelineState> pipeline_untextured = nil;
    id<MTLSamplerState> sampler = nil;
    id<MTLBuffer> vertex_buffer = nil;
    id<MTLBuffer> index_buffer = nil;
    id<MTLBuffer> uniform_buffer = nil;

    // White 1x1 fallback texture
    id<MTLTexture> white_texture = nil;

    id<MTLCommandBuffer> current_cmd = nil;
    id<MTLRenderCommandEncoder> current_encoder = nil;
    id<CAMetalDrawable> current_drawable = nil;
    NSAutoreleasePool* frame_pool = nil;

    std::vector<TextureEntry> textures;
    uint32_t next_texture_id = 1;

    float projection[16];
};

MetalRenderer::~MetalRenderer() {
    shutdown();
}

bool MetalRenderer::init(const RendererConfig& config) {
    m_width = config.width;
    m_height = config.height;

    m_impl = new Impl();
    m_impl->window_data = reinterpret_cast<CocoaWindowData*>(config.window);

    if (!m_impl->window_data) {
        pebble::platform::log_error("MetalRenderer: invalid window handle");
        return false;
    }

    m_impl->device = m_impl->window_data->device;
    m_impl->command_queue = [m_impl->device newCommandQueue];

    // Compile shaders
    NSError* error = nil;
    NSString* src = [NSString stringWithUTF8String:SPRITE_SHADER_SRC];
    id<MTLLibrary> library = [m_impl->device newLibraryWithSource:src options:nil error:&error];
    if (!library) {
        pebble::platform::log_error("Metal shader compilation failed: %s",
            [[error localizedDescription] UTF8String]);
        return false;
    }

    id<MTLFunction> vert_fn = [library newFunctionWithName:@"vertex_main"];
    id<MTLFunction> frag_tex_fn = [library newFunctionWithName:@"fragment_main"];
    id<MTLFunction> frag_notex_fn = [library newFunctionWithName:@"fragment_notex"];

    // Vertex descriptor
    MTLVertexDescriptor* vd = [[MTLVertexDescriptor alloc] init];
    // position: float2
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    // texcoord: float2
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = 8;
    vd.attributes[1].bufferIndex = 0;
    // color: uchar4
    vd.attributes[2].format = MTLVertexFormatUChar4;
    vd.attributes[2].offset = 16;
    vd.attributes[2].bufferIndex = 0;
    // stride
    vd.layouts[0].stride = sizeof(SpriteVertex);
    vd.layouts[0].stepRate = 1;
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    // Textured pipeline
    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = vert_fn;
    pd.fragmentFunction = frag_tex_fn;
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    m_impl->pipeline_textured = [m_impl->device newRenderPipelineStateWithDescriptor:pd error:&error];
    if (!m_impl->pipeline_textured) {
        pebble::platform::log_error("Metal pipeline (textured) creation failed");
        return false;
    }

    // Untextured pipeline
    pd.fragmentFunction = frag_notex_fn;
    m_impl->pipeline_untextured = [m_impl->device newRenderPipelineStateWithDescriptor:pd error:&error];

    // Sampler
    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterNearest;
    sd.magFilter = MTLSamplerMinMagFilterNearest;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    m_impl->sampler = [m_impl->device newSamplerStateWithDescriptor:sd];

    // Vertex/index buffers (pre-allocated for max batch size)
    size_t vb_size = 4096 * 4 * sizeof(SpriteVertex);  // 4096 sprites × 4 verts
    size_t ib_size = 4096 * 6 * sizeof(uint16_t);       // 4096 sprites × 6 indices
    m_impl->vertex_buffer = [m_impl->device newBufferWithLength:vb_size
                                                        options:MTLResourceStorageModeShared];
    m_impl->index_buffer = [m_impl->device newBufferWithLength:ib_size
                                                       options:MTLResourceStorageModeShared];
    m_impl->uniform_buffer = [m_impl->device newBufferWithLength:sizeof(float) * 16
                                                         options:MTLResourceStorageModeShared];

    // Create 1x1 white fallback texture
    uint8_t white[] = { 255, 255, 255, 255 };
    TextureID white_id = texture_create(1, 1, white);
    if (white_id > 0 && white_id <= m_impl->textures.size()) {
        m_impl->white_texture = m_impl->textures[white_id - 1].texture;
    }

    pebble::platform::log_info("Metal renderer initialized (%dx%d)", m_width, m_height);
    return true;
}

void MetalRenderer::shutdown() {
    if (m_impl) {
        delete m_impl;
        m_impl = nullptr;
    }
}

void MetalRenderer::begin_frame(float r, float g, float b, float a) {
    m_impl->frame_pool = [[NSAutoreleasePool alloc] init];

    MTKView* view = m_impl->window_data->metalView;

    // Get drawable from the underlying CAMetalLayer directly
    // This is non-blocking if a drawable is available
    CAMetalLayer* layer = (CAMetalLayer*)[view layer];
    layer.displaySyncEnabled = YES; // VSync
    m_impl->current_drawable = [layer nextDrawable];
    if (!m_impl->current_drawable) return;

    // Build render pass descriptor manually (don't rely on MTKView's)
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = [m_impl->current_drawable texture];
    pass.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, a);
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    m_impl->current_cmd = [m_impl->command_queue commandBuffer];
    m_impl->current_encoder = [m_impl->current_cmd renderCommandEncoderWithDescriptor:pass];

    CGSize size = layer.drawableSize;
    m_width = static_cast<int32_t>(size.width);
    m_height = static_cast<int32_t>(size.height);

    MTLViewport vp;
    vp.originX = 0; vp.originY = 0;
    vp.width = static_cast<double>(m_width);
    vp.height = static_cast<double>(m_height);
    vp.znear = 0; vp.zfar = 1;
    [m_impl->current_encoder setViewport:vp];
}

void MetalRenderer::end_frame() {
    if (m_impl->current_encoder) {
        [m_impl->current_encoder endEncoding];
        m_impl->current_encoder = nil;
    }

    if (m_impl->current_drawable && m_impl->current_cmd) {
        // Present with frame pacing — syncs to display refresh rate
        [m_impl->current_cmd presentDrawable:m_impl->current_drawable];
        [m_impl->current_cmd commit];
        // Wait for GPU to finish before reusing buffers (simple sync)
        [m_impl->current_cmd waitUntilCompleted];
    }

    m_impl->current_cmd = nil;
    m_impl->current_drawable = nil;

    if (m_impl->frame_pool) {
        [m_impl->frame_pool drain];
        m_impl->frame_pool = nil;
    }
}

TextureID MetalRenderer::texture_create(int32_t width, int32_t height, const uint8_t* rgba_data) {
    @autoreleasepool {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:width height:height mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;

        id<MTLTexture> tex = [m_impl->device newTextureWithDescriptor:desc];
        if (!tex) return 0;

        [tex replaceRegion:MTLRegionMake2D(0, 0, width, height)
               mipmapLevel:0
                 withBytes:rgba_data
               bytesPerRow:width * 4];

        TextureID id = m_impl->next_texture_id++;
        while (m_impl->textures.size() < id) {
            m_impl->textures.push_back({ nil, false });
        }
        m_impl->textures[id - 1] = { tex, true };
        return id;
    }
}

void MetalRenderer::texture_destroy(TextureID id) {
    if (id == 0 || id > m_impl->textures.size()) return;
    m_impl->textures[id - 1].texture = nil;
    m_impl->textures[id - 1].active = false;
}

void MetalRenderer::upload_batch_data(const SpriteVertex* vertices, uint32_t vertex_count,
                                       const uint16_t* indices, uint32_t index_count) {
    if (!m_impl->current_encoder || vertex_count == 0) return;
    memcpy([m_impl->vertex_buffer contents], vertices, vertex_count * sizeof(SpriteVertex));
    memcpy([m_impl->index_buffer contents], indices, index_count * sizeof(uint16_t));

    // Bind vertex + uniform buffers once
    [m_impl->current_encoder setVertexBuffer:m_impl->vertex_buffer offset:0 atIndex:0];
    [m_impl->current_encoder setVertexBuffer:m_impl->uniform_buffer offset:0 atIndex:1];
}

void MetalRenderer::draw_batch(uint32_t index_offset, uint32_t index_count, TextureID texture) {
    if (!m_impl->current_encoder || index_count == 0) return;

    id<MTLTexture> mtl_tex = m_impl->white_texture;
    bool has_texture = false;
    if (texture > 0 && texture <= m_impl->textures.size() && m_impl->textures[texture - 1].active) {
        mtl_tex = m_impl->textures[texture - 1].texture;
        has_texture = true;
    }

    if (has_texture) {
        [m_impl->current_encoder setRenderPipelineState:m_impl->pipeline_textured];
        [m_impl->current_encoder setFragmentTexture:mtl_tex atIndex:0];
        [m_impl->current_encoder setFragmentSamplerState:m_impl->sampler atIndex:0];
    } else {
        [m_impl->current_encoder setRenderPipelineState:m_impl->pipeline_untextured];
    }

    [m_impl->current_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:index_count
                                         indexType:MTLIndexTypeUInt16
                                       indexBuffer:m_impl->index_buffer
                                 indexBufferOffset:index_offset * sizeof(uint16_t)];
}

void MetalRenderer::set_projection(const float* matrix) {
    memcpy([m_impl->uniform_buffer contents], matrix, sizeof(float) * 16);
}

// Factory
Renderer* create_renderer() {
    return new MetalRenderer();
}

} // namespace pebble::gfx

#endif // PEBBLE_PLATFORM_MACOS
