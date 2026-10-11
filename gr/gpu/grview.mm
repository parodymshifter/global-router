// grview.mm -- a 3D congestion view of a routed design, drawn with Metal.
// An analysis tool, not a router. Input: the per-tile grid file grmap
// writes (`grmap bench.gr solution.route out.grid`): each tile's worst 2D
// edge use/capacity and its class. Every tile is a bar: its height is the
// use/capacity ratio and its color the class --
//   violet <= 50%, blue 50-75%, green 75-90%, yellow 90-99%,
//   orange exactly full, red overflowed, grey blocked.
//
//   grview <file.grid> [title]                      interactive window
//   grview <file.grid> --snapshot out.png [--yaw deg] [--pitch deg]
//          [--zoom f] [--focus x y] [--size WxH] [--hide-below c] [--hscale f]
//          [--red-lift f]
//                                                   render once, no window
//   grview --sequence frames.tsv --out-dir DIR [--video out.mp4] [--fps n] [--hold s] [camera options]
//          render every frame a router's video trace lists (pd38route PD38ROUTE_FRAMES) with one camera, to
//          DIR/fNNNN.jpg, and optionally an H.264 movie holding each frame --hold seconds (0.5) at --fps (30)
//   grview --selftest
// A GRMAP2 grid adds a per-tile activity in [0, 1] (the share of the tile's
// GCells whose routes changed since the previous frame), drawn as a glow.
// (GRVIEW_DEBUG=1 prints the snapshot camera.)
//
// Window controls: drag to rotate, scroll to zoom, Shift+drag to pan,
// + / - taller or flatter bars, H hides or shows the violet and blue bars
// (so the hot areas stand out), B toggles the red lift, R resets the view.
// Red lift: overflow is usually 1-2 units over a capacity of 60-200, so a
// red bar is only a few percent taller than a full (orange) one; by default
// red bars are drawn half a full bar's height taller so they stand out
// (B, or --red-lift 0, shows true heights).
//
// Build (Command Line Tools only; the shaders compile at run time, as in
// pattern_metal.mm, since there is no offline Metal compiler here):
//   clang++ -std=c++17 -O2 -Wall -Wextra -fobjc-arc gpu/grview.mm -o grview \
//     -framework Cocoa -framework Metal -framework MetalKit -framework ImageIO \
//     -framework CoreGraphics -framework UniformTypeIdentifiers -framework AVFoundation \
//     -framework CoreMedia -framework CoreVideo
//
// Optional by construction: nothing in the routers depends on it, and
// grmap's PPM output stays the dependency-free 2D view (CLAUDE.md,
// conventions: dependencies isolated, optional, reason recorded --
// HISTORY_COST_SWEEP.md Part 93).

#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <simd/simd.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace gv {

struct Grid {
  int nx = 0, ny = 0;
  std::vector<float> ratio;
  std::vector<uint8_t> cls;
  std::vector<float> act; // GRMAP2 only: activity in [0, 1]
};

bool readGrid(const std::string &path, Grid &g, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  char magic[8];
  int32_t dims[2];
  if (!f.read(magic, 8) || (std::memcmp(magic, "GRMAP1\0\0", 8) != 0 && std::memcmp(magic, "GRMAP2\0\0", 8) != 0)) {
    err = path + " is not a grmap .grid file";
    return false;
  }
  const bool v2 = magic[5] == '2';
  if (!f.read(reinterpret_cast<char *>(dims), sizeof dims) || dims[0] <= 0 || dims[1] <= 0 || dims[0] > 100000 ||
      dims[1] > 100000) {
    err = path + ": bad dimensions";
    return false;
  }
  g.nx = dims[0];
  g.ny = dims[1];
  size_t n = static_cast<size_t>(g.nx) * g.ny;
  g.ratio.resize(n);
  g.cls.resize(n);
  if (!f.read(reinterpret_cast<char *>(g.ratio.data()), static_cast<std::streamsize>(n * sizeof(float))) ||
      !f.read(reinterpret_cast<char *>(g.cls.data()), static_cast<std::streamsize>(n))) {
    err = path + ": truncated";
    return false;
  }
  g.act.clear();
  if (v2) {
    g.act.resize(n);
    if (!f.read(reinterpret_cast<char *>(g.act.data()), static_cast<std::streamsize>(n * sizeof(float)))) {
      err = path + ": truncated activity";
      return false;
    }
  }
  for (uint8_t c : g.cls)
    if (c > 6) {
      err = path + ": class out of range";
      return false;
    }
  return true;
}

