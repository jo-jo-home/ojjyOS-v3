#!/usr/bin/env python3
"""
mkicons.py - the ojjyOS icon set (Tahoe style), generated from code.

Every icon is drawn with vector shapes at 4x (512 px) and downsampled with
a premultiplied Lanczos filter to 128x128 RGBA, so edges are smooth at any
display size (including 2x HiDPI). Output: kernel/src/ui/icons_data.c with
one array per icon plus a lookup table; --sheet writes a PNG contact sheet.

Usage: python3 tools/mkicons.py kernel/src/ui/icons_data.c [--sheet out.png]
ojjyOS - Created by Jonas Lee
"""
import math
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

N = 128            # output size
SS = 4             # supersampling
B = N * SS         # working canvas size

# ---------------------------------------------------------------- helpers

def canvas():
    return Image.new("RGBA", (B, B), (0, 0, 0, 0))

def mask():
    return Image.new("L", (B, B), 0)

def S(v):          # design units are on a 128 grid
    return v * SS

def squircle_points(cx, cy, r, n=5.0, steps=720):
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        x = abs(c) ** (2 / n) * r * (1 if c >= 0 else -1)
        y = abs(s) ** (2 / n) * r * (1 if s >= 0 else -1)
        pts.append((cx + x, cy + y))
    return pts

def squircle_mask(inset=10):
    m = mask()
    r = S(64 - inset)
    ImageDraw.Draw(m).polygon(squircle_points(S(64), S(64 - 1), r), fill=255)
    return m

def vgradient(top, bottom, y0=0, y1=None):
    """Full-canvas vertical gradient between two RGB(A) colours."""
    y1 = B if y1 is None else y1
    g = Image.new("RGBA", (1, B))
    for y in range(B):
        t = min(1.0, max(0.0, (y - y0) / max(1, y1 - y0)))
        c = tuple(int(round(a + (b - a) * t)) for a, b in zip(top, bottom))
        if len(c) == 3:
            c = c + (255,)
        g.putpixel((0, y), c)
    return g.resize((B, B))

def radial(center, inner, outer, radius):
    g = Image.new("RGBA", (B, B))
    px = g.load()
    cx, cy = center
    for y in range(0, B, 2):
        for x in range(0, B, 2):
            t = min(1.0, math.hypot(x - cx, y - cy) / radius)
            c = tuple(int(round(a + (b - a) * t)) for a, b in zip(inner, outer))
            if len(c) == 3:
                c = c + (255,)
            for dy in (0, 1):
                for dx in (0, 1):
                    px[x + dx, y + dy] = c
    return g

def fill(img, m, paint):
    """Composite `paint` (colour tuple or RGBA image) through mask m."""
    if isinstance(paint, tuple):
        paint = Image.new("RGBA", (B, B), paint if len(paint) == 4 else paint + (255,))
    layer = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    layer.paste(paint, (0, 0), m)
    img.alpha_composite(layer)

def shape(fn):
    m = mask()
    fn(ImageDraw.Draw(m))
    return m

def intersect(a, b):
    return ImageChops.multiply(a, b)

def drop_shadow(img, m, dy=3.0, blur=4.0, alpha=0.32):
    sh = m.filter(ImageFilter.GaussianBlur(S(blur)))
    sh = ImageChops.offset(sh, 0, int(S(dy)))
    sh = sh.point(lambda v: int(v * alpha))
    layer = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    layer.paste((8, 24, 56, 255), (0, 0), sh)
    img.alpha_composite(layer)

def gloss(img, m, strength=70):
    """Soft top highlight + thin inner rim, like Tahoe's glassy icons."""
    hl = vgradient((255, 255, 255, strength), (255, 255, 255, 0), 0, S(70))
    fill(img, m, hl)
    rim = ImageChops.subtract(m, m.filter(ImageFilter.MinFilter(5)))
    fill(img, rim, (255, 255, 255, 60))

def app_base(top, bottom, inset=10):
    img = canvas()
    m = squircle_mask(inset)
    drop_shadow(img, m)
    fill(img, m, vgradient(top, bottom, S(inset), S(128 - inset)))
    return img, m

def finish(img, m=None, shine=55):
    if m is not None and shine:
        gloss(img, m, shine)
    return img

def rr(d, box, r, fill_v=255):
    d.rounded_rectangle([S(v) for v in box], radius=S(r), fill=fill_v)

# ---------------------------------------------------------------- icons

