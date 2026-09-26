// C++ app to draw a gray circle, using wgpu-native (https://github.com/gfx-rs/wgpu-native)
// as the WebGPU implementation and GLFW (+ glfw3webgpu) for the window/surface.
//
// Adaptado do exemplo do triângulo: em vez de 3 vértices fixos, geramos um
// "leque de triângulos" (triangle fan) aproximando um círculo, usando um
// vértice central + N vértices na borda, e um buffer de índices para não
// duplicar o vértice central N vezes.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu.h>

namespace {

constexpr float PI = 3.14159265358979323846f;

struct AdapterRequest {
    WGPUAdapter adapter = nullptr;
};

struct DeviceRequest {
    WGPUDevice device = nullptr;
};

void handle_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                             WGPUStringView message, void *userdata1, void * /*userdata2*/) {
    if (status == WGPURequestAdapterStatus_Success) {
        static_cast<AdapterRequest *>(userdata1)->adapter = adapter;
    } else {
        std::fprintf(stderr, "request_adapter failed: %.*s\n", (int)message.length, message.data);
    }
}

void handle_request_device(WGPURequestDeviceStatus status, WGPUDevice device,
                            WGPUStringView message, void *userdata1, void * /*userdata2*/) {
    if (status == WGPURequestDeviceStatus_Success) {
        static_cast<DeviceRequest *>(userdata1)->device = device;
    } else {
        std::fprintf(stderr, "request_device failed: %.*s\n", (int)message.length, message.data);
    }
}

WGPUBuffer create_buffer_with_data(WGPUDevice device, const void *data, size_t size,
                                    WGPUBufferUsage usage) {
    WGPUBufferDescriptor descriptor = {};
    descriptor.usage = usage;
    descriptor.size = size;
    descriptor.mappedAtCreation = true;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &descriptor);
    void *mapped_range = wgpuBufferGetMappedRange(buffer, 0, size);
    std::memcpy(mapped_range, data, size);
    wgpuBufferUnmap(buffer);
    return buffer;
}

// Gera a geometria de um círculo como "leque de triângulos": um vértice
// central (índice 0) + N vértices igualmente espaçados na borda (índices
// 1..N). Os índices descrevem os triângulos (centro, borda[i], borda[i+1]),
// evitando duplicar o vértice central N vezes como um triangle fan "cru" faria.
constexpr int kCircleSegments = 64; // mais segmentos = borda mais suave

std::vector<float> make_circle_vertices(float radius) {
    std::vector<float> coords;
    coords.reserve((kCircleSegments + 2) * 2);

    // vértice central
    coords.push_back(0.0f);
    coords.push_back(0.0f);

    // vértices da borda
    for (int i = 0; i <= kCircleSegments; ++i) {
        float angle = 2.0f * PI * static_cast<float>(i) / kCircleSegments;
        coords.push_back(radius * std::cos(angle));
        coords.push_back(radius * std::sin(angle));
    }
    return coords;
}

std::vector<uint16_t> make_circle_indices() {
    std::vector<uint16_t> indices;
    indices.reserve(kCircleSegments * 3);

    for (int i = 0; i < kCircleSegments; ++i) {
        indices.push_back(0);                                   // centro
        indices.push_back(static_cast<uint16_t>(i + 1));         // borda[i]
        indices.push_back(static_cast<uint16_t>(i + 2));         // borda[i+1]
    }
    return indices;
}

} // namespace