const float kColor[7][3] = {{128 / 255.f, 64 / 255.f, 200 / 255.f}, {40 / 255.f, 100 / 255.f, 230 / 255.f},
                            {40 / 255.f, 170 / 255.f, 70 / 255.f},  {240 / 255.f, 210 / 255.f, 40 / 255.f},
                            {245 / 255.f, 130 / 255.f, 30 / 255.f}, {215 / 255.f, 30 / 255.f, 30 / 255.f},
                            {60 / 255.f, 60 / 255.f, 60 / 255.f}};

struct Instance {
  float x, y, ratio, cls;
  float r, g, b, pad;
};

std::vector<Instance> instances(const Grid &g) {
  std::vector<Instance> out(static_cast<size_t>(g.nx) * g.ny);
  for (int y = 0; y < g.ny; ++y)
    for (int x = 0; x < g.nx; ++x) {
      size_t i = static_cast<size_t>(y) * g.nx + x;
      const float *c = kColor[g.cls[i]];
      float a = g.act.empty() ? 0.0f : std::min(1.0f, std::max(0.0f, g.act[i]));
      out[i] = {static_cast<float>(x), static_cast<float>(y), std::min(g.ratio[i], 3.0f),
                static_cast<float>(g.cls[i]), c[0], c[1], c[2], a};
    }
  return out;
}

// Column-major matrices for simd / Metal.
simd_float4x4 perspective(float fovy, float aspect, float zn, float zf) {
  float ys = 1.0f / std::tan(fovy * 0.5f), xs = ys / aspect, zs = zf / (zn - zf);
  return simd_matrix(simd_make_float4(xs, 0, 0, 0), simd_make_float4(0, ys, 0, 0), simd_make_float4(0, 0, zs, -1),
                     simd_make_float4(0, 0, zs * zn, 0));
}
simd_float4x4 lookAt(simd_float3 eye, simd_float3 target, simd_float3 up) {
  simd_float3 z = simd_normalize(eye - target), x = simd_normalize(simd_cross(up, z)), y = simd_cross(z, x);
  return simd_matrix(simd_make_float4(x.x, y.x, z.x, 0), simd_make_float4(x.y, y.y, z.y, 0),
                     simd_make_float4(x.z, y.z, z.z, 0),
                     simd_make_float4(-simd_dot(x, eye), -simd_dot(y, eye), -simd_dot(z, eye), 1));
}

struct Camera {
  float yaw = -0.6f, pitch = 0.75f, dist = 1, hscale = 1;
  simd_float3 target = {0, 0, 0};
  float minClass = 0;
  float redLift = 0.5f; // extra height of red bars, in full-bar heights
  void reset(const Grid &g) {
    float s = static_cast<float>(std::max(g.nx, g.ny));
    yaw = -0.6f;
    pitch = 0.8f;
    dist = 1.5f * s;
    redLift = 0.5f;
    hscale = 0.06f * s;
    target = simd_make_float3(g.nx * 0.5f, g.ny * 0.5f, 0);
    minClass = 0;
  }
  simd_float3 eye() const {
    return target + dist * simd_make_float3(std::cos(pitch) * std::sin(yaw), -std::cos(pitch) * std::cos(yaw),
                                            std::sin(pitch));
  }
  simd_float4x4 mvp(float aspect) const {
    return simd_mul(perspective(0.8f, aspect, dist * 0.01f, dist * 10.0f), lookAt(eye(), target, simd_make_float3(0, 0, 1)));
  }
};

struct Uniforms {
  simd_float4x4 mvp;
  simd_float4 light; // xyz direction
  float hscale, minClass, redLift, pad1;
};

