#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cmath>
#include <cstdlib>
#include <string>

#include "core/TdClient.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkImageFilters.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLTypes.h"

static sk_sp<SkSurface> makeSurface(GrDirectContext *ctx, int w, int h) {
  GrGLFramebufferInfo fb{0, 0x8058 /* GL_RGBA8 */};
  auto rt = GrBackendRenderTargets::MakeGL(w, h, /*samples*/ 0, /*stencil*/ 8, fb);
  return SkSurfaces::WrapBackendRenderTarget(ctx, rt, kBottomLeft_GrSurfaceOrigin,
                                             kRGBA_8888_SkColorType, nullptr, nullptr);
}

// Стеклянная панель: блюр того, что под ней, + подсветка и контур.
static void drawGlass(SkCanvas *c, SkRect r, float radius, float blur) {
  SkRRect rr = SkRRect::MakeRectXY(r, radius, radius);
  c->save();
  c->clipRRect(rr, true);
  auto f = SkImageFilters::Blur(blur, blur, SkTileMode::kClamp, nullptr);
  c->saveLayer(SkCanvas::SaveLayerRec(&r, nullptr, f.get(), 0));
  c->restore();
  SkPaint tint;
  tint.setAntiAlias(true);
  tint.setColor(SkColorSetARGB(46, 255, 255, 255));
  c->drawRRect(rr, tint);
  c->restore();

  SkPaint edge;
  edge.setAntiAlias(true);
  edge.setStyle(SkPaint::kStroke_Style);
  edge.setStrokeWidth(1.5f);
  edge.setColor(SkColorSetARGB(110, 255, 255, 255));
  c->drawRRect(rr, edge);
}

int main(int, char **) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
  }

#ifdef __ANDROID__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_Window *win = SDL_CreateWindow("ayuglass", 420, 860,
                                     SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!win) { SDL_Log("window: %s", SDL_GetError()); return 1; }
  SDL_GLContext gl = SDL_GL_CreateContext(win);
  if (!gl) { SDL_Log("gl: %s", SDL_GetError()); return 1; }
  SDL_GL_SetSwapInterval(1);

  auto iface = GrGLMakeAssembledInterface(
      nullptr, [](void *, const char *name) -> GrGLFuncPtr { return (GrGLFuncPtr)SDL_GL_GetProcAddress(name); });
  sk_sp<GrDirectContext> ctx = GrDirectContexts::MakeGL(iface);
  if (!ctx) { SDL_Log("Skia GL context failed"); return 1; }

  // --- TDLib (api_id/hash приходят из CMake: -DTG_API_ID / -DTG_API_HASH) ---
  core::Config cfg;
  cfg.api_id = TG_API_ID;
  cfg.api_hash = TG_API_HASH;
  // Для десктопных dev-сборок: ключи из окружения (в CI они не вшиваются)
  if (const char *e = SDL_getenv("TG_API_ID")) cfg.api_id = std::atoi(e);
  if (const char *e = SDL_getenv("TG_API_HASH")) cfg.api_hash = e;
  char *pref = SDL_GetPrefPath("ayuglass", "app");
  cfg.data_dir = std::string(pref ? pref : "") + "tdlib";
  SDL_free(pref);
  core::TdClient td(cfg);
  td.start();

  int pw = 0, ph = 0;
  sk_sp<SkSurface> surface;
  bool running = true;
  Uint64 last = SDL_GetTicks();
  int frames = 0;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
    }

    for (auto &ev : td.events().drain()) {
      if (auto *a = std::get_if<core::EvAuth>(&ev)) SDL_Log("auth state: %d", (int)a->state);
      else if (auto *c = std::get_if<core::EvChat>(&ev)) SDL_Log("chat: %s", c->title.c_str());
      else if (auto *er = std::get_if<core::EvError>(&ev)) SDL_Log("td error %d: %s", er->code, er->message.c_str());
    }

    int w, h;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    if (!surface || w != pw || h != ph) { surface = makeSurface(ctx.get(), w, h); pw = w; ph = h; }
    if (!surface) continue;

    SkCanvas *c = surface->getCanvas();
    float t = SDL_GetTicks() / 1000.f;
    c->clear(SkColorSetRGB(18, 22, 34));

    SkPaint p;
    p.setAntiAlias(true);
    SkColor cols[3] = {SkColorSetRGB(70, 140, 255), SkColorSetRGB(255, 110, 160), SkColorSetRGB(90, 220, 170)};
    for (int i = 0; i < 3; ++i) {
      p.setColor(cols[i]);
      c->drawCircle(w * (0.5f + 0.3f * std::sin(t * (0.7f + i * 0.3f) + i)),
                    h * (0.4f + 0.2f * std::cos(t * (0.5f + i * 0.25f))), w * 0.22f, p);
    }
    drawGlass(c, SkRect::MakeXYWH(w * 0.08f, h * 0.35f, w * 0.84f, h * 0.2f), w * 0.06f, 18.f);

    ctx->flushAndSubmit();
    SDL_GL_SwapWindow(win);

    ++frames;
    Uint64 now = SDL_GetTicks();
    if (now - last >= 2000) {
      SDL_Log("fps: %.1f", frames * 1000.0 / (now - last));
      frames = 0;
      last = now;
    }
  }

  td.stop();
  surface.reset();
  ctx.reset();
  SDL_GL_DestroyContext(gl);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