int main() {
    // 1. Window/canvas
    if (!glfwInit()) {
        std::fprintf(stderr, "failed to initialize GLFW\n");
        std::exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(680, 680, "WebGPU circle", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "failed to create window\n");
        std::exit(EXIT_FAILURE);
    }

    WGPUInstance instance = wgpuCreateInstance(nullptr);
    WGPUSurface surface = glfwCreateWindowWGPUSurface(instance, window);

    // 2. Adapter and device
    WGPURequestAdapterOptions adapter_options = {};
    adapter_options.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter_options.compatibleSurface = surface;

    AdapterRequest adapter_request;
    WGPURequestAdapterCallbackInfo adapter_callback_info = {};
    adapter_callback_info.mode = WGPUCallbackMode_AllowProcessEvents;
    adapter_callback_info.callback = handle_request_adapter;
    adapter_callback_info.userdata1 = &adapter_request;
    wgpuInstanceRequestAdapter(instance, &adapter_options, adapter_callback_info);
    WGPUAdapter adapter = adapter_request.adapter;

    DeviceRequest device_request;
    WGPURequestDeviceCallbackInfo device_callback_info = {};
    device_callback_info.mode = WGPUCallbackMode_AllowProcessEvents;
    device_callback_info.callback = handle_request_device;
    device_callback_info.userdata1 = &device_request;
    wgpuAdapterRequestDevice(adapter, nullptr, device_callback_info);
    WGPUDevice device = device_request.device;

    // 3. Configure canvas
    WGPUSurfaceCapabilities surface_capabilities = {};
    wgpuSurfaceGetCapabilities(surface, adapter, &surface_capabilities);
    WGPUTextureFormat texture_format = surface_capabilities.formats[0];

    WGPUSurfaceConfiguration surface_config = {};
    surface_config.device = device;
    surface_config.format = texture_format;
    surface_config.usage = WGPUTextureUsage_RenderAttachment;
    surface_config.presentMode = WGPUPresentMode_Fifo;
    surface_config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        surface_config.width = static_cast<uint32_t>(width);
        surface_config.height = static_cast<uint32_t>(height);
    }
    wgpuSurfaceConfigure(surface, &surface_config);

    // 4. Vertex data: só posições agora (a cor cinza é fixa no fragment shader,
    // então não precisamos de um buffer de cor por vértice)
    std::vector<float> coords = make_circle_vertices(0.7f);
    std::vector<uint16_t> indices = make_circle_indices();

    WGPUBuffer coord_buffer = create_buffer_with_data(
        device, coords.data(), coords.size() * sizeof(float), WGPUBufferUsage_Vertex);

    // buffer de índices: usage Index em vez de Vertex
    WGPUBuffer index_buffer = create_buffer_with_data(
        device, indices.data(), indices.size() * sizeof(uint16_t), WGPUBufferUsage_Index);
    uint32_t index_count = static_cast<uint32_t>(indices.size());

    // 5. Shaders
    // Sem atributo de cor por vértice: a cor cinza é uma constante no
    // fragment shader.
    const char *shader_code = R"(
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vertex_main(@location(0) position: vec2<f32>) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(position, 0.0, 1.0);
    return output;
}