const char *kShader = R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Vtx { float4 p; float4 n; };
struct Inst { float x, y, ratio, cls; float r, g, b, pad; };
struct Uni { float4x4 mvp; float4 light; float hscale, minClass, redLift, pad1; };
struct Out { float4 pos [[position]]; float3 col; };
vertex Out vmain(uint vid [[vertex_id]], uint iid [[instance_id]],
                 const device Vtx *verts [[buffer(0)]], const device Inst *inst [[buffer(1)]],
                 constant Uni &u [[buffer(2)]]) {
  Vtx v = verts[vid];
  Inst t = inst[iid];
  Out o;
  if (t.cls < u.minClass && t.cls < 5.5) { // hidden: a degenerate triangle
    o.pos = float4(0, 0, 2, 1);
    o.col = float3(0);
    return o;
  }
  float h = max(t.ratio * u.hscale, 0.05);
  if (t.cls > 4.5 && t.cls < 5.5) h += u.redLift * u.hscale;
  float3 w = float3(t.x + 0.06 + v.p.x * 0.88, t.y + 0.06 + v.p.y * 0.88, v.p.z * h);
  o.pos = u.mvp * float4(w, 1);
  float d = 0.42 + 0.58 * max(dot(v.n.xyz, normalize(u.light.xyz)), 0.0);
  if (v.n.z > 0.5) d = max(d, 0.85); // tops stay bright, so the color reads from any angle
  o.col = float3(t.r, t.g, t.b) * d;
  // activity: routes changed here since the last frame -- a bright glow
  // (lightened toward cyan, in proportion; the congestion color stays readable underneath)
  if (t.pad > 0.0) o.col = min(mix(o.col, float3(1.0), 0.30 * t.pad) + float3(0.0, 0.32, 0.40) * t.pad, float3(1.0));
  return o;
}
fragment float4 fmain(Out i [[stage_in]]) { return float4(i.col, 1); }
)MSL";

struct Vtx {
  simd_float4 p, n;
};

std::vector<Vtx> cube() {
  // Unit box [0,1]^3, 6 faces x 2 triangles, outward normals.
  const float P[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  const int F[6][4] = {{4, 5, 6, 7}, {0, 3, 2, 1}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3}};
  const float N[6][3] = {{0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, 1, 0}, {1, 0, 0}, {-1, 0, 0}};
  std::vector<Vtx> v;
  for (int f = 0; f < 6; ++f) {
    int q[6] = {F[f][0], F[f][1], F[f][2], F[f][0], F[f][2], F[f][3]};
    for (int k : q)
      v.push_back({simd_make_float4(P[k][0], P[k][1], P[k][2], 1), simd_make_float4(N[f][0], N[f][1], N[f][2], 0)});
  }
  return v;
}

struct Renderer {
  id<MTLDevice> dev;
  id<MTLCommandQueue> queue;
  id<MTLRenderPipelineState> pipe;
  id<MTLDepthStencilState> depth;
  id<MTLBuffer> verts, inst;
  NSUInteger vertCount = 0, instCount = 0;
  bool init(id<MTLDevice> d, MTLPixelFormat color, const Grid &g, std::string &err) {
    dev = d;
    queue = [dev newCommandQueue];
    NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:kShader] options:nil error:&e];
    if (!lib) {
      err = std::string("shader compile failed: ") + (e ? e.localizedDescription.UTF8String : "?");
      return false;
    }
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vmain"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fmain"];
    pd.colorAttachments[0].pixelFormat = color;
    pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    pipe = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
    if (!pipe) {
      err = std::string("pipeline failed: ") + (e ? e.localizedDescription.UTF8String : "?");
      return false;
    }
    MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
    dd.depthCompareFunction = MTLCompareFunctionLess;
    dd.depthWriteEnabled = YES;
    depth = [dev newDepthStencilStateWithDescriptor:dd];
    std::vector<Vtx> c = cube();
    vertCount = c.size();
    verts = [dev newBufferWithBytes:c.data() length:c.size() * sizeof(Vtx) options:MTLResourceStorageModeShared];
    std::vector<Instance> in = instances(g);
    instCount = in.size();
    inst = [dev newBufferWithBytes:in.data() length:in.size() * sizeof(Instance) options:MTLResourceStorageModeShared];
    return true;
  }
  void setGrid(const Grid &g) {
    std::vector<Instance> in = instances(g);
    if (in.size() != instCount || !inst) {
      instCount = in.size();
      inst = [dev newBufferWithBytes:in.data() length:in.size() * sizeof(Instance) options:MTLResourceStorageModeShared];
    } else {
      std::memcpy([inst contents], in.data(), in.size() * sizeof(Instance));
    }
  }
  void encode(id<MTLRenderCommandEncoder> enc, const Camera &cam, float aspect) {
    Uniforms u;
    u.mvp = cam.mvp(aspect);
    u.light = simd_make_float4(0.4f, -0.7f, 0.9f, 0);
    u.hscale = cam.hscale;
    u.minClass = cam.minClass;
    u.redLift = cam.redLift;
    u.pad1 = 0;
    [enc setRenderPipelineState:pipe];
    [enc setDepthStencilState:depth];
    [enc setCullMode:MTLCullModeBack];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];
    [enc setVertexBuffer:verts offset:0 atIndex:0];
    [enc setVertexBuffer:inst offset:0 atIndex:1];
    [enc setVertexBytes:&u length:sizeof u atIndex:2];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertCount instanceCount:instCount];
  }
};