def icon_finder():
    img, m = app_base((120, 200, 255), (24, 110, 235))
    # right half: lighter face, divided by a profile curve
    right = mask()
    d = ImageDraw.Draw(right)
    pts = [(S(66), S(0)), (S(128), S(0)), (S(128), S(128)), (S(60), S(128))]
    curve = [(S(60 + 6 * math.sin(t / 10)), S(t * 1.0)) for t in range(0, 129, 4)]
    d.polygon([(S(128), S(0))] + [(x + S(4), y) for x, y in curve] + [(S(128), S(128))], fill=255)
    fill(img, intersect(right, m), vgradient((236, 246, 255), (190, 220, 250), S(10), S(118)))
    # eyes
    for ex in (40, 84):
        fill(img, shape(lambda d, ex=ex: rr(d, (ex - 3.5, 36, ex + 3.5, 58), 3.5)), (22, 40, 70))
    # nose line + smile
    nose = shape(lambda d: d.line([(S(66), S(36)), (S(58), S(70)), (S(68), S(72))], fill=255, width=S(4), joint="curve"))
    fill(img, intersect(nose, m), (22, 40, 70))
    smile = shape(lambda d: d.arc([S(30), S(52), S(98), S(96)], 25, 155, fill=255, width=S(4)))
    fill(img, smile, (22, 40, 70))
    return finish(img, m, 40)

def icon_terminal():
    img, m = app_base((70, 76, 88), (20, 22, 28))
    # inner screen
    scr = shape(lambda d: rr(d, (20, 26, 108, 102), 10))
    fill(img, scr, vgradient((18, 22, 30), (8, 10, 14), S(26), S(102)))
    stroke = shape(lambda d: d.line([(S(32), S(46)), (S(48), S(58)), (S(32), S(70))], fill=255, width=S(6), joint="curve"))
    fill(img, stroke, (120, 230, 150))
    fill(img, shape(lambda d: rr(d, (54, 66, 76, 72), 3)), (230, 240, 236))
    return finish(img, m, 35)

def icon_textedit():
    img, m = app_base((250, 250, 248), (222, 228, 236))
    # page lines
    for i, y in enumerate(range(44, 98, 9)):
        w = 74 if i % 3 != 2 else 50
        fill(img, shape(lambda d, y=y, w=w: rr(d, (28, y, 28 + w, y + 3.5), 1.75)), (150, 162, 178))
    # header "A"
    fill(img, shape(lambda d: rr(d, (28, 24, 70, 34), 3)), (32, 120, 230))
    # pen
    pen = shape(lambda d: d.polygon([(S(92), S(18)), (S(104), S(30)), (S(70), S(64)), (S(56), S(68)), (S(60), S(54))], fill=255))
    fill(img, pen, vgradient((255, 196, 60), (232, 128, 30), S(18), S(68)))
    tip = shape(lambda d: d.polygon([(S(60), S(54)), (S(70), S(64)), (S(56), S(68))], fill=255))
    fill(img, tip, (60, 50, 50))
    return finish(img, m, 30)

def icon_notes():
    img, m = app_base((255, 255, 255), (238, 238, 232))
    top = intersect(shape(lambda d: d.rectangle([0, 0, B, S(42)], fill=255)), m)
    fill(img, top, vgradient((255, 222, 89), (248, 186, 20), S(10), S(42)))
    for y in range(56, 110, 12):
        fill(img, shape(lambda d, y=y: d.rectangle([S(14), S(y), S(114), S(y + 1.6)], fill=255)), (205, 205, 200))
    # dotted perforation
    for x in range(22, 110, 10):
        fill(img, shape(lambda d, x=x: d.ellipse([S(x - 1.6), S(40.4), S(x + 1.6), S(43.6)], fill=255)), (255, 255, 255, 180))
    return finish(img, m, 25)

def icon_preview():
    img, m = app_base((110, 190, 250), (36, 112, 214))
    # back photo
    back = shape(lambda d: rr(d, (34, 22, 102, 80), 6))
    back = back.rotate(8, resample=Image.BICUBIC, center=(S(68), S(51)))
    drop_shadow(img, back, 2, 3, 0.25)
    fill(img, back, (245, 248, 252))
    # front photo with landscape
    front = shape(lambda d: rr(d, (22, 40, 94, 102), 6))
    drop_shadow(img, front, 2, 3, 0.3)
    fill(img, front, (255, 255, 255))
    pic = shape(lambda d: rr(d, (28, 46, 88, 90), 3))
    fill(img, pic, vgradient((120, 200, 255), (210, 240, 255), S(46), S(90)))
    hills = shape(lambda d: d.polygon([(S(28), S(90)), (S(28), S(78)), (S(46), S(62)), (S(60), S(76)), (S(70), S(68)), (S(88), S(84)), (S(88), S(90))], fill=255))
    fill(img, hills, vgradient((60, 170, 110), (30, 120, 80), S(62), S(90)))
    fill(img, shape(lambda d: d.ellipse([S(70), S(50), S(80), S(60)], fill=255)), (255, 214, 80))
    return finish(img, m, 40)

