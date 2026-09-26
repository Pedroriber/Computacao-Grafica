// C++ app to draw an analog clock, using wgpu-native (https://github.com/gfx-rs/wgpu-native)
// as the WebGPU implementation and GLFW (+ glfw3webgpu) for the window/surface.
//
// Face (circulo indexado) + 12 marcadores de hora (retangulos radiais fixos)
// + 3 ponteiros (retangulos, atualizados a cada frame com base na hora real,
// com resolucao de fracao de segundo para movimento fluido, sem "tick").
// Face, marcadores e ponteiros usam a MESMA pipeline (posicao + cor por
// vertice); cada um so troca os buffers antes do seu draw call.
//
// Assume janela quadrada (largura == altura), entao nao ha correcao de
// aspect ratio na geometria: coordenadas polares/radiais sao usadas
// diretamente, sem dividir nenhuma componente por largura/altura.

#include <array>
#include <chrono>
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

void release_if_not_null(WGPUBuffer buffer) {
    if (buffer) {
        wgpuBufferRelease(buffer);
    }
}

constexpr float PI = 3.14159265358979323846f;
constexpr int kCircleSegments = 64;
constexpr float kFaceRadius = 0.7f;
constexpr float kTwoPi = 2.0f * PI;

struct Geometry {
    std::vector<float> coords;   // pares (x, y)
    std::vector<uint8_t> colors; // grupos (r, g, b, a)
};

// Face do relogio: circulo indexado (leque de triangulos), com cor cinza por
// vertice.
Geometry make_face_geometry() {
    Geometry geometry;
    geometry.coords.reserve((kCircleSegments + 2) * 2);
    geometry.colors.reserve((kCircleSegments + 2) * 4);

    auto push_vertex = [&](float x, float y) {
        geometry.coords.push_back(x);
        geometry.coords.push_back(y);
        geometry.colors.push_back(230);
        geometry.colors.push_back(230);
        geometry.colors.push_back(230);
        geometry.colors.push_back(255);
    };

    push_vertex(0.0f, 0.0f);
    for (int i = 0; i <= kCircleSegments; ++i) {
        float angle = kTwoPi * static_cast<float>(i) / kCircleSegments;
        push_vertex(kFaceRadius * std::cos(angle), kFaceRadius * std::sin(angle));
    }
    return geometry;
}

std::vector<uint16_t> make_face_indices() {
    std::vector<uint16_t> indices;
    indices.reserve(kCircleSegments * 3);
    for (int i = 0; i < kCircleSegments; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<uint16_t>(i + 1));
        indices.push_back(static_cast<uint16_t>(i + 2));
    }
    return indices;
}

// Um retangulo generico alinhado com a direcao `angle` (convencao de
// relogio: sentido horario, 0 = para cima), indo do raio `r_inner` ao raio
// `r_outer` a partir do centro, com meia-largura `half_width`. Usado tanto
// para os ponteiros (r_inner = 0) quanto para os marcadores de hora
// (r_inner e r_outer proximos da borda).
void append_radial_rect(Geometry &geometry, float angle, float r_inner, float r_outer,
                         float half_width, uint8_t r, uint8_t g, uint8_t b) {
    float dir_x = std::sin(angle);
    float dir_y = std::cos(angle);
    float perp_x = dir_y;
    float perp_y = -dir_x;

    float inner_x = dir_x * r_inner;
    float inner_y = dir_y * r_inner;
    float outer_x = dir_x * r_outer;
    float outer_y = dir_y * r_outer;

    float p1x = inner_x + perp_x * half_width, p1y = inner_y + perp_y * half_width;
    float p2x = inner_x - perp_x * half_width, p2y = inner_y - perp_y * half_width;
    float p3x = outer_x - perp_x * half_width, p3y = outer_y - perp_y * half_width;
    float p4x = outer_x + perp_x * half_width, p4y = outer_y + perp_y * half_width;

    auto push_vertex = [&](float x, float y) {
        geometry.coords.push_back(x);
        geometry.coords.push_back(y);
        geometry.colors.push_back(r);
        geometry.colors.push_back(g);
        geometry.colors.push_back(b);
        geometry.colors.push_back(255);
    };

    // 2 triangulos formando o retangulo: (p1,p2,p3) e (p1,p3,p4)
    push_vertex(p1x, p1y);
    push_vertex(p2x, p2y);
    push_vertex(p3x, p3y);
    push_vertex(p1x, p1y);
    push_vertex(p3x, p3y);
    push_vertex(p4x, p4y);
}

// 12 marcadores fixos, um a cada 30 graus, proximos da borda da face.
Geometry make_markers_geometry() {
    Geometry geometry;
    for (int i = 0; i < 12; ++i) {
        float angle = kTwoPi * static_cast<float>(i) / 12.0f;
        append_radial_rect(geometry, angle, kFaceRadius - 0.08f, kFaceRadius - 0.02f,
                            0.010f, 60, 60, 60);
    }
    return geometry;
}