const MTLClearColor kBackground = {0.93, 0.94, 0.95, 1};

// Offscreen rendering into a BGRA texture, read back as pixels.
struct Offscreen {
  id<MTLDevice> dev;
  Renderer r;
  id<MTLTexture> color, dtex;
  int W = 0, H = 0;
  bool init(const Grid &g, int w, int h, std::string &err) {
    W = w;
    H = h;
    dev = MTLCreateSystemDefaultDevice();
    if (!dev) {
      err = "no Metal device";
      return false;
    }
    if (!r.init(dev, MTLPixelFormatBGRA8Unorm, g, err))
      return false;
    MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                   width:W height:H mipmapped:NO];
    cd.usage = MTLTextureUsageRenderTarget;
    cd.storageMode = MTLStorageModeShared;
    color = [dev newTextureWithDescriptor:cd];
    MTLTextureDescriptor *zd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                   width:W height:H mipmapped:NO];
    zd.usage = MTLTextureUsageRenderTarget;
    zd.storageMode = MTLStorageModePrivate;
    dtex = [dev newTextureWithDescriptor:zd];
    return true;
  }
  void render(const Camera &cam, std::vector<uint8_t> &px) {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = kBackground;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.depthAttachment.texture = dtex;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.clearDepth = 1.0;
    id<MTLCommandBuffer> cb = [r.queue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
    r.encode(enc, cam, static_cast<float>(W) / H);
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    px.resize(static_cast<size_t>(W) * H * 4);
    [color getBytes:px.data() bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
  }
};

// BGRA pixels to a PNG or JPEG file.
bool writeImage(std::vector<uint8_t> &px, int W, int H, const std::string &path, bool jpeg, std::string &err) {
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(px.data(), W, H, 8, W * 4, cs,
                                           kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
  CGImageRef img = CGBitmapContextCreateImage(ctx);
  NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
  CFStringRef type = (__bridge CFStringRef)(jpeg ? UTTypeJPEG.identifier : UTTypePNG.identifier);
  CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, type, 1, nullptr);
  bool ok = dst != nullptr;
  if (dst) {
    NSDictionary *props = jpeg ? @{(__bridge NSString *)kCGImageDestinationLossyCompressionQuality : @0.82} : @{};
    CGImageDestinationAddImage(dst, img, (__bridge CFDictionaryRef)props);
    ok = CGImageDestinationFinalize(dst);
    CFRelease(dst);
  }
  CGImageRelease(img);
  CGContextRelease(ctx);
  CGColorSpaceRelease(cs);
  if (!ok)
    err = "could not write " + path;
  return ok;
}

bool snapshot(const Grid &g, const Camera &cam, int W, int H, const std::string &path, std::string &err) {
  Offscreen off;
  if (!off.init(g, W, H, err))
    return false;
  std::vector<uint8_t> px;
  off.render(cam, px);
  return writeImage(px, W, H, path, false, err);
}

// An H.264 movie from BGRA frames, each held `repeat` video frames.
struct MovieWriter {
  AVAssetWriter *writer = nil;
  AVAssetWriterInput *input = nil;
  AVAssetWriterInputPixelBufferAdaptor *adaptor = nil;
  int W = 0, H = 0, fps = 30;
  long long frame = 0;
  bool open(const std::string &path, int w, int h, int f, std::string &err) {
    W = w;
    H = h;
    fps = f;
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    NSError *e = nil;
    writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&e];
    if (!writer) {
      err = std::string("cannot create movie: ") + (e ? e.localizedDescription.UTF8String : "?");
      return false;
    }
    NSDictionary *settings = @{
      AVVideoCodecKey : AVVideoCodecTypeH264,
      AVVideoWidthKey : @(W),
      AVVideoHeightKey : @(H),
      AVVideoCompressionPropertiesKey : @{AVVideoAverageBitRateKey : @(8000000)}
    };
    input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
    input.expectsMediaDataInRealTime = NO;
    adaptor = [AVAssetWriterInputPixelBufferAdaptor
        assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                   sourcePixelBufferAttributes:@{
                                     (NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                                     (NSString *)kCVPixelBufferWidthKey : @(W),
                                     (NSString *)kCVPixelBufferHeightKey : @(H)
                                   }];
    [writer addInput:input];
    if (![writer startWriting]) {
      err = "cannot start the movie";
      return false;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];
    return true;
  }
  bool add(const std::vector<uint8_t> &px, int repeat, std::string &err) {
    for (int r = 0; r < repeat; ++r) {
      while (!input.readyForMoreMediaData)
        [NSThread sleepForTimeInterval:0.002];
      CVPixelBufferRef pb = nullptr;
      if (CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor.pixelBufferPool, &pb) != kCVReturnSuccess || !pb) {
        err = "no pixel buffer";
        return false;
      }
      CVPixelBufferLockBaseAddress(pb, 0);
      uint8_t *dst = static_cast<uint8_t *>(CVPixelBufferGetBaseAddress(pb));
      size_t stride = CVPixelBufferGetBytesPerRow(pb);
      for (int y = 0; y < H; ++y)
        std::memcpy(dst + y * stride, px.data() + static_cast<size_t>(y) * W * 4, static_cast<size_t>(W) * 4);
      CVPixelBufferUnlockBaseAddress(pb, 0);
      bool ok = [adaptor appendPixelBuffer:pb withPresentationTime:CMTimeMake(frame++, fps)];
      CVPixelBufferRelease(pb);
      if (!ok) {
        err = "could not append a movie frame";
        return false;
      }
    }
    return true;
  }
  bool close(std::string &err) {
    [input markAsFinished];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{
      dispatch_semaphore_signal(done);
    }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (writer.status != AVAssetWriterStatusCompleted) {
      err = "the movie did not finish";
      return false;
    }
    return true;
  }
};

} // namespace gv