def gear_mask(cx, cy, r_out, r_in, teeth, hole):
    pts = []
    for i in range(teeth * 2):
        a0 = math.pi * 2 * i / (teeth * 2)
        r = r_out if i % 2 == 0 else r_in
        for k in range(6):
            a = a0 + (math.pi * 2 / (teeth * 2)) * (k / 5) * 0.999
            pts.append((S(cx + r * math.cos(a)), S(cy + r * math.sin(a))))
    m = mask()
    d = ImageDraw.Draw(m)
    d.polygon(pts, fill=255)
    d.ellipse([S(cx - hole), S(cy - hole), S(cx + hole), S(cy + hole)], fill=0)
    return m

def icon_settings():
    img, m = app_base((178, 186, 196), (96, 104, 116))
    ring = shape(lambda d: d.ellipse([S(18), S(17), S(110), S(109)], fill=255))
    fill(img, ring, vgradient((70, 76, 86), (40, 44, 52), S(17), S(109)))
    g = gear_mask(64, 63, 40, 33, 12, 13)
    drop_shadow(img, g, 1.5, 2, 0.4)
    fill(img, g, vgradient((236, 240, 244), (160, 168, 178), S(23), S(103)))
    inner = gear_mask(64, 63, 22, 18, 8, 9)
    fill(img, inner, vgradient((120, 128, 140), (200, 206, 214), S(41), S(85)))
    return finish(img, m, 35)

