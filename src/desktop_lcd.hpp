#pragma once
#include <gbcllr/presentation.hpp>
#include <SDL.h>
#include <memory>

// Presentation-only resource owner, borrowing the desktop's SDL device.
// An unavailable/lost device returns false so the caller uses the CPU service.
class DesktopLcd {
public:
    static SDL_Renderer* create_renderer(SDL_Window*);
    explicit DesktopLcd(SDL_Renderer*);
    ~DesktopLcd();
    DesktopLcd(const DesktopLcd&)=delete;
    DesktopLcd& operator=(const DesktopLcd&)=delete;
    bool available() const;
    bool draw(const gbcllr::PresentationFrame&,const SDL_Rect&);
    void reset();
    uint64_t draws() const;
    uint64_t upload_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    class GlOwner;
    std::unique_ptr<GlOwner> gl_;
};