// ---------------------------------------------------------------------------
// The interactive window.
// ---------------------------------------------------------------------------

@interface GVView : MTKView
@property(nonatomic) gv::Camera *cam;
@property(nonatomic) const gv::Grid *grid;
@end

@implementation GVView
- (BOOL)acceptsFirstResponder {
  return YES;
}
- (void)mouseDragged:(NSEvent *)e {
  gv::Camera &c = *self.cam;
  if (e.modifierFlags & NSEventModifierFlagShift) {
    float s = c.dist * 0.0015f;
    simd_float3 right = {std::cos(c.yaw), std::sin(c.yaw), 0}, fwd = {-std::sin(c.yaw), std::cos(c.yaw), 0};
    c.target = c.target - right * (float)e.deltaX * s + fwd * (float)e.deltaY * s;
  } else {
    c.yaw -= (float)e.deltaX * 0.008f;
    c.pitch = std::min(1.55f, std::max(0.05f, c.pitch + (float)e.deltaY * 0.008f));
  }
}
- (void)scrollWheel:(NSEvent *)e {
  gv::Camera &c = *self.cam;
  c.dist = std::min(20000.0f, std::max(2.0f, c.dist * std::exp(-(float)e.scrollingDeltaY * 0.02f)));
}
- (void)keyDown:(NSEvent *)e {
  gv::Camera &c = *self.cam;
  NSString *k = e.charactersIgnoringModifiers.lowercaseString;
  if ([k isEqualToString:@"+"] || [k isEqualToString:@"="])
    c.hscale *= 1.25f;
  else if ([k isEqualToString:@"-"])
    c.hscale /= 1.25f;
  else if ([k isEqualToString:@"h"])
    c.minClass = c.minClass > 0 ? 0 : 2; // hide violet and blue
  else if ([k isEqualToString:@"b"])
    c.redLift = c.redLift > 0 ? 0 : 0.5f;
  else if ([k isEqualToString:@"r"])
    c.reset(*self.grid);
  else
    [super keyDown:e];
}
@end

@interface GVDelegate : NSObject <MTKViewDelegate, NSWindowDelegate>
@property(nonatomic) gv::Renderer *renderer;
@property(nonatomic) gv::Camera *cam;
@end

