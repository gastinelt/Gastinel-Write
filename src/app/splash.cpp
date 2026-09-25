#include "app/splash.h"

#include "app/theme.h"

#include "image/png.h"
#include "platform/log.h"
#include "render/draw.h"
#include "render/text_run.h"

namespace gw::app {
namespace {

constexpr int kWidth = 560;
constexpr int kHeight = 200;
constexpr int kPadding = 28;
constexpr int kLogoSide = 144;   // carre du logo, angles droits stricts
constexpr int kTextGap = 28;     // entre le carre et la colonne de texte
constexpr int kLineGap = 10;     // entre les deux lignes de texte

constexpr float kTaglineSize = 26.0f;
constexpr float kSubtitleSize = 14.0f;


// Texte figé par la charte si la citation est désactivée.
constexpr char kTagline[] = "L’écriture, sans fard.";
constexpr char kSubtitle[] = "Gastinel. Logiciel libre de droit.";

std::vector<std::string> wrap_text(const std::string& text, const font::TtfFont& font,
                                   render::GlyphAtlas& atlas, int max_width) {
    std::vector<std::string> lines;
    std::string current_line;
    std::string current_word;

    for (std::size_t i = 0; i <= text.size(); ++i) {
        char c = (i < text.size()) ? text[i] : ' ';
        if (c == ' ' || c == '\t' || c == '\n' || i == text.size()) {
            if (!current_word.empty()) {
                std::string test_line = current_line.empty() ? current_word : (current_line + " " + current_word);
                if (!current_line.empty() && render::measure_utf8(test_line, font, atlas) > max_width) {
                    lines.push_back(current_line);
                    current_line = current_word;
                } else {
                    current_line = test_line;
                }
                current_word.clear();
            }
            if (c == '\n') {
                if (!current_line.empty()) {
                    lines.push_back(current_line);
                    current_line.clear();
                }
            }
        } else {
            current_word += c;
        }
    }
    if (!current_line.empty()) {
        lines.push_back(current_line);
    }
    return lines;
}

} // namespace

bool SplashScreen::show(FontSet& fonts, const std::string& branding_dir, bool show_quote,
                        const Quote* quote) {
    show_quote_ = show_quote && quote != nullptr;
    if (show_quote_) {
        quote_text_ = quote->text ? quote->text : "";
        quote_author_ = quote->author ? quote->author : "";
        width_ = 680;
        height_ = 240;
    } else {
        quote_text_.clear();
        quote_author_.clear();
        width_ = 560;
        height_ = 200;
    }

    if (!window_.create_centered("Gastinel Write", width_, height_)) return false;
    framebuffer_.resize(width_, height_);

    // ATTENTION au sens des suffixes, il est contre-intuitif : la lettre decrit
    // le fond DE L ICONE, pas le fond sur lequel la poser.
    //   icone_write_b.png -> tuile NOIRE, symbole blanc  (luminance moyenne 29)
    //   icone_write_w.png -> tuile BLANCHE, symbole noir (luminance moyenne 229)
    const std::string logo_path =
        branding_dir + (dark_mode() ? "/icone_write_b.png" : "/icone_write_w.png");
    std::string error;
    has_logo_ = image::load_png_file(logo_path, logo_, error);
    if (!has_logo_) {
        platform::log_error("Splash : logo absent (%s). Emplacement laisse vide.", error.c_str());
    }

    render(fonts);
    window_.show_without_activating();
    window_.present(framebuffer_);
    return true;
}

void SplashScreen::close() { window_.close(); }

void SplashScreen::render(FontSet& fonts) {
    const Theme& t = theme();
    framebuffer_.clear(t.background);

    const int logo_side = show_quote_ ? 130 : kLogoSide;
    const int logo_padding = show_quote_ ? 24 : kPadding;
    const int text_gap = show_quote_ ? 24 : kTextGap;

    // --- Logo, a gauche, centre verticalement ---
    if (has_logo_ && !logo_.empty()) {
        int draw_w = logo_side;
        int draw_h = logo_side;
        if (logo_.width > logo_.height) {
            draw_h = logo_.height * logo_side / logo_.width;
        } else if (logo_.height > logo_.width) {
            draw_w = logo_.width * logo_side / logo_.height;
        }

        const int logo_x = logo_padding + (logo_side - draw_w) / 2;
        const int logo_y = (height_ - draw_h) / 2;
        render::blit_image(framebuffer_, logo_, logo_x, logo_y, draw_w, draw_h);
    }

    const font::TtfFont& semibold = fonts.font(Weight::SemiBold);
    const font::TtfFont& regular = fonts.font(Weight::Regular);
    const int text_x = logo_padding + logo_side + text_gap;
    const int max_text_w = width_ - text_x - logo_padding;

    if (show_quote_) {
        constexpr float kHeaderSize = 11.0f;
        constexpr float kQuoteSize = 15.0f;
        constexpr float kAuthorSize = 13.0f;

        render::GlyphAtlas& header_atlas = fonts.atlas(Weight::SemiBold, kHeaderSize);
        render::GlyphAtlas& quote_atlas = fonts.atlas(Weight::Regular, kQuoteSize);
        render::GlyphAtlas& author_atlas = fonts.atlas(Weight::SemiBold, kAuthorSize);

        const std::string full_quote = "« " + quote_text_ + " »";
        const std::vector<std::string> lines = wrap_text(full_quote, regular, quote_atlas, max_text_w);

        const int header_lh = render::line_height(semibold, kHeaderSize);
        const int quote_lh = render::line_height(regular, kQuoteSize);
        const int author_lh = render::line_height(semibold, kAuthorSize);

        const int total_h = header_lh + 8 + static_cast<int>(lines.size()) * (quote_lh + 4) + 8 + author_lh;
        int cur_y = (height_ - total_h) / 2;

        render::draw_utf8(framebuffer_, "CITATION DU JOUR", text_x,
                          cur_y + render::ascent_pixels(semibold, kHeaderSize),
                          semibold, header_atlas, t.subtle);
        cur_y += header_lh + 8;

        for (const auto& line : lines) {
            render::draw_utf8(framebuffer_, line, text_x,
                              cur_y + render::ascent_pixels(regular, kQuoteSize),
                              regular, quote_atlas, t.foreground);
            cur_y += quote_lh + 4;
        }
        cur_y += 4;

        const std::string author_str = "— " + quote_author_;
        render::draw_utf8(framebuffer_, author_str, text_x,
                          cur_y + render::ascent_pixels(semibold, kAuthorSize),
                          semibold, author_atlas, t.subtle);
    } else {
        render::GlyphAtlas& bold_atlas = fonts.atlas(Weight::SemiBold, kTaglineSize);
        render::GlyphAtlas& regular_atlas = fonts.atlas(Weight::Regular, kSubtitleSize);

        const int tagline_line = render::line_height(semibold, kTaglineSize);
        const int subtitle_line = render::line_height(regular, kSubtitleSize);
        const int block_height = tagline_line + kLineGap + subtitle_line;

        const int block_top = (height_ - block_height) / 2;

        const int tagline_baseline = block_top + render::ascent_pixels(semibold, kTaglineSize);
        render::draw_utf8(framebuffer_, kTagline, text_x, tagline_baseline, semibold, bold_atlas,
                          t.foreground);

        const int subtitle_baseline =
            block_top + tagline_line + kLineGap + render::ascent_pixels(regular, kSubtitleSize);
        render::draw_utf8(framebuffer_, kSubtitle, text_x, subtitle_baseline, regular, regular_atlas,
                          t.subtle);
    }

    // Filet de contour : la fenetre est blanche et sans chrome, sans ce trait
    // elle naurait aucune limite visible sur un fond clair.
    render::stroke_rect(framebuffer_, 0, 0, width_, height_, 1, t.foreground);
}

} // namespace gw::app
