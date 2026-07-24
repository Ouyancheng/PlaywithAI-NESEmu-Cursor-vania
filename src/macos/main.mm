#import <AudioToolbox/AudioToolbox.h>
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "core/Nes.hpp"

#include <array>
#include <mutex>

namespace {

class AudioQueuePlayer {
public:
    void start() {
        if (queue_) {
            return;
        }
        AudioStreamBasicDescription desc{};
        desc.mSampleRate = 44100.0;
        desc.mFormatID = kAudioFormatLinearPCM;
        desc.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        desc.mBytesPerPacket = sizeof(float);
        desc.mFramesPerPacket = 1;
        desc.mBytesPerFrame = sizeof(float);
        desc.mChannelsPerFrame = 1;
        desc.mBitsPerChannel = 32;
        AudioQueueNewOutput(&desc, &AudioQueuePlayer::callback, this, nullptr, nullptr, 0, &queue_);
        for (auto& buffer : buffers_) {
            AudioQueueAllocateBuffer(queue_, 2048 * sizeof(float), &buffer);
            fill(buffer);
            AudioQueueEnqueueBuffer(queue_, buffer, 0, nullptr);
        }
        AudioQueueStart(queue_, nullptr);
    }

    void stop() {
        if (!queue_) {
            return;
        }
        AudioQueueStop(queue_, true);
        for (auto* buffer : buffers_) {
            if (buffer) {
                AudioQueueFreeBuffer(queue_, buffer);
            }
        }
        AudioQueueDispose(queue_, true);
        queue_ = nullptr;
    }

    void push(const std::vector<float>& samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (float sample : samples) {
            if (fifoSize_ == fifo_.size()) {
                fifoRead_ = (fifoRead_ + 1) % fifo_.size();
                --fifoSize_;
            }
            fifo_[fifoWrite_] = sample;
            fifoWrite_ = (fifoWrite_ + 1) % fifo_.size();
            ++fifoSize_;
        }
    }

private:
    static void callback(void* user, AudioQueueRef, AudioQueueBufferRef buffer) {
        auto* player = static_cast<AudioQueuePlayer*>(user);
        player->fill(buffer);
        AudioQueueEnqueueBuffer(player->queue_, buffer, 0, nullptr);
    }

    void fill(AudioQueueBufferRef buffer) {
        auto* out = reinterpret_cast<float*>(buffer->mAudioData);
        constexpr int frames = 2048;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (int i = 0; i < frames; ++i) {
                if (fifoSize_ > 0) {
                    float sample = fifo_[fifoRead_];
                    fifoRead_ = (fifoRead_ + 1) % fifo_.size();
                    --fifoSize_;
                    if (underrunRecovery_ > 0) {
                        const float t = static_cast<float>(kRecoverySamples - underrunRecovery_ + 1) / static_cast<float>(kRecoverySamples);
                        sample = recoveryStart_ + (sample - recoveryStart_) * t;
                        --underrunRecovery_;
                    }
                    out[i] = sample;
                    lastSample_ = sample;
                } else {
                    lastSample_ *= 0.999f;
                    out[i] = lastSample_;
                    underrunRecovery_ = kRecoverySamples;
                    recoveryStart_ = lastSample_;
                }
            }
        }
        buffer->mAudioDataByteSize = frames * sizeof(float);
    }

    AudioQueueRef queue_ = nullptr;
    AudioQueueBufferRef buffers_[3]{};
    std::mutex mutex_;
    std::array<float, 44100> fifo_{};
    std::size_t fifoRead_ = 0;
    std::size_t fifoWrite_ = 0;
    std::size_t fifoSize_ = 0;
    float lastSample_ = 0.0f;
    static constexpr int kRecoverySamples = 64;
    int underrunRecovery_ = 0;
    float recoveryStart_ = 0.0f;
};

}

@interface GameView : MTKView <MTKViewDelegate>
- (instancetype)initWithFrame:(NSRect)frame emulator:(nes::Nes*)emulator audio:(AudioQueuePlayer*)audio;
- (void)uploadFrame;
@end

@implementation GameView {
    nes::Nes* _emulator;
    AudioQueuePlayer* _audio;
    id<MTLCommandQueue> _commandQueue;
    id<MTLTexture> _texture;
    id<MTLRenderPipelineState> _pipeline;
    id<MTLSamplerState> _sampler;
}