@fragment
fn fragment_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(0.5, 0.5, 0.5, 1.0); // cinza
}
)";

    WGPUShaderSourceWGSL wgsl_source = {};
    wgsl_source.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_source.code = {shader_code, WGPU_STRLEN};

    WGPUShaderModuleDescriptor shader_descriptor = {};
    shader_descriptor.nextInChain = &wgsl_source.chain;
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_descriptor);

    // 6. Pipeline (immutable)
    WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = {};
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_descriptor);

    // apenas um buffer de vértice agora (posição)
    WGPUVertexAttribute coord_attribute = {};
    coord_attribute.shaderLocation = 0;
    coord_attribute.offset = 0;
    coord_attribute.format = WGPUVertexFormat_Float32x2;

    WGPUVertexBufferLayout vertex_buffer_layout = {};
    vertex_buffer_layout.arrayStride = 2 * 4; // dois floats
    vertex_buffer_layout.stepMode = WGPUVertexStepMode_Vertex;
    vertex_buffer_layout.attributeCount = 1;
    vertex_buffer_layout.attributes = &coord_attribute;

    WGPUColorTargetState color_target = {};
    color_target.format = texture_format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment_state = {};
    fragment_state.module = shader;
    fragment_state.entryPoint = {"fragment_main", WGPU_STRLEN};
    fragment_state.targetCount = 1;
    fragment_state.targets = &color_target;

    WGPURenderPipelineDescriptor pipeline_descriptor = {};
    pipeline_descriptor.layout = pipeline_layout;
    pipeline_descriptor.vertex.module = shader;
    pipeline_descriptor.vertex.entryPoint = {"vertex_main", WGPU_STRLEN};
    pipeline_descriptor.vertex.bufferCount = 1;
    pipeline_descriptor.vertex.buffers = &vertex_buffer_layout;
    pipeline_descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeline_descriptor.primitive.frontFace = WGPUFrontFace_CCW;
    pipeline_descriptor.primitive.cullMode = WGPUCullMode_None;
    pipeline_descriptor.multisample.count = 1;
    pipeline_descriptor.multisample.mask = 0xFFFFFFFF;
    pipeline_descriptor.fragment = &fragment_state;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pipeline_descriptor);

    // 7. Event handling
    glfwSetKeyCallback(window, [](GLFWwindow * /*window*/, int key, int /*scancode*/, int action, int /*mods*/) {
        if (action == GLFW_PRESS) {
            std::printf("%d\n", key);
        }
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow *window, int button, int action, int /*mods*/) {
        if (action == GLFW_PRESS) {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            std::printf("%f %f %d\n", x, y, button);
        }
    });

    // 8. Render one frame
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    auto draw = [&]() {
        WGPUSurfaceTexture surface_texture = {};
        wgpuSurfaceGetCurrentTexture(surface, &surface_texture);
        WGPUTextureView view = wgpuTextureCreateView(surface_texture.texture, nullptr);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

        WGPURenderPassColorAttachment color_attachment = {};
        color_attachment.view = view;
        color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_attachment.loadOp = WGPULoadOp_Clear;
        color_attachment.storeOp = WGPUStoreOp_Store;
        color_attachment.clearValue = {1.0, 1.0, 1.0, 1.0}; // fundo branco

        WGPURenderPassDescriptor render_pass_descriptor = {};
        render_pass_descriptor.colorAttachmentCount = 1;
        render_pass_descriptor.colorAttachments = &color_attachment;

        WGPURenderPassEncoder render_pass = wgpuCommandEncoderBeginRenderPass(encoder, &render_pass_descriptor);

        wgpuRenderPassEncoderSetPipeline(render_pass, pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(render_pass, 0, coord_buffer, 0, WGPU_WHOLE_SIZE);
        // buffer de índices: formato Uint16 porque usamos uint16_t nos índices
        wgpuRenderPassEncoderSetIndexBuffer(render_pass, index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        // DrawIndexed em vez de Draw: desenha index_count índices, começando do índice 0
        wgpuRenderPassEncoderDrawIndexed(render_pass, index_count, 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(render_pass);
        wgpuRenderPassEncoderRelease(render_pass);

        WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(queue, 1, &command_buffer);
        wgpuSurfacePresent(surface);

        wgpuCommandBufferRelease(command_buffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(surface_texture.texture);
    };

    bool needs_redraw = true;
    glfwSetWindowUserPointer(window, &needs_redraw);
    glfwSetWindowRefreshCallback(window, [](GLFWwindow *window) {
        *static_cast<bool *>(glfwGetWindowUserPointer(window)) = true;
    });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow *window, int width, int height) {
        if (width == 0 || height == 0) {
            return;
        }
        *static_cast<bool *>(glfwGetWindowUserPointer(window)) = true;
    });

    while (!glfwWindowShouldClose(window)) {
        glfwWaitEvents();

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) {
            continue;
        }
        if (static_cast<uint32_t>(width) != surface_config.width ||
            static_cast<uint32_t>(height) != surface_config.height) {
            surface_config.width = static_cast<uint32_t>(width);
            surface_config.height = static_cast<uint32_t>(height);
            wgpuSurfaceConfigure(surface, &surface_config);
            needs_redraw = true;
        }

        if (needs_redraw) {
            needs_redraw = false;
            draw();
        }
    }

    wgpuRenderPipelineRelease(pipeline);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader);
    wgpuBufferRelease(index_buffer);
    wgpuBufferRelease(coord_buffer);
    wgpuSurfaceCapabilitiesFreeMembers(surface_capabilities);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuSurfaceRelease(surface);
    glfwDestroyWindow(window);
    wgpuInstanceRelease(instance);
    glfwTerminate();
    return 0;
}