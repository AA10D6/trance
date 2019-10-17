#include <trance/render/openvr.h>
#include <common/util.h>
#include <algorithm>
#include <iostream>

#pragma warning(push, 0)
#include <common/trance.pb.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>
#pragma warning(pop)

OpenVrRenderer::OpenVrRenderer(const trance_pb::System& system)
: _initialised{false}
, _success{false}
, _width{0}
, _height{0}
, _system{nullptr}
, _compositor{nullptr}
{
  vr::HmdError error;
  _system = vr::VR_Init(&error, vr::VRApplication_Scene);
  if (!_system || error != vr::VRInitError_None) {
    std::cerr << "OpenVR initialization failed" << std::endl;
    std::cerr << vr::VR_GetVRInitErrorAsEnglishDescription(error) << std::endl;
    return;
  }
  _initialised = true;

  _window.reset(new sf::RenderWindow);
  _window->create({}, "trance", sf::Style::None);
  _window->setVerticalSyncEnabled(system.enable_vsync());
  _window->setFramerateLimit(0);
  _window->setVisible(false);
  _window->setActive(true);

  init_glew();

  _compositor = vr::VRCompositor();
  if (!_compositor) {
    std::cerr << "OpenVR compositor failed" << std::endl;
    return;
  }

  _system->GetRecommendedRenderTargetSize(&_width, &_height);
  if (!(create_frame_buf(_width, _height, leftEye) && create_frame_buf(_width, _height, rightEye))) {
    std::cerr << "framebuffer failed" << std::endl;
    return;
  }

  _success = true;
}

OpenVrRenderer::~OpenVrRenderer()
{
  if (_initialised) {
    vr::VR_Shutdown();
  }

  glDeleteRenderbuffers(1, &leftEye.depthBufferId);
  glDeleteTextures(1, &leftEye.renderTextureId);
  glDeleteFramebuffers(1, &leftEye.renderFramebufferId);
  glDeleteTextures(1, &leftEye.resolveTextureId);
  glDeleteFramebuffers(1, &leftEye.resolveFramebufferId);

  glDeleteRenderbuffers(1, &rightEye.depthBufferId);
  glDeleteTextures(1, &rightEye.renderTextureId);
  glDeleteFramebuffers(1, &rightEye.renderFramebufferId);
  glDeleteTextures(1, &rightEye.resolveTextureId);
  glDeleteFramebuffers(1, &rightEye.resolveFramebufferId);
}

bool OpenVrRenderer::create_frame_buf(int width, int height, FramebufData &fbuf_data)
{
  glGenFramebuffers(1, &fbuf_data.renderFramebufferId);
  glBindFramebuffer(GL_FRAMEBUFFER, fbuf_data.renderFramebufferId);

  glGenRenderbuffers(1, &fbuf_data.depthBufferId);
  glBindRenderbuffer(GL_RENDERBUFFER, fbuf_data.depthBufferId);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbuf_data.depthBufferId);

  glGenTextures(1, &fbuf_data.renderTextureId);
  glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, fbuf_data.renderTextureId);
  glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, width, height, true);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, fbuf_data.renderTextureId, 0);

  glGenFramebuffers(1, &fbuf_data.resolveFramebufferId);
  glBindFramebuffer(GL_FRAMEBUFFER, fbuf_data.resolveFramebufferId);

  glGenTextures(1, &fbuf_data.resolveTextureId);
  glBindTexture(GL_TEXTURE_2D, fbuf_data.resolveTextureId);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbuf_data.resolveTextureId, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

bool OpenVrRenderer::success() const
{
  return _success;
}

bool OpenVrRenderer::vr_enabled() const
{
  return true;
}

bool OpenVrRenderer::is_openvr() const
{
  return true;
}

uint32_t OpenVrRenderer::view_width() const
{
  return _width;
}

uint32_t OpenVrRenderer::width() const
{
  // TODO: ???
  return _width;
}

uint32_t OpenVrRenderer::height() const
{
  return _height;
}

float OpenVrRenderer::eye_spacing_multiplier() const
{
  // TODO: ???
  return 150.f;
}

void OpenVrRenderer::init()
{
}

bool OpenVrRenderer::update()
{
  vr::VREvent_t event;
  while (_system->PollNextEvent(&event, sizeof(event))) {
    // Ignore.
  }
  return true;
}

void OpenVrRenderer::render(const std::function<void(State)>& render_fn)
{
  static vr::TrackedDevicePose_t m_rTrackedDevicePose[vr::k_unMaxTrackedDeviceCount];
  auto error = vr::VRCompositor()->WaitGetPoses(m_rTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
  if (error != vr::VRCompositorError_None) {
    std::cerr << "compositor wait failed: " << static_cast<uint32_t>(error) << std::endl;
  }

  // LEFT
  glBindFramebuffer(GL_FRAMEBUFFER, leftEye.renderFramebufferId);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, _width, _height);
  render_fn(State::VR_LEFT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, leftEye.renderFramebufferId);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, leftEye.resolveFramebufferId);
  glBlitFramebuffer(0, 0, _width, _height, 0, 0, _width, _height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

  // RIGHT
  glBindFramebuffer(GL_FRAMEBUFFER, rightEye.renderFramebufferId);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(0, 0, _width, _height);
  render_fn(State::VR_RIGHT);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, rightEye.renderFramebufferId);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rightEye.resolveFramebufferId);
  glBlitFramebuffer(0, 0, _width, _height, 0, 0, _width, _height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

  vr::Texture_t left = { (void*)(uintptr_t)leftEye.resolveTextureId, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
  vr::Texture_t right = { (void*)(uintptr_t)rightEye.resolveTextureId, vr::TextureType_OpenGL, vr::ColorSpace_Gamma };
  error = vr::VRCompositor()->Submit(vr::Eye_Left, &left);
  if (error != vr::VRCompositorError_None) {
    std::cerr << "compositor submit failed: " << static_cast<uint32_t>(error) << std::endl;
  }
  error = vr::VRCompositor()->Submit(vr::Eye_Right, &right);
  if (error != vr::VRCompositorError_None) {
    std::cerr << "compositor submit failed: " << static_cast<uint32_t>(error) << std::endl;
  }
  vr::VRCompositor()->PostPresentHandoff();
}