- (instancetype)initWithFrame:(NSRect)frame emulator:(nes::Nes*)emulator audio:(AudioQueuePlayer*)audio {
    self = [super initWithFrame:frame device:MTLCreateSystemDefaultDevice()];
    if (self) {
        _emulator = emulator;
        _audio = audio;
        self.delegate = self;
        self.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        self.framebufferOnly = NO;
        self.enableSetNeedsDisplay = NO;
        self.paused = NO;
        self.preferredFramesPerSecond = nes::kFrameRateNtsc;
        self.layer.magnificationFilter = kCAFilterNearest;
        _commandQueue = [self.device newCommandQueue];
        NSError* error = nil;
        NSString* shaderSource =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct VertexOut { float4 position [[position]]; float2 uv; };\n"
             "vertex VertexOut vertex_main(uint vid [[vertex_id]]) {\n"
             "    float2 pos[4] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0) };\n"
             "    float2 uv[4] = { float2(0.0, 1.0), float2(1.0, 1.0), float2(0.0, 0.0), float2(1.0, 0.0) };\n"
             "    VertexOut out; out.position = float4(pos[vid], 0.0, 1.0); out.uv = uv[vid]; return out;\n"
             "}\n"
             "fragment float4 fragment_main(VertexOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {\n"
             "    return tex.sample(smp, in.uv);\n"
             "}\n";
        id<MTLLibrary> library = [self.device newLibraryWithSource:shaderSource options:nil error:&error];
        if (!library) {
            NSLog(@"Could not compile Metal shaders: %@", error);
            return self;
        }
        MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDesc.vertexFunction = [library newFunctionWithName:@"vertex_main"];
        pipelineDesc.fragmentFunction = [library newFunctionWithName:@"fragment_main"];
        pipelineDesc.colorAttachments[0].pixelFormat = self.colorPixelFormat;
        _pipeline = [self.device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if (!_pipeline) {
            NSLog(@"Could not create Metal pipeline: %@", error);
            return self;
        }
        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        samplerDesc.minFilter = MTLSamplerMinMagFilterNearest;
        samplerDesc.magFilter = MTLSamplerMinMagFilterNearest;
        samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        _sampler = [self.device newSamplerStateWithDescriptor:samplerDesc];
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                        width:nes::kScreenWidth
                                                                                       height:nes::kScreenHeight
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        _texture = [self.device newTextureWithDescriptor:desc];
    }
    return self;
}

- (BOOL)acceptsFirstResponder { return YES; }

- (void)keyDown:(NSEvent*)event { [self setKey:event pressed:YES]; }
- (void)keyUp:(NSEvent*)event { [self setKey:event pressed:NO]; }

- (void)setKey:(NSEvent*)event pressed:(BOOL)pressed {
    if (!_emulator) {
        return;
    }
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@"z"]) _emulator->setButton(0, nes::Button::A, pressed);
    else if ([key isEqualToString:@"x"]) _emulator->setButton(0, nes::Button::B, pressed);
    else if ([key isEqualToString:@"a"]) _emulator->setButton(0, nes::Button::Select, pressed);
    else if ([key isEqualToString:@"s"]) _emulator->setButton(0, nes::Button::Start, pressed);
    else if (event.keyCode == 126) _emulator->setButton(0, nes::Button::Up, pressed);
    else if (event.keyCode == 125) _emulator->setButton(0, nes::Button::Down, pressed);
    else if (event.keyCode == 123) _emulator->setButton(0, nes::Button::Left, pressed);
    else if (event.keyCode == 124) _emulator->setButton(0, nes::Button::Right, pressed);
}

- (void)uploadFrame {
    const auto& fb = _emulator->framebuffer();
    [_texture replaceRegion:MTLRegionMake2D(0, 0, nes::kScreenWidth, nes::kScreenHeight)
                mipmapLevel:0
                  withBytes:fb.data()
                bytesPerRow:nes::kScreenWidth * sizeof(nes::Rgb)];
}

- (void)drawInMTKView:(MTKView*)view {
    if (!_emulator || !_emulator->hasCartridge()) {
        return;
    }
    _emulator->stepFrame();
    if (_audio) {
        _audio->push(_emulator->takeAudioSamples());
    }
    [self uploadFrame];
    id<CAMetalDrawable> drawable = view.currentDrawable;
    MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
    if (!drawable || !pass || !_pipeline) {
        return;
    }
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    const double drawableWidth = drawable.texture.width;
    const double drawableHeight = drawable.texture.height;
    const double nesAspect = static_cast<double>(nes::kScreenWidth) / static_cast<double>(nes::kScreenHeight);
    double viewportWidth = drawableWidth;
    double viewportHeight = drawableWidth / nesAspect;
    if (viewportHeight > drawableHeight) {
        viewportHeight = drawableHeight;
        viewportWidth = drawableHeight * nesAspect;
    }
    MTLViewport viewport{};
    viewport.originX = (drawableWidth - viewportWidth) * 0.5;
    viewport.originY = (drawableHeight - viewportHeight) * 0.5;
    viewport.width = viewportWidth;
    viewport.height = viewportHeight;
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    [encoder setViewport:viewport];
    [encoder setRenderPipelineState:_pipeline];
    [encoder setFragmentTexture:_texture atIndex:0];
    [encoder setFragmentSamplerState:_sampler atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow* _window;
    GameView* _view;
    nes::Nes _emulator;
    AudioQueuePlayer _audio;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    NSRect frame = NSMakeRect(0, 0, 768, 720);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"TestAiNES";
    _window.contentAspectRatio = NSMakeSize(nes::kScreenWidth, nes::kScreenHeight);
    _window.minSize = NSMakeSize(512, 480);
    _view = [[GameView alloc] initWithFrame:frame emulator:&_emulator audio:&_audio];
    _window.contentView = _view;
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:_view];
    [self buildMenu];
    _audio.start();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    _audio.stop();
}

- (void)buildMenu {
    NSMenu* menubar = [[NSMenu alloc] init];
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    [menubar addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"TestAiNES"];
    [appMenu addItemWithTitle:@"Open ROM..." action:@selector(openRom:) keyEquivalent:@"o"];
    [appMenu addItemWithTitle:@"Reset" action:@selector(resetEmulator:) keyEquivalent:@"r"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    NSApp.mainMenu = menubar;
}

- (void)openRom:(id)sender {
    (void)sender;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"nes"]];
    panel.canChooseDirectories = NO;
    if ([panel runModal] != NSModalResponseOK) {
        return;
    }
    std::string error;
    if (!_emulator.loadRom(panel.URL.path.UTF8String, error)) {
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Could not load ROM";
        alert.informativeText = [NSString stringWithUTF8String:error.c_str()];
        [alert runModal];
        return;
    }
    _window.title = [NSString stringWithFormat:@"TestAiNES - %@ (%s)", panel.URL.lastPathComponent, _emulator.mapperName().c_str()];
}

- (void)resetEmulator:(id)sender {
    (void)sender;
    _emulator.reset();
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