@implementation GVDelegate
- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
  (void)view;
  (void)size;
}
- (void)drawInMTKView:(MTKView *)view {
  MTLRenderPassDescriptor *rp = view.currentRenderPassDescriptor;
  if (!rp)
    return;
  id<MTLCommandBuffer> cb = [self.renderer->queue commandBuffer];
  id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
  CGSize s = view.drawableSize;
  self.renderer->encode(enc, *self.cam, static_cast<float>(s.width / std::max(1.0, s.height)));
  [enc endEncoding];
  [cb presentDrawable:view.currentDrawable];
  [cb commit];
}
- (void)windowWillClose:(NSNotification *)n {
  (void)n;
  [NSApp terminate:nil];
}
@end

// ---------------------------------------------------------------------------
// Self-test: the file reader (good and broken files), the class colors, and
// the camera (the orbit target projects to the center of the screen, and a
// point above it projects higher on screen).
// ---------------------------------------------------------------------------

static int failures = 0;
static void check(bool c, const std::string &what) {
  std::cout << (c ? "  ok   " : "  FAIL ") << what << "\n";
  if (!c)
    ++failures;
}

static int selftest() {
  std::cout << "grview self test\n";
  const char *tmp = "grview_selftest.grid";
  {
    std::ofstream f(tmp, std::ios::binary);
    f.write("GRMAP1\0\0", 8);
    int32_t d[2] = {3, 2};
    f.write(reinterpret_cast<const char *>(d), sizeof d);
    float r[6] = {0.1f, 0.6f, 0.8f, 0.95f, 1.0f, 1.5f};
    f.write(reinterpret_cast<const char *>(r), sizeof r);
    uint8_t c[6] = {0, 1, 2, 3, 4, 5};
    f.write(reinterpret_cast<const char *>(c), sizeof c);
  }
  gv::Grid g;
  std::string err;
  check(gv::readGrid(tmp, g, err) && g.nx == 3 && g.ny == 2 && g.ratio[5] == 1.5f && g.cls[4] == 4,
        "a 3 x 2 grid reads back exactly");
  std::vector<gv::Instance> in = gv::instances(g);
  check(in.size() == 6 && in[4].x == 1 && in[4].y == 1 && in[5].r > 0.8f && in[5].g < 0.2f && in[0].b > in[0].g,
        "instances: tile (1,1) at x 1 y 1, the overflowed tile red, the low tile violet");
  {
    std::ofstream f(tmp, std::ios::binary);
    f.write("GRMAP1\0\0", 8);
    int32_t d[2] = {3, 2};
    f.write(reinterpret_cast<const char *>(d), sizeof d);
  }
  gv::Grid t;
  check(!gv::readGrid(tmp, t, err), "a truncated file is rejected (" + err + ")");
  {
    std::ofstream f(tmp, std::ios::binary);
    f.write("NOTAGRID", 8);
  }
  check(!gv::readGrid(tmp, t, err), "a file with the wrong magic is rejected");
  {
    std::ofstream f(tmp, std::ios::binary);
    f.write("GRMAP2\0\0", 8);
    int32_t d[2] = {2, 1};
    f.write(reinterpret_cast<const char *>(d), sizeof d);
    float r[2] = {0.2f, 1.2f};
    f.write(reinterpret_cast<const char *>(r), sizeof r);
    uint8_t c[2] = {0, 5};
    f.write(reinterpret_cast<const char *>(c), sizeof c);
    float a[2] = {0.0f, 0.75f};
    f.write(reinterpret_cast<const char *>(a), sizeof a);
  }
  gv::Grid g2;
  check(gv::readGrid(tmp, g2, err) && g2.act.size() == 2 && g2.act[1] == 0.75f,
        "a GRMAP2 grid reads back with its activity");
  std::vector<gv::Instance> in2 = gv::instances(g2);
  check(in2[0].pad == 0.0f && in2[1].pad == 0.75f, "activity reaches the instances (the glow channel)");
  {
    std::ofstream f(tmp, std::ios::binary);
    f.write("GRMAP2\0\0", 8);
    int32_t d[2] = {2, 1};
    f.write(reinterpret_cast<const char *>(d), sizeof d);
    float r[2] = {0.2f, 1.2f};
    f.write(reinterpret_cast<const char *>(r), sizeof r);
    uint8_t c[2] = {0, 5};
    f.write(reinterpret_cast<const char *>(c), sizeof c);
  }
  check(!gv::readGrid(tmp, t, err), "a GRMAP2 grid without its activity is rejected");
  std::remove(tmp);
  gv::Camera cam;
  cam.reset(g);
  simd_float4 c = simd_mul(cam.mvp(1.0f), simd_make_float4(cam.target.x, cam.target.y, cam.target.z, 1));
  simd_float4 up = simd_mul(cam.mvp(1.0f), simd_make_float4(cam.target.x, cam.target.y, cam.target.z + 1, 1));
  check(std::fabs(c.x / c.w) < 1e-4f && std::fabs(c.y / c.w) < 1e-4f && c.w > 0,
        "the orbit target projects to the center of the screen, in front of the camera");
  check(up.y / up.w > c.y / c.w, "a point above the target projects higher on the screen");
  std::cout << "\n" << (failures == 0 ? "SELF TEST PASS" : "SELF TEST FAIL") << "\n";
  return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  @autoreleasepool {
    if (argc == 2 && std::string(argv[1]) == "--selftest")
      return selftest();
    if (argc < 2) {
      std::cerr << "usage: grview <file.grid> [title] | grview <file.grid> --snapshot out.png [options] | grview "
                   "--sequence frames.tsv --out-dir DIR [--video out.mp4] [options] | grview --selftest\n";
      return 2;
    }
    if (std::string(argv[1]) == "--sequence") {
      if (argc < 3) {
        std::cerr << "grview: --sequence frames.tsv\n";
        return 2;
      }
      std::string tsv = argv[2], outDir, video, err;
      int W = 1600, H = 900, fps = 30;
      double hold = 0.5;
      std::vector<std::string> files;
      {
        std::ifstream f(tsv);
        if (!f) {
          std::cerr << "grview: cannot read " << tsv << "\n";
          return 1;
        }
        std::string line;
        std::getline(f, line); // header
        while (std::getline(f, line)) {
          size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
          if (b != std::string::npos)
            files.push_back(line.substr(a + 1, b - a - 1));
        }
      }
      if (files.empty()) {
        std::cerr << "grview: no frames listed in " << tsv << "\n";
        return 1;
      }
      std::string dir = tsv.find('/') == std::string::npos ? "." : tsv.substr(0, tsv.rfind('/'));
      gv::Grid g;
      if (!gv::readGrid(dir + "/" + files[0], g, err)) {
        std::cerr << "grview: " << err << "\n";
        return 1;
      }
      gv::Camera cam;
      cam.reset(g);
      for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> double {
          if (i + 1 >= argc) {
            std::cerr << "grview: " << a << " needs a value\n";
            std::exit(2);
          }
          return std::atof(argv[++i]);
        };
        if (a == "--out-dir" && i + 1 < argc)
          outDir = argv[++i];
        else if (a == "--video" && i + 1 < argc)
          video = argv[++i];
        else if (a == "--fps")
          fps = std::max(1, static_cast<int>(next()));
        else if (a == "--hold")
          hold = next();
        else if (a == "--yaw")
          cam.yaw = static_cast<float>(next() * M_PI / 180);
        else if (a == "--pitch")
          cam.pitch = static_cast<float>(next() * M_PI / 180);
        else if (a == "--zoom")
          cam.dist /= static_cast<float>(next());
        else if (a == "--hscale")
          cam.hscale *= static_cast<float>(next());
        else if (a == "--red-lift")
          cam.redLift = static_cast<float>(next());
        else if (a == "--size" && i + 1 < argc) {
          if (std::sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 16 || H < 16 || W > 8192 || H > 8192) {
            std::cerr << "grview: --size WxH\n";
            return 2;
          }
          W &= ~1; // the movie wants even sizes
          H &= ~1;
        }
      }
      if (outDir.empty()) {
        std::cerr << "grview: --sequence needs --out-dir\n";
        return 2;
      }
      gv::Offscreen off;
      if (!off.init(g, W, H, err)) {
        std::cerr << "grview: " << err << "\n";
        return 1;
      }
      gv::MovieWriter mw;
      if (!video.empty() && !mw.open(video, W, H, fps, err)) {
        std::cerr << "grview: " << err << "\n";
        return 1;
      }
      std::vector<uint8_t> px;
      const int repeat = std::max(1, static_cast<int>(std::lround(hold * fps)));
      for (size_t k = 0; k < files.size(); ++k) {
        @autoreleasepool {
          if (k > 0 && !gv::readGrid(dir + "/" + files[k], g, err)) {
            std::cerr << "grview: " << err << "\n";
            return 1;
          }
          off.r.setGrid(g);
          off.render(cam, px);
          char name[32];
          std::snprintf(name, sizeof name, "/f%04zu.jpg", k);
          if (!gv::writeImage(px, W, H, outDir + name, true, err) || (!video.empty() && !mw.add(px, repeat, err))) {
            std::cerr << "grview: " << err << "\n";
            return 1;
          }
        }
      }
      if (!video.empty() && !mw.close(err)) {
        std::cerr << "grview: " << err << "\n";
        return 1;
      }
      std::printf("%zu frames rendered to %s%s\n", files.size(), outDir.c_str(),
                  video.empty() ? "" : (", movie " + video).c_str());
      return 0;
    }
    gv::Grid grid;
    std::string err;
    if (!gv::readGrid(argv[1], grid, err)) {
      std::cerr << "grview: " << err << "\n";
      return 1;
    }
    gv::Camera cam;
    cam.reset(grid);
    std::string snap, title = argc >= 3 && argv[2][0] != '-' ? argv[2] : argv[1];
    int W = 1600, H = 1000;
    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      auto next = [&]() -> double {
        if (i + 1 >= argc) {
          std::cerr << "grview: " << a << " needs a value\n";
          std::exit(2);
        }
        return std::atof(argv[++i]);
      };
      if (a == "--snapshot" && i + 1 < argc)
        snap = argv[++i];
      else if (a == "--yaw")
        cam.yaw = static_cast<float>(next() * M_PI / 180);
      else if (a == "--pitch")
        cam.pitch = static_cast<float>(next() * M_PI / 180);
      else if (a == "--zoom")
        cam.dist /= static_cast<float>(next());
      else if (a == "--hscale")
        cam.hscale *= static_cast<float>(next());
      else if (a == "--hide-below")
        cam.minClass = static_cast<float>(next());
      else if (a == "--red-lift")
        cam.redLift = static_cast<float>(next());
      else if (a == "--focus") {
        float fx = static_cast<float>(next()), fy = static_cast<float>(next());
        cam.target = simd_make_float3(fx, fy, 0);
      } else if (a == "--size" && i + 1 < argc) {
        if (std::sscanf(argv[++i], "%dx%d", &W, &H) != 2 || W < 16 || H < 16 || W > 8192 || H > 8192) {
          std::cerr << "grview: --size WxH\n";
          return 2;
        }
      }
    }
    if (!snap.empty()) {
      if (std::getenv("GRVIEW_DEBUG"))
        std::fprintf(stderr, "target %.1f %.1f dist %.1f yaw %.3f pitch %.3f hscale %.2f size %dx%d\n", cam.target.x,
                     cam.target.y, cam.dist, cam.yaw, cam.pitch, cam.hscale, W, H);
      if (!gv::snapshot(grid, cam, W, H, snap, err)) {
        std::cerr << "grview: " << err << "\n";
        return 1;
      }
      return 0;
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    NSRect frame = NSMakeRect(80, 80, 1400, 900);
    NSWindow *win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable |
                            NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    win.title = [NSString
        stringWithFormat:@"%s  —  drag: rotate · scroll: zoom · shift-drag: pan · +/-: bar height · H: hide cool "
                         @"tiles · B: red lift · R: reset",
                         title.c_str()];
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    if (!dev) {
      std::cerr << "grview: no Metal device\n";
      return 1;
    }
    GVView *view = [[GVView alloc] initWithFrame:frame device:dev];
    view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    view.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
    view.clearColor = gv::kBackground;
    view.cam = &cam;
    view.grid = &grid;
    gv::Renderer renderer;
    if (!renderer.init(dev, view.colorPixelFormat, grid, err)) {
      std::cerr << "grview: " << err << "\n";
      return 1;
    }
    GVDelegate *del = [GVDelegate new];
    del.renderer = &renderer;
    del.cam = &cam;
    view.delegate = del;
    win.delegate = del;
    win.contentView = view;
    [win makeKeyAndOrderFront:nil];
    [win makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
  }
  return 0;
}