// Ponteiros de hora/minuto/segundo, recalculados a cada chamada com base na
// hora atual do sistema com resolucao de milissegundos, para um movimento
// continuo e suave (sem "tick" pulando de segundo em segundo).
Geometry make_hands_geometry() {
    using namespace std::chrono;

    // C++20: current_zone()/zoned_time convertem o instante UTC do
    // system_clock para a hora local (considerando fuso horário do SO),
    // substituindo std::localtime. floor<days> isola a meia-noite local do
    // dia atual, e hh_mm_ss decompõe o tempo desde a meia-noite em
    // horas/minutos/segundos (+ fração), com precisão de milissegundo aqui.
    auto local_now = zoned_time{current_zone(), system_clock::now()}.get_local_time();
    auto midnight_today = floor<days>(local_now);
    hh_mm_ss<milliseconds> time_of_day{floor<milliseconds>(local_now - midnight_today)};

    float second = static_cast<float>(time_of_day.seconds().count()) +
                   static_cast<float>(time_of_day.subseconds().count()) / 1000.0f;
    float minute = static_cast<float>(time_of_day.minutes().count()) + second / 60.0f;
    float hour = static_cast<float>(time_of_day.hours().count() % 12) + minute / 60.0f;

    float hour_angle = hour / 12.0f * kTwoPi;
    float minute_angle = minute / 60.0f * kTwoPi;
    float second_angle = second / 60.0f * kTwoPi;

    Geometry geometry;
    // ponteiro de hora: curto e grosso, preto
    append_radial_rect(geometry, hour_angle, 0.0f, 0.35f, 0.020f, 20, 20, 20);
    // ponteiro de minuto: mais longo e um pouco mais fino, preto
    append_radial_rect(geometry, minute_angle, 0.0f, 0.55f, 0.014f, 20, 20, 20);
    // ponteiro de segundo: o mais longo e fino, vermelho para destacar
    append_radial_rect(geometry, second_angle, -0.05f, 0.62f, 0.006f, 200, 0, 0);

    return geometry;
}

} // namespace

int main() {
    // 1. Window/canvas — janela quadrada, entao nenhuma correcao de aspect
    // ratio e necessaria na geometria.
    if (!glfwInit()) {
        std::fprintf(stderr, "failed to initialize GLFW\n");
        std::exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(480, 480, "WebGPU analog clock", nullptr, nullptr);
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

    // 4. Vertex data
    WGPUBuffer face_coord_buffer = nullptr;
    WGPUBuffer face_color_buffer = nullptr;
    WGPUBuffer face_index_buffer = nullptr;
    uint32_t face_index_count = 0;

    WGPUBuffer marker_coord_buffer = nullptr;
    WGPUBuffer marker_color_buffer = nullptr;
    uint32_t marker_vertex_count = 0;

    WGPUBuffer hand_coord_buffer = nullptr;
    WGPUBuffer hand_color_buffer = nullptr;
    uint32_t hand_vertex_count = 0;

    auto rebuild_face_buffers = [&]() {
        release_if_not_null(face_coord_buffer);
        release_if_not_null(face_color_buffer);
        release_if_not_null(face_index_buffer);

        Geometry geometry = make_face_geometry();
        std::vector<uint16_t> indices = make_face_indices();

        face_coord_buffer = create_buffer_with_data(
            device, geometry.coords.data(), geometry.coords.size() * sizeof(float), WGPUBufferUsage_Vertex);
        face_color_buffer = create_buffer_with_data(
            device, geometry.colors.data(), geometry.colors.size() * sizeof(uint8_t), WGPUBufferUsage_Vertex);
        face_index_buffer = create_buffer_with_data(
            device, indices.data(), indices.size() * sizeof(uint16_t), WGPUBufferUsage_Index);
        face_index_count = static_cast<uint32_t>(indices.size());
    };

    auto rebuild_marker_buffers = [&]() {
        release_if_not_null(marker_coord_buffer);
        release_if_not_null(marker_color_buffer);

        Geometry geometry = make_markers_geometry();

        marker_coord_buffer = create_buffer_with_data(
            device, geometry.coords.data(), geometry.coords.size() * sizeof(float), WGPUBufferUsage_Vertex);
        marker_color_buffer = create_buffer_with_data(
            device, geometry.colors.data(), geometry.colors.size() * sizeof(uint8_t), WGPUBufferUsage_Vertex);
        marker_vertex_count = static_cast<uint32_t>(geometry.coords.size() / 2);
    };

    auto rebuild_hand_buffers = [&]() {
        release_if_not_null(hand_coord_buffer);
        release_if_not_null(hand_color_buffer);

        Geometry geometry = make_hands_geometry();

        hand_coord_buffer = create_buffer_with_data(
            device, geometry.coords.data(), geometry.coords.size() * sizeof(float), WGPUBufferUsage_Vertex);
        hand_color_buffer = create_buffer_with_data(
            device, geometry.colors.data(), geometry.colors.size() * sizeof(uint8_t), WGPUBufferUsage_Vertex);
        hand_vertex_count = static_cast<uint32_t>(geometry.coords.size() / 2);
    };

    rebuild_face_buffers();
    rebuild_marker_buffers();
    rebuild_hand_buffers();

    // 5. Shaders
    const char *shader_code = R"(
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
};

@vertex
fn vertex_main(
      @location(0) position: vec2<f32>,
      @location(1) color: vec3<f32>,
) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(position, 0.0, 1.0);
    output.color = color;
    return output;
}

