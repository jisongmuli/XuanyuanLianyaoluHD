"""Rasterize the licensed Noto font at actual HD glyph resolution."""
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
texts = json.loads((ROOT / 'recovered/texts.json').read_text(encoding='utf8'))
chars = sorted(set(''.join(t['text'] for t in texts) + ''.join(chr(c) for c in range(32, 127)) + '□'))
font = ImageFont.truetype(str(ROOT / 'remaster/assets/fonts/NotoSansSC.subset.ttf'), 72)
boxes = {ch: font.getbbox(ch) for ch in chars}
# Pillow's default text origin is above the baseline, not the ink's top edge.
# Keep the complete ascent and descent of this character set in one shared cell.
visible = [box for box in boxes.values() if box[3] > box[1]]
ink_top, ink_bottom = min(b[1] for b in visible), max(b[3] for b in visible)
padding = 3
scale = (72 - 2 * padding) / (ink_bottom - ink_top)
binary = bytearray()
index = []
checks = []
for ch in chars:
    im = Image.new('L', (72, 72))
    box = boxes[ch]
    width = 36 if ord(ch) < 256 else 72
    if box[2] > box[0] and box[3] > box[1]:
        # Rasterize all ink before resizing: drawing directly into the 72px cell
        # used to discard descenders and the lower strokes of Chinese glyphs.
        body = Image.new('L', (box[2] - box[0] + 8, box[3] - box[1] + 8))
        ImageDraw.Draw(body).text((4 - box[0], 4 - box[1]), ch, fill=255, font=font)
        body = body.crop((4, 4, body.width - 4, body.height - 4))
        target_w = max(1, min(width - 2 * padding, round(body.width * scale)))
        target_h = max(1, round(body.height * scale))
        y = padding + round((box[1] - ink_top) * scale)
        assert y + target_h <= 72 - padding, (ch, box, y, target_h)
        body = body.resize((target_w, target_h), Image.Resampling.LANCZOS)
        im.paste(body, ((width - target_w) // 2, y))
    elif ch == '□':
        ImageDraw.Draw(im).rectangle((padding + 10, padding + 10, 72 - padding - 11, 72 - padding - 11), outline=255, width=3)
    bbox = im.getbbox()
    assert bbox is None or (bbox[0] >= padding and bbox[1] >= padding and bbox[2] <= width - padding and bbox[3] <= 72 - padding), (ch, bbox)
    checks.append({'code': ord(ch), 'ink_bounds': bbox, 'full_source_bounds': box})
    index.append({'code': ord(ch), 'offset': len(binary)})
    binary.extend(im.tobytes())
(ROOT / 'native_hd/assets/font.alpha').write_bytes(binary)
(ROOT / 'native_hd/assets/font-index.json').write_text(json.dumps(index), encoding='utf8')
(ROOT / 'native_hd/assets/font-metrics.json').write_text(json.dumps({'cell': [72, 72], 'ink_top': ink_top, 'ink_bottom': ink_bottom, 'padding': padding, 'scale': scale, 'glyphs': checks}, ensure_ascii=False, indent=2), encoding='utf8')
print(f'{len(index)} HD glyphs, {len(binary)} alpha bytes')
