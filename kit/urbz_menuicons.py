"""A new picture for a button a mod adds to Urb Info (the city's bottom-screen menu).

A mod names a 32 x 32 PNG (transparent background, up to 15 colours) in its mod.json:

    "urb_info_icon": "icon.png"

At build time the kit adds it to Urb Info's own picture sheet as frame 4, with its colours in palette row 4
(rows 4-15 of the sheet's palette are unused filler in the game). The mod's button then uses icon frame 4
(`x | y << 8 | 4 << 16`). Urb Info's sheet: gfx 10362, layout 10363, palette 10361 (menu 0x020F5744,
docs/systems.md "Bottom-screen menus"); a button with icon frame k is drawn in palette row k.
The files are made from your game's own into the hidden generated mod build/new-menu-icons (nothing from the
game is committed). Only one mod can add a picture this way for now.
"""
import json, os, shutil, struct, sys

KIT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # the repo (this file is in kit/)
sys.path.insert(0, os.path.join(KIT, 'kit'))
GFX, LAYOUT, PALETTE = 10362, 10363, 10361        # Urb Info's sheet (file numbers in project/assets)
FRAME = 4                                         # the new picture's frame = its palette row
CANVAS, ORIGIN = 32, (16, 16)                     # the button's (x, y) is the picture's centre


class MenuIconError(Exception):
    pass


def _colours(img, mod):
    """The PNG's opaque colours (at most 15), in a fixed order."""
    from urbz_palette import bgr555, to555
    cols = []
    for (r, g, b, a) in img.getdata():
        if a >= 128:
            c = bgr555(to555((r, g, b)))           # the DS keeps 5 bits per channel
            if c not in cols:
                cols.append(c)
    if len(cols) > 15:
        raise MenuIconError('mod "%s": its Urb Info icon has %d colours (after 15-bit rounding); '
                            'keep it to 15 plus see-through' % (mod, len(cols)))
    return sorted(cols)


def menu_icons_mod(proj, mod_dirs, out_dir):
    """Mods' "urb_info_icon" -> the hidden mod out_dir (Urb Info's sheet with frame 4). (dirs, log line)."""
    from PIL import Image
    import urbz_composite as C
    from urbz_anims import frame_data, CHUNK_FLAGS
    from urbz_palette import to555
    from urbzcomp import pack_chunk
    want = []
    for m in mod_dirs:
        p = os.path.join(m, 'mod.json')
        if os.path.exists(p):
            icon = json.load(open(p)).get('urb_info_icon')
            if icon:
                want.append((os.path.basename(os.path.normpath(m)), os.path.join(m, icon)))
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    if not want:
        return None, ''
    if len(want) > 1:
        raise MenuIconError('mods %s each add an Urb Info icon; only one can for now'
                            % ', '.join('"%s"' % w[0] for w in want))
    mod, path = want[0]
    if not os.path.exists(path):
        raise MenuIconError('mod "%s": its Urb Info icon %s is missing' % (mod, path))
    img = Image.open(path).convert('RGBA')
    if img.size != (CANVAS, CANVAS):
        raise MenuIconError('mod "%s": its Urb Info icon must be %d x %d (it is %d x %d)'
                            % ((mod, CANVAS, CANVAS) + img.size))
    cols = _colours(img, mod)
    pal = [(255, 0, 255)] + cols + [(0, 0, 0)] * (15 - len(cols))

    read = lambda n: open(os.path.join(proj, 'assets', '%05d.bin' % n), 'rb').read()
    gfx, layout, palette = bytearray(read(GFX)), read(LAYOUT), bytearray(read(PALETTE))
    lay = C.parse_layout(layout)
    if len(lay.entries) != FRAME:
        raise MenuIconError('Urb Info\'s sheet has %d frames (the game has 4); was it changed by a mod?'
                            % len(lay.entries))
    e, content = frame_data(img, pal, lay.entries[0].extra, CANVAS, ORIGIN)
    while len(gfx) % 4:
        gfx += b'\0'
    e.index, e.chunk_off = FRAME, len(gfx)
    gfx += pack_chunk(CHUNK_FLAGS, content)
    while len(gfx) % 4:
        gfx += b'\0'
    lay.entries.append(e)
    lay.head = bytes([max(x.w for x in lay.entries), max(x.h for x in lay.entries)]) + lay.head[2:]
    for i, c in enumerate(pal):
        struct.pack_into('<H', palette, 32 * FRAME + 2 * i, to555(c))

    os.makedirs(os.path.join(out_dir, 'assets'))
    for n, data in ((GFX, gfx), (LAYOUT, C.build_layout(lay)), (PALETTE, palette)):
        open(os.path.join(out_dir, 'assets', '%05d.bin' % n), 'wb').write(bytes(data))
    json.dump({'name': 'new-menu-icons', 'description': 'Kit: a mod\'s Urb Info button picture (urb_info_icon)',
               'toggle': False, 'hidden': True}, open(os.path.join(out_dir, 'mod.json'), 'w'), indent=1)
    return [out_dir], 'menu icons: Urb Info frame %d from mod "%s" (%d colours)' % (FRAME, mod, len(cols))
