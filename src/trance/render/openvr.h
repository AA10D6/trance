#ifndef TRANCE_SRC_TRANCE_RENDER_OPENVR_H
#define TRANCE_SRC_TRANCE_RENDER_OPENVR_H
#include <trance/render/render.h>
#include <vector>

#pragma warning(push, 0)
#include <GL/glew.h>
#include <openvr/openvr.h>
#pragma warning(pop)

class OpenVrRenderer : public Renderer
{
public:
  OpenVrRenderer(const trance_pb::System& system);
  ~OpenVrRenderer() override;
  bool success() const;

  bool vr_enabled() const override;
  bool is_openvr() const override;
  uint32_t view_width() const override;
  uint32_t width() const override;
  uint32_t height() const override;
  float eye_spacing_multiplier() const override;

  void init() override;
  bool update() override;
  void render(const std::function<void(State)>& render_fn) override;

private:
  bool _initialised;
  bool _success;
  uint32_t _width;
  uint32_t _height;

  vr::IVRSystem* _system;
  vr::IVRCompositor* _compositor;

  struct FramebufData
  {
    GLuint depthBufferId;
    GLuint renderTextureId;
    GLuint renderFramebufferId;
    GLuint resolveTextureId;
    GLuint resolveFramebufferId;
  };

  FramebufData leftEye;
  FramebufData rightEye;

  bool create_frame_buf(int, int, FramebufData&);
};

#endif