def icon_about():
    img, m = app_base((88, 176, 250), (20, 86, 200))
    # Tahoe waves
    for i, (y, a) in enumerate([(78, 90), (90, 120), (102, 160)]):
        wave = mask()
        d = ImageDraw.Draw(wave)
        pts = [(S(x), S(y + 6 * math.sin(x / 13 + i))) for x in range(0, 129, 2)]
        d.polygon(pts + [(S(128), S(128)), (0, S(128))], fill=255)
        fill(img, intersect(wave, m), (255, 255, 255, a // 2))
    disc = shape(lambda d: d.ellipse([S(44), S(22), S(84), S(62)], fill=255))
    drop_shadow(img, disc, 1.5, 2, 0.3)
    fill(img, disc, (255, 255, 255))
    fill(img, shape(lambda d: d.ellipse([S(60.5), S(29), S(67.5), S(36)], fill=255)), (30, 110, 220))
    fill(img, shape(lambda d: rr(d, (60.5, 39, 67.5, 55), 3.5)), (30, 110, 220))
    return finish(img, m, 45)

def icon_calendar():
    img, m = app_base((255, 255, 255), (236, 238, 242))
    top = intersect(shape(lambda d: d.rectangle([0, 0, B, S(40)], fill=255)), m)
    fill(img, top, vgradient((255, 98, 88), (226, 52, 48), S(10), S(40)))
    # "OCT" pills and big date as blocky strokes
    from PIL import ImageFont
    try:
        f1 = ImageFont.truetype("fonts/Inter-SemiBold.ttf", S(15))
        f2 = ImageFont.truetype("fonts/Inter-Medium.ttf", S(54))
        t = mask(); d = ImageDraw.Draw(t)
        d.text((S(64), S(27)), "OCT", font=f1, fill=255, anchor="mm")
        fill(img, t, (255, 255, 255))
        t = mask(); d = ImageDraw.Draw(t)
        d.text((S(64), S(80)), "18", font=f2, fill=255, anchor="mm")
        fill(img, t, (40, 44, 52))
    except OSError:
        pass
    return finish(img, m, 25)

def icon_calculator():
    img, m = app_base((84, 88, 98), (34, 36, 42))
    fill(img, shape(lambda d: rr(d, (22, 20, 106, 42), 6)), (170, 210, 160))
    for r in range(3):
        for c in range(4):
            x, y = 22 + c * 22, 50 + r * 20
            col = (255, 160, 40) if c == 3 else (120, 126, 138) if r == 0 else (88, 92, 102)
            fill(img, shape(lambda d, x=x, y=y: d.ellipse([S(x), S(y), S(x + 16), S(y + 16)], fill=255)), col)
    return finish(img, m, 30)

def icon_monitor():
    img, m = app_base((44, 50, 62), (12, 14, 20))
    scr = shape(lambda d: rr(d, (18, 24, 110, 104), 8))
    fill(img, scr, (6, 10, 16))
    for x in range(26, 104, 13):
        fill(img, shape(lambda d, x=x: d.rectangle([S(x), S(28), S(x + 0.7), S(100)], fill=255)), (30, 80, 60, 160))
    for y in range(32, 100, 13):
        fill(img, shape(lambda d, y=y: d.rectangle([S(22), S(y), S(106), S(y + 0.7)], fill=255)), (30, 80, 60, 160))
    pts = [(22, 76), (36, 74), (44, 58), (52, 86), (62, 40), (72, 80), (80, 66), (90, 70), (106, 52)]
    line = shape(lambda d: d.line([(S(x), S(y)) for x, y in pts], fill=255, width=S(4), joint="curve"))
    glow = line.filter(ImageFilter.GaussianBlur(S(3)))
    fill(img, glow, (60, 255, 160, 120))
    fill(img, line, (90, 255, 170))
    return finish(img, m, 30)

def icon_web():
    img, m = app_base((240, 246, 252), (196, 212, 230))
    globe = shape(lambda d: d.ellipse([S(18), S(17), S(110), S(109)], fill=255))
    drop_shadow(img, globe, 1.5, 2, 0.3)
    fill(img, globe, radial((S(52), S(44)), (110, 200, 255), (16, 92, 210), S(78)))
    lines = mask()
    d = ImageDraw.Draw(lines)
    for w in (46, 30, 12):
        d.ellipse([S(64 - w), S(17), S(64 + w), S(109)], outline=255, width=S(2))
    for y, w in ((40, 40), (63, 46), (86, 40)):
        d.line([(S(64 - w), S(y)), (S(64 + w), S(y))], fill=255, width=S(2))
    fill(img, intersect(lines, globe), (255, 255, 255, 150))
    # compass needle
    n1 = shape(lambda d: d.polygon([(S(64), S(28)), (S(71), S(63)), (S(57), S(63))], fill=255))
    n2 = shape(lambda d: d.polygon([(S(64), S(98)), (S(71), S(63)), (S(57), S(63))], fill=255))
    fill(img, n1, (255, 76, 66))
    fill(img, n2, (250, 250, 250))
    return finish(img, m, 35)

def icon_folder():
    img = canvas()
    back = shape(lambda d: (rr(d, (10, 26, 118, 108), 9), rr(d, (10, 20, 54, 36), 7)))
    drop_shadow(img, back, 2.5, 3.5, 0.28)
    fill(img, back, vgradient((86, 168, 238), (52, 130, 220), S(20), S(108)))
    front = shape(lambda d: rr(d, (10, 38, 118, 108), 9))
    fill(img, front, vgradient((134, 204, 252), (80, 164, 240), S(38), S(108)))
    edge = shape(lambda d: d.rectangle([S(14), S(38), S(114), S(40)], fill=255))
    fill(img, edge, (255, 255, 255, 110))
    return img

def icon_file():
    img = canvas()
    page = shape(lambda d: d.polygon([(S(30), S(10)), (S(80), S(10)), (S(100), S(30)), (S(100), S(118)), (S(30), S(118))], fill=255))
    drop_shadow(img, page, 2, 3, 0.28)
    fill(img, page, vgradient((255, 255, 255), (236, 240, 246), S(10), S(118)))
    fold = shape(lambda d: d.polygon([(S(80), S(10)), (S(100), S(30)), (S(80), S(30))], fill=255))
    fill(img, fold, (206, 214, 226))
    for y in range(44, 104, 10):
        fill(img, shape(lambda d, y=y: rr(d, (40, y, 90 if y % 30 else 74, y + 3), 1.5)), (170, 182, 198))
    return img

def icon_disk():
    img = canvas()
    body = shape(lambda d: rr(d, (14, 34, 114, 98), 12))
    drop_shadow(img, body, 2.5, 3.5, 0.3)
    fill(img, body, vgradient((236, 240, 246), (176, 186, 200), S(34), S(98)))
    face = shape(lambda d: rr(d, (14, 70, 114, 98), 12))
    fill(img, intersect(face, body), vgradient((150, 162, 178), (118, 130, 148), S(70), S(98)))
    fill(img, shape(lambda d: d.ellipse([S(96), S(80), S(104), S(88)], fill=255)), (90, 230, 130))
    fill(img, shape(lambda d: rr(d, (26, 82, 70, 86), 2)), (90, 100, 116))
    return img

def icon_trash():
    img = canvas()
    can = shape(lambda d: d.polygon([(S(28), S(30)), (S(100), S(30)), (S(92), S(116)), (S(36), S(116))], fill=255))
    drop_shadow(img, can, 2, 3, 0.28)
    fill(img, can, vgradient((236, 242, 250, 235), (188, 202, 220, 235), S(30), S(116)))
    ribs = mask()
    d = ImageDraw.Draw(ribs)
    for x0, x1 in ((46, 48), (63, 63), (82, 80)):
        d.line([(S(x0), S(42)), (S(x1), S(106))], fill=255, width=S(3))
    fill(img, ribs, (150, 166, 188))
    lid = shape(lambda d: rr(d, (22, 20, 106, 30), 4))
    fill(img, lid, vgradient((214, 224, 236), (164, 178, 198), S(20), S(30)))
    return img

ICONS = [
    ("ICON_FINDER", icon_finder), ("ICON_TERMINAL", icon_terminal), ("ICON_TEXTEDIT", icon_textedit),
    ("ICON_NOTES", icon_notes), ("ICON_PREVIEW", icon_preview), ("ICON_SETTINGS", icon_settings),
    ("ICON_ABOUT", icon_about), ("ICON_CALENDAR", icon_calendar), ("ICON_CALCULATOR", icon_calculator),
    ("ICON_MONITOR", icon_monitor), ("ICON_WEB", icon_web), ("ICON_FOLDER", icon_folder),
    ("ICON_FILE", icon_file), ("ICON_DISK", icon_disk), ("ICON_TRASH", icon_trash),
]

def downsample(img):
    r, g, b, a = img.split()
    pm = Image.merge("RGBA", [ImageChops.multiply(c, a) for c in (r, g, b)] + [a])
    small = pm.resize((N, N), Image.LANCZOS)
    out = bytearray()
    for pr, pg, pb, pa in small.getdata():
        if pa:
            pr, pg, pb = (min(255, pr * 255 // pa), min(255, pg * 255 // pa), min(255, pb * 255 // pa))
        else:
            pr = pg = pb = 0
        out += bytes((pr, pg, pb, pa))
    return bytes(out)

def main(argv):
    out_path = argv[1]
    sheet = argv[argv.index("--sheet") + 1] if "--sheet" in argv else None
    rendered = [(name, downsample(fn())) for name, fn in ICONS]
    lines = ["/* Generated by tools/mkicons.py - do not edit. ojjyOS - Created by Jonas Lee */",
             '#include "icons.h"', ""]
    for name, data in rendered:
        lines.append(f"static const uint8_t {name.lower()}_px[ICON_PX * ICON_PX * 4] = {{")
        for i in range(0, len(data), 32):
            lines.append("    " + ",".join(str(v) for v in data[i:i + 32]) + ",")
        lines.append("};")
    lines.append("")
    lines.append("const uint8_t *const icon_pixels[ICON_COUNT] = {")
    for name, _ in rendered:
        lines.append(f"    [{name}] = {name.lower()}_px,")
    lines.append("};")
    open(out_path, "w").write("\n".join(lines) + "\n")
    print(f"{len(rendered)} icons ({N}x{N}) -> {out_path}")
    if sheet:
        cols = 5
        rows = (len(rendered) + cols - 1) // cols
        bg = Image.new("RGBA", (cols * 150, rows * 150 * 2), (0, 0, 0, 255))
        bgp = bg.load()
        for y in range(bg.size[1]):
            c = (60, 140, 220, 255) if y < rows * 150 else (226, 236, 246, 255)
            for x in range(bg.size[0]):
                bgp[x, y] = c
        for i, (name, data) in enumerate(rendered):
            ic = Image.frombytes("RGBA", (N, N), data)
            for half in range(2):
                bg.alpha_composite(ic, ((i % cols) * 150 + 11, (i // cols) * 150 + 11 + half * rows * 150))
        bg.save(sheet)

if __name__ == "__main__":
    main(sys.argv)
