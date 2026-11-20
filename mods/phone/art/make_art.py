"""Turn the phone picture (mods/phone/phone-ts2.png, any size up to 40 x 80, transparent background) into
the DS tiles and 16-colour palette the touch screen draws instead of the mixer's blender.
Writes mods/phone/code/phone_art.inc. Run it after changing the picture, then
`python urbz.py patch build phone`.

    python mods/phone/art/make_art.py [picture.png]
"""
import os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.dirname(HERE)
TW, TH = 5, 10                                    # the picture's box in tiles (40 x 80 px)


def main(argv):
    src = argv[0] if argv else os.path.join(MOD, 'phone-ts2.png')
    im = Image.open(src).convert('RGBA')
    if im.width > TW * 8 or im.height > TH * 8:
        im.thumbnail((TW * 8, TH * 8), Image.LANCZOS)
    box = Image.new('RGBA', (TW * 8, TH * 8), (0, 0, 0, 0))
    box.paste(im, ((TW * 8 - im.width) // 2, (TH * 8 - im.height) // 2), im)
    solid = box.convert('RGB')
    q = solid.quantize(colors=15, method=Image.MEDIANCUT)
    pal = q.getpalette()[:45]
    idx = q.load()
    alpha = box.getchannel('A').load()
    colours = [0x0000] + [((pal[3 * k] >> 3) | (pal[3 * k + 1] >> 3) << 5 | (pal[3 * k + 2] >> 3) << 10)
                          for k in range(15)]
    words = []
    for ty in range(TH):
        for tx in range(TW):
            for y in range(8):
                w = 0
                for x in range(8):
                    px, py = tx * 8 + x, ty * 8 + y
                    v = 0 if alpha[px, py] < 128 else idx[px, py] + 1
                    w |= v << (4 * x)
                words.append(w)
    out = os.path.join(MOD, 'code', 'phone_art.inc')
    with open(out, 'w') as f:
        f.write('/* Made by mods/phone/art/make_art.py from %s. */\n' % os.path.basename(src))
        f.write('#define ART_W %d\n#define ART_H %d\n' % (TW, TH))
        f.write('static const u16 art_pal[16] = {%s};\n' % ', '.join('0x%04X' % c for c in colours))
        f.write('static const u32 art_tiles[%d] = {\n' % len(words))
        for k in range(0, len(words), 8):
            f.write('    %s,\n' % ', '.join('0x%08X' % w for w in words[k:k + 8]))
        f.write('};\n')
    print('wrote', out, '(%d x %d tiles, %d colours)' % (TW, TH, len(set(colours))))


if __name__ == '__main__':
    main(sys.argv[1:])
