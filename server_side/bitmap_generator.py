"""
E-paper boot screen generator.

Display panel: 128 x 296 (portrait, width x height) — e.g. a typical
2.9" e-ink strip. Content is authored "longways" (296 x 128, landscape)
because it's much easier to lay out readable text that way, then
converted into the panel's native portrait orientation.
"""

from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------------------
# Panel geometry
# ---------------------------------------------------------------------------
FINAL_SIZE = (128, 296)        # panel's native (w, h) — portrait, longways
TEXT_CANVAS_SIZE = (296, 128)  # authoring canvas (w, h) — landscape

FONT_BOLD_CANDIDATES = [
    "/System/Library/Fonts/HelveticaNeue.ttc",              # macOS built-in
    "/System/Library/Fonts/Supplemental/Arial Bold.ttf",     # macOS built-in
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",  # Linux fallback
]
FONT_REG_CANDIDATES = [
    "/System/Library/Fonts/HelveticaNeue.ttc",              # macOS built-in
    "/System/Library/Fonts/Supplemental/Arial.ttf",          # macOS built-in
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",       # Linux fallback
]


def load_font(size, bold=True):
    candidates = FONT_BOLD_CANDIDATES if bold else FONT_REG_CANDIDATES
    for path in candidates:
        try:
            if path.endswith(".ttc"):
                # HelveticaNeue.ttc is a collection; index 1 is Bold,
                # index 0 is Regular on macOS's system copy.
                return ImageFont.truetype(path, size, index=1 if bold else 0)
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def to_panel_orientation(img: Image.Image, final_size=FINAL_SIZE, mirror=False) -> Image.Image:
    """
    Convert a landscape-authored image (drawn right-reading, normal
    orientation) into the panel's native portrait orientation.

    Plain `rotate(90, expand=True)` is what makes the text come out
    readable in a normal viewer/preview. Some e-paper controllers write
    their framebuffer mirrored though, so if text shows up backwards on
    your actual hardware, call this with mirror=True to pre-compensate.
    """
    img = img.convert("1") if img.mode != "1" else img

    if mirror:
        img = img.transpose(Image.FLIP_LEFT_RIGHT)

    rotated = img.rotate(90, expand=True)

    if rotated.size != final_size:
        rotated = rotated.resize(final_size)

    return rotated


def _text_size(draw, text, font):
    left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
    return right - left, bottom - top, left, top


def _draw_centered(draw, text, font, canvas_w, y, fill=0):
    w, h, left, top = _text_size(draw, text, font)
    x = (canvas_w - w) / 2 - left
    draw.text((x, y - top), text, font=font, fill=fill)
    return h


def make_boot_screen(
    title="See you countdown",
    subtitle="I love you! -Ben",
    canvas_size=TEXT_CANVAS_SIZE,
    title_size=54,
    subtitle_size=18,
    gap=18,
) -> Image.Image:
    """
    Build a clean, centered boot/splash screen: a bold title line and a
    smaller subtitle line beneath it, both horizontally centered, the
    pair vertically centered as a block. Returns a landscape-oriented
    1-bit image ready to be passed to `to_panel_orientation`.
    """
    w, h = canvas_size
    img = Image.new("1", canvas_size, color=1)  # 1 = white background
    draw = ImageDraw.Draw(img)

    title_font = load_font(title_size, bold=True)
    subtitle_font = load_font(subtitle_size, bold=False)

    # Shrink title to fit width with a little side margin, if needed.
    margin = 8
    while title_size > 10:
        title_font = load_font(title_size, bold=True)
        tw, _, _, _ = _text_size(draw, title, title_font)
        if tw <= w - 2 * margin:
            break
        title_size -= 2

    title_w, title_h, _, _ = _text_size(draw, title, title_font)
    sub_w, sub_h, _, _ = _text_size(draw, subtitle, subtitle_font)

    block_h = title_h + gap + sub_h
    top_y = (h - block_h) / 2

    # A thin rule under the title for a bit of polish.
    _draw_centered(draw, title, title_font, w, top_y)
    rule_y = top_y + title_h + gap / 2
    rule_w = min(title_w + 20, w - 2 * margin)
    draw.line(
        [((w - rule_w) / 2, rule_y), ((w + rule_w) / 2, rule_y)],
        fill=0,
        width=1,
    )
    _draw_centered(draw, subtitle, subtitle_font, w, top_y + title_h + gap)

    return img


if __name__ == "__main__":
    landscape = make_boot_screen(
        title="See You Countdown",
        subtitle="I love you! -Ben",
        title_size=36,
        subtitle_size=12,
    )
    to_panel_orientation(landscape, mirror=False).save(
        "boot_screen_no_mirror.bmp"
    )
    to_panel_orientation(landscape, mirror=True).save(
        "boot_screen_mirrored.bmp"
    )
    print("saved both variants")