@fragment
fn fragment_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.color, 1.0);
}
)";

    WGPUShaderSourceWGSL wgsl_source = {};
    wgsl_source.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_source.code = {shader_code, WGPU_STRLEN};

    WGPUShaderModuleDescriptor shader_descriptor = {};
    shader_descriptor.nextInChain = &wgsl_source.chain;
    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shader_descriptor);

    // 6. Pipeline (immutable) — reaproveitada para face, marcadores e ponteiros
    WGPUPipelineLayoutDescriptor pipeline_layout_descriptor = {};
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_descriptor);

    WGPUVertexAttribute coord_attribute = {};
    coord_attribute.shaderLocation = 0;
    coord_attribute.offset = 0;
    coord_attribute.format = WGPUVertexFormat_Float32x2;

    WGPUVertexAttribute color_attribute = {};
    color_attribute.shaderLocation = 1;
    color_attribute.offset = 0;
    color_attribute.format = WGPUVertexFormat_Unorm8x4;

    std::array<WGPUVertexBufferLayout, 2> vertex_buffer_layouts = {};
    vertex_buffer_layouts[0].arrayStride = 2 * 4;
    vertex_buffer_layouts[0].stepMode = WGPUVertexStepMode_Vertex;
    vertex_buffer_layouts[0].attributeCount = 1;
    vertex_buffer_layouts[0].attributes = &coord_attribute;

    vertex_buffer_layouts[1].arrayStride = 4 * 1;
    vertex_buffer_layouts[1].stepMode = WGPUVertexStepMode_Vertex;
    vertex_buffer_layouts[1].attributeCount = 1;
    vertex_buffer_layouts[1].attributes = &color_attribute;

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
    pipeline_descriptor.vertex.bufferCount = vertex_buffer_layouts.size();
    pipeline_descriptor.vertex.buffers = vertex_buffer_layouts.data();
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
        color_attachment.clearValue = {1.0, 1.0, 1.0, 1.0};

        WGPURenderPassDescriptor render_pass_descriptor = {};
        render_pass_descriptor.colorAttachmentCount = 1;
        render_pass_descriptor.colorAttachments = &color_attachment;

        WGPURenderPassEncoder render_pass = wgpuCommandEncoderBeginRenderPass(encoder, &render_pass_descriptor);
        wgpuRenderPassEncoderSetPipeline(render_pass, pipeline);

        // face (indexada)
        wgpuRenderPassEncoderSetVertexBuffer(render_pass, 0, face_coord_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(render_pass, 1, face_color_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(render_pass, face_index_buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(render_pass, face_index_count, 1, 0, 0, 0);

        // marcadores de hora (nao-indexados, estaticos)
        wgpuRenderPassEncoderSetVertexBuffer(render_pass, 0, marker_coord_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(render_pass, 1, marker_color_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(render_pass, marker_vertex_count, 1, 0, 0);

        // ponteiros (nao-indexados, dinamicos)
        wgpuRenderPassEncoderSetVertexBuffer(render_pass, 0, hand_coord_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetVertexBuffer(render_pass, 1, hand_color_buffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(render_pass, hand_vertex_count, 1, 0, 0);

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

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow * /*window*/, int /*width*/, int /*height*/) {
        // nada a fazer aqui: o loop abaixo checa o tamanho a cada iteracao,
        // ja que agora redesenhamos continuamente de qualquer forma.
    });

    // Modo "continuous": diferente do exemplo original (redraw sob demanda),
    // aqui o ponteiro de segundos precisa se mover a cada frame, entao
    // usamos glfwPollEvents() (nao-bloqueante) e desenhamos em todo loop,
    // recalculando os ponteiros com a hora atual (incluindo fracao de
    // segundo) a cada iteracao — isso e o que da a fluidez sem "tick".
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

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
            // sem correcao de aspect ratio: se a janela for redimensionada
            // para um tamanho nao-quadrado, o circulo/ponteiros vao
            // distorcer, ja que a geometria nao leva mais isso em conta.
        }

        rebuild_hand_buffers();
        draw();
    }

    wgpuRenderPipelineRelease(pipeline);
    wgpuPipelineLayoutRelease(pipeline_layout);
    wgpuShaderModuleRelease(shader);
    release_if_not_null(hand_coord_buffer);
    release_if_not_null(hand_color_buffer);
    release_if_not_null(marker_coord_buffer);
    release_if_not_null(marker_color_buffer);
    release_if_not_null(face_coord_buffer);
    release_if_not_null(face_color_buffer);
    release_if_not_null(face_index_buffer);
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