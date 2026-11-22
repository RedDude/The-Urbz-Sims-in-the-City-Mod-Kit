# The Urbz DS: engine systems reference

What we know about how the game works, with the evidence for each fact.
Addresses are ARM9 RAM addresses (code loads at 0x02000000; ITCM code at 0x01FF8000).

## Assets

- **Game asset ID = file number + 1.** `assets/10747.bin` is game ID 10748.
- `0x02032CA8` = *get asset by game ID* (r0 = ID). `FUN_02032f74` is a thunk to it.
  Hooking it gives per-scene asset traces (`verify/urbz_verify.py trace`).
- `rom.bin` (FAT file 0) = u32 offset table (13,385 assets + EOF sentinel) + assets, 4-byte aligned.
- **The asset manager only fits the original assets** (proven, Phase 7): `asset_load` (0x02032B08) refuses
  game IDs >= 0x344A (13,386), and its 3 tables are sized for exactly 13,386 entries: `asset_offsets`
  0x0212352C (rom.bin's index, read at boot: 0xD128 bytes, literal 0x02032B04), `asset_ptrs` 0x02130654
  (where each loaded asset is) and `asset_refs` 0x0213D77C (a use count byte each). Code: 0x02032A60-
  0x020331F4 (5 count literals, 13 table addresses). When mods add assets, `urbz_build.py` adds a generated
  hidden code mod (`build/new-assets/`) that moves the 3 tables into the code region, sized for the build
  (9 bytes per asset of heap), and patches those literals in the ROM. Proven: proof `npc-anims` (the game
  loads a new asset; it plays in DeSmuME and the build runs in melonDS). Before this, added assets were
  silently never loaded.
- **Space (proven 2026-10-05, proof `rom-grow`):** the ROM file is not the limit. The original is 32 MB (30.4 MB
  used); DS cartridges go to 512 MB (The Sims 3 DS is 128 MB). A build padded to 128 MB writes a correct header
  (size, card size, CRCs), boots in DeSmuME and melonDS and loads an added asset from past the old end. The real
  limits are the DS's memory while playing, which emulators emulate too: the heap (~0.7-1.4 MB free, below), VRAM
  (what's on screen), 16 OBJ palette rows (people use rows 9+; imported furniture/pets each take one), ~31
  hardware pieces per drawing, the 8 KB save, and 9 bytes of heap per added asset.
- Frame scripts (an animation's frame order and timing) are assets too: pairs `{u8 frame, u8 ticks}`, then
  0xFF (hold the last frame) or 0xFD (loop). An animation row's script 0 = the default.

## Chunks

Header u32 = `flags | size << 8`. Decoded by `FUN_0202d760` / `FUN_0202d83c`:

| flags bits 4–6 | codec | in this ROM |
|---|---|---|
| 0 | stored raw (memcpy) | 241 chunks (mostly 32-byte palettes) |
| 1 | Nintendo BIOS LZ77 (chunk header doubles as LZ77 header) | 1,628 chunks |
| 2 / 3 / 4 | BIOS Huffman / RLE / custom | unused |
| 6 | EA bitstream (ITCM `0x01FF8C50`) | 62,509 chunks |

- Flags bit 7 = 16-bit delta filter after decoding (`FUN_0202d928`, `x[i] += x[i-1]`).
- Assets are usually back-to-back chunks. `scan_chunks.walk_chunks` reads them in order and
  only trusts a walk that covers the whole asset.
- Some decoded chunks end with an **embedded tile stream** `[u16 length][EA stream]`
  (no chunk header). These are screens' 4bpp tiles: 623 of them, always the last block.

## Screens

Decoded chunk layout:
1. a u16 (purpose unknown)
2. 256 BGR555 colours
3. `u16 w, u16 h` (usually 32×24 tiles = 256×192)
4. tilemap: `w*h` u16 entries (tile 10 bits, hflip bit 10, vflip bit 11, palette bits 12–15)
5. `[u16 len][EA-compressed 4bpp tiles]`

Rendering verified pixel-identical to the emulator (EA logo, asset 10747).

## Sprites

- **Sprite definition records** live in the ARM9 binary, 16 bytes each:
  `{u32 gfx game ID, u32 layout game ID, u32 palette game ID or 0, u32 param}`.
  About 9,600 records in about 209 tables, plus some pairs inlined in code.
- **Layout asset** (no chunks; full format in `urbz_composite.py`, decoded from `FUN_020ae17c`,
  `FUN_02001abc`, `FUN_02005534`, `FUN_0206e774`):
  - header: +0,+1 max frame w,h; +6 u16 entry count; **+10 u8 X, +11 u8 Y** (sizes of the
    per-entry extra data); +12 u16 entry-offset table (relative to +12); padded to 4.
  - entry (one per animation frame): +0 u8 cell count (bits 0-4; bits 5-7 flags), +2/+3 w,h
    (cells' bounding box), **+4 u16 chunk offset** into the gfx asset, +6/+8 s16 x,y (bounding-box
    origin), then X*2 + Y*6 bytes of extra data, then one **u32 per cell**: bits 0-8 signed x,
    9-17 signed y, 18-19 OBJ size, 20-21 OBJ shape, 22-31 first tile (32-byte units; always the
    running sum of earlier cells).
  - The frame's chunk is each cell's 4bpp tiles back to back (row-major within a cell, 1D
    mapping). At load the game re-spaces cells to 64-byte boundaries in VRAM. Flip and palette
    come from the sprite, not the cell.
  - Checked over all 5,331 pairs (146,503 cells): 100% legal shapes and tile counts; in the city
    the decoded OAM matched the hardware 15/15 draws and the rendered player 678/680 pixels.
- **Gfx asset** = back-to-back chunks, one per frame. Simple frames (one cell) are w x h tiles
  row-major; composite frames (up to 5 cells, e.g. the player's body parts) need the layout.
  `urbz_png.py` exports both as the assembled picture and cuts edits back into cells.
- `FUN_020ae0c8({gfx, layout}, slot, ...)` decodes frames into sub-screen OBJ VRAM
  (`0x06400000`).
- Because offsets are u16, a sprite gfx asset must stay under 64 KB. The largest is 51,572 B.
- `project/sprite_refs.json` lists, per gfx asset, the layouts that point into it.
  - Built by `urbz_sprites.py`: pairs scanned from the ARM9 and kept only if every layout
    offset is a real chunk start.
  - It covers **all 4,006** multi-chunk assets; the builder uses it to re-point layouts when chunks
    grow. Four sheets whose ids the game computes (07279, 09195, 09197, 10368) are paired with the
    layout right after them, whose offsets match their chunks exactly. Pairs whose frames need more
    tiles than the chunk holds are dropped (136 coincidental offset-0 pairings).
  - Verified at runtime: every non-zero-offset chunk the game decoded in the intro and city
    start (424) has a known referencer.

## Text

- **Text bank = asset 00054** (game ID 55, 340 KB, loaded at boot and kept at RAM `0x0236CFC0`).
  - u32 at +0 = offset of the string table.
  - u16 Huffman tree at +4. Node 0x100 is the root; node n's children are at entries
    2(n−0x100) and 2(n−0x100)+1 (bit 0 → first). Values < 0x100 are bytes; 0 ends a string.
  - String table: one u32 byte offset per string (8,311). Each string starts on a byte
    boundary, and bits are read least-significant first.
  - Bytes 0xF0–0xFF act as lead bytes of 2-byte codes (unused in the US text).
- Decoder: ITCM `0x01FF85C4(id, buf, maxlen)`, called from `0x020A6D40` with a 1,024-byte
  buffer. Decoded strings sit in plain ASCII just before the bank in RAM.
- Codes: `@1`–`@3` = names (`@1` = the player's name); `@hh @mm @ss @am @DD` = clock/date;
  `\xB8` = ™; `\n` = line break. The game word-wraps to the text box.
- String IDs seen so far:
  - 27 = title "Spin the record…"
  - 22 / 25 / 26 = legal lines
  - 526 = "Kris Thistle"
  - 5870 / 5871 / 5873 = Kris's first lines
- Accented letters: the fonts have Western-European letters at 0x7B-0xB8 (© œ ¡ ¿ À Á ... ñ ... ü
  º ª … ™); `urbz_text.py` maps them to/from real characters (é = 0xA3, ñ = 0xAA, ü = 0xB4).

## Fonts

- 8 fonts, each a raw asset (no chunks), loaded at boot by `FUN_02034924` → `FUN_02033a48(slot)`
  from the table at 0x020C5CEC (8 x {u32 game id, u32 colour base}). Resident descriptors at
  0x02140D60 + slot*0x28.

  | slot | asset | height | codes | colour base |
  |---|---|---|---|---|
  | 0 | 11547 | 9 | 0x20-0xB9 | 8 |
  | 1 | 11546 (with shadow) | 9 | 0x20-0xB9 | 8 |
  | 2 | 11549 | 10 | 0x20-0xB9 | 16 |
  | 3 | 11548 | 10 | 0x20-0xB9 | 16 |
  | 4 | 11550 | 16 | 0x20-0xB9 | 16 |
  | 5 | 11552 | 20 | 0x20-0xB9 | 16 |
  | 6 | 11551 | 22 | 0x20-0xB9 | 16 |
  | 7 | 11555 | 16 | 0x20-0xBA | 16 |

- File: u16 first, u16 last, u8 height; big-endian u16 table offsets at bytes 6-7, 10-11, 14-15
  (glyph offset table, width table, bitmap). Glyphs are 4bpp, **column-major**, low nibble first,
  byte-aligned. Pixel 0 = transparent, v = colour base + v. Advance = glyph width (spacing is in
  the glyph). Codes outside first..last draw glyph 1. Draw `FUN_02033bc8`, width `FUN_020336fc`,
  measure `FUN_0203458c`.
- 0xBA-0xEF are free for new glyphs (0x40 '@' and 0xF0+ are reserved); the loader derives every
  pointer from the header, so fonts can grow. `urbz_font.py` exports/imports PNG sheets
  (width = red bar under each glyph). Proven: an 'o' changed to a box showed in the Catalog menu.

## Palettes at runtime

- **BG:** a screen's 256 colours load as-is, except **row 0**, which the game fills with the
  text layer's colours (backdrop, yellow, black, white). Spare rows (unused by the map,
  filled with `0x83FF`) can take new colours. Colour 0 of every row is transparent.
- **OBJ (sprites):** palette RAM is only written through RAM shadows: table 0x02122FFC +
  engine*0x10 = {BG shadow, BG RAM, OBJ shadow, OBJ RAM}; `FUN_0201f750` copies into the shadow
  then to 0x05000200/0x05000600 (CPU copy, no DMA). Loaders: `FUN_02020100(asset, offset, index, n)`
  now, `FUN_0202005c` queued (flushed by `FUN_0201fbe0`).
- Where sprite colours come from (all proven with recolour mods):
  1. a sprite record's palette field (non-zero) → that asset, into the entity's row (+0x90 bits 12-15);
  2. **people**: table **0x020D0054** = palette game id per character id (Kris 45 → file 09160),
     put in a free row by `FUN_0206d73c`;
  3. **the player**: built from **11543** (skin tones, hair colours, shoes) and **11542** (32
     clothing colours) by `FUN_02083538` (docs/player-look.md);
  4. **street objects**: rows 2-8 at area start from table 0x020C82C4 = files 00000, 00001, 00002,
     00004, 00005, 00006, 00007 (e.g. puddles use row 3 = 00001);
  5. bottom-screen HUD: 10361 (rows 0-7), 10357/10354 (rows 14/15).
- `urbz_palette.py` edits any palette file as a swatch PNG (`list`, `who <person>`, `edit`).
  `urbz_verify.py trace` still captures the colours actually on screen (verify/palettes/).
- LZ77 chunks (1,628, all 32-192 bytes) are embedded palettes in sprite sheets. Proven: the game's
  own decoder (`decode_chunk` 0x0202D83C) decodes our re-packed, grown LZ77 chunk byte for byte.

## New game flow (scripted in the emulator)

- Create-a-Bod → clothes → rep quiz: the check mark is at touch (233,165).
- "Are you sure you want this Urb?" defaults to NO: press LEFT, then A.
- Intro cutscene: START skips it.
- In the city, **Kris Thistle** walks up straight away (tutorial). Close her dialogue with A
  (shows replies), tap the first reply (120,64), then A twice; then A three times for the
  goal pop-ups. `verify/scripts/newgame.json` does all of this and ends in a running city.
- HUD: clock, with money under it; 8 motive bars along the bottom of the top screen.
- `verify/states/world-start.dst`: in the city at 10:03 am, §0
  (vanilla ROM, clock pinned to 2026-01-05 12:00).
- Title menu: Create-an-Urb / Load-an-Urb / Settings / Minigames; DOWN moves (the first press
  after a pause is ignored, so press twice for Load-an-Urb).
- City bottom bar: globe/map (19,163 - the hit box is centred there, (18,180) misses), Urb Info
  (80,180), Options (128,180: Settings / Save Game (203,52) / Quit), Goals (193,180), pocket
  (238,180). Save Game: confirm with the check at (220,117).
- A new game starts on **area 70, "5: Roof" of King Tower** (not a street). The Squeegee job sign
  is up-left of the start; the roof elevator leads to the tower floors (docs/areas.md).

## Game code and adding our own (Phase 4)

- **arm9** is uncompressed: load 0x02000000, entry 0x02000800, 0x123678 bytes, no overlays.
  NitroSDK module params at 0x02000ADC. Autoload blocks: ITCM 0x01FF8000 (0x1A00 bytes),
  DTCM 0x027C0000 (0x1A0 bytes). Main BSS 0x02121AC0..0x0214DE20.
- Start-up (0x0200097C) copies each autoload block, clears that block's own BSS, then clears the
  main BSS. `urbz_code.py` adds one more autoload block at **0x0214DE20** (just past BSS) holding
  every code mod, and moves the heap start (the literal at **0x020B7D88** returned by
  `OS_GetInitArenaLo`) past it. Proven: a 25 KB region with two mods boots, runs and smoke-tests
  identical to the original.
- When arm9 grows past 0x127800 in the ROM, arm7 + file-name table + FAT + banner move to the end
  of the cartridge (header fields 0x30/0x40/0x48/0x68/0x80 updated), and so do any files the bigger arm9
  would now cover (their FAT entries change; 2026-10-09).
- **Code regions over 160 KB (2026-10-09):** the autoload copy at start-up runs forwards from a source inside
  the BSS (about 0x02123700) to 0x0214DE20, so past ~170 KB it would overwrite its own source. A bigger region
  is made part of the **static code** instead: the arm9 file holds zeros for the BSS (cleared at start-up as
  always) and then the region, and the cartridge loader puts it at 0x0214DE20 before anything runs (the
  ITCM/DTCM autoload bytes follow it and are copied out as before). The arm9 file grows by ~180 KB of zeros
  plus the region. All four player mods together: 306 KB, in DeSmuME and melonDS (proofs game-intact,
  melonds, furniture-melonds).
- Hooks (`hooks.txt`): `call` rewrites a BL, `jump` replaces a function, `wrap` runs our code
  before one moved ARM instruction (flags and r0-r3 preserved), `data`/`u8`/`u16`/`u32` write bytes.
  About 96% of the game is ARM code; `code/functions.json` (Ghidra export) gives each function's
  range and mode, and the builder checks the instruction it replaces.
- **Savestates contain the game code.** Test code mods from a cold boot (`urbz_verify.py city`).

## Heap
- The entity pool (people, objects) is on the heap, so it **moves up by the size of the code region** when
  code mods are built in (0x0215E344 in the original game). Tests find people by scanning
  (`tests/proofs.py` `people_in`), never at a fixed address.

- One NNS "expanded heap" ('EXPH') from the arena: handle pointer at 0x02142004, start 0x0214DE20,
  end 0x023C0000. Free list head at handle+0x24, used list at handle+0x2C; blocks have a 16-byte
  header ('FR'/'UD', size at +4, next at +0xC).
- Measured with `play --heap 30`: new game to the city: lowest free **1,403 KB**, largest free
  block 1,367 KB, most used 1,096 KB. Code mods up to 128 KB build without a warning.

## Clock

- `game_time` = **0x0214112C**: `{s16 day, s8 hour, s8 minute, s8 second, s8 tick(0-29)}`.
- `time_add` (0x0201D994) adds a delta and normalises; returns bits 2 = minute, 4 = hour, 8 = day.
- `world_tick` (0x02084104) runs 30 times a second while the city runs and calls
  `time_update` (0x02083DB0) with `time_speed_table[time_speed_index]`
  (table 0x02113B5C, index u8 at 0x021473F8). Entry 0 = +3 s per tick (a game minute = 20 ticks,
  a day = 16 real minutes); entry 1 = +60 s (sleeping fast-forward). `time_update` also fires the
  hourly/daily events (bills, shops...).
- Proven: the `clock-speed` mod (data patch of entry 0 to 1 s + 15/30) halves the clock rate.
- Dialogs and pop-ups pause the world tick (the clock stops).

## Needs (motives)

- Player needs: **s32[8] at 0x02141204**, 8.24 fixed point (0..100.0). Order: hunger, hygiene,
  energy, social, comfort, bladder, fun, room (the HUD order).
- `motive_decay` (0x0205E27C) runs every tick; each need changes by `motive_rate` (0x0205DD70):
  table 0x020CEDA8 (normal; hunger -0.0046/tick = -5.6 per game hour) or 0x020CEDC8 (about 6x
  faster, when the sim's +8 field is set), with modifiers.
  - Skipped entirely while sim flag bit 2 is set (u16 at 0x02141C30: set in the tutorial, which is
    why needs don't drop at the start of a new game).
  - Only Fun decays unless `game_phase` (u8 0x02141C25) == 3.
- Decay is per *tick*, not per game minute, so a slower clock means needs drop faster per game hour.
- Gains from objects/actions: `motive_apply_effect` (0x0205DC20) adds row N of table 0x020CEED4
  (s32[8] per tick) scaled by 0x020EA998[quality] (1.0, 1.0, 1.2, 1.4, 1.6, 2.0).
- Starting needs: table 0x020CEDE8 (55, 55, 60, 75, 65, 65, 65, 85).
- HUD bars: `FUN_02039cb4` reads each need via `motive_get` and draws value*16/100 pixels.
- Proven: patching the hunger rate to half made hunger drop exactly half as fast.

## Money

- **s32 at 0x02141124** (game_state+4); saved as 24 bits. The HUD redraws it on its own refresh.
- Object upgrades: table 0x020C25BC, [category][4 levels] x 12 bytes (s16 price first);
  `buy_upgrade` (0x02018DAC) pays the price and refunds half the old level's.

## Collectibles and the trash bug (2026-11-22, Ghidra 12.2 decompile; proofs `fix-trash`, `fix-trash-sold`)

- Five collectibles lie around the streets: 0 Pile of Trash, 1 Aluminum Cans, 2 License Plate, 3 Nuclear Fuel Rods,
  4 Three-Eyed Bog Frog (name string 210 + kind, description 215 + kind). You hold them in Pockets > Collectibles
  (`collectible_count`, u8[5] at 0x0214125C, capped at 250) and sell them on the shop screen (`shop_input`).
- **Where they come from:** area record type 16 `{u16 16, u16, s16 x, s16 y, u8 kind, u8 slot}` (`collectible_spawn`
  0x0201DF70). Each slot (0-63, one per record in the whole city) rolls once a day, with `collectible_chance[kind]`
  (60, 50, 40, 30, 20 %); bits at game_state+0xA18 (rolled today) and +0xA20 (there today) remember it, so
  entering again the same day brings back the same ones. Picking one up clears its "there today" bit. Each new day
  `collectible_day` (0x0201DF50, from `time_update`) clears both. 13 areas have type-16 records; Urbania Park (19)
  has 10: trash in slots 0-3 and 5-7, a licence plate in 4, cans in 8 and 9.
- **The bug** (seen in the emulator, original ROM, proof `fix-trash`): picking up a pile of trash
  (`collectible_pick` 0x0201DC50) adds 1 to `trash_picked` (u8 at 0x02141B48, game_state+0xA28, kept in the save by
  0x0201DE88). `collectible_spawn` makes no trash at all once it is over 100. The Artsies' goal "Clean up the
  city! Recycle 100 piles of trash" (mission 5, goal 2, sub-goal 2) counts something else: trash *sold while that
  sub-goal is shown* (`artsie_trash_sold`, u8 at 0x0214126A; at 100 `shop_input` marks it done). Trash picked up
  before the Artsies give you the challenge still uses up the city's 101 piles, so the goal can no longer be
  finished, and the trash is gone for good (it is in the save). Players remember it as the trash vanishing around
  day 30 or 100; nothing in the code looks at the day (time_update's day 30/50/75/100 checks only show
  messages). Seen: with `trash_picked` = 101 and every slot due today, Urbania Park reloads with the cans and
  the plate but none of its 7 piles.
- **The fix** (Bug Fixes mod), two ways, picked with its setting `trash` (`settings.json`, Tune window; the build
  writes it into `tune_trash`):
  - `keep-coming`: until that sub-goal is done, `trash_picked` is held at 100, so trash keeps coming;
    after it, the game behaves as the original. Seen: same test, the count goes back to 100 and all 7 piles are
    there; with the sub-goal marked done, the count stays at 101. A save that already has the bug gets its trash
    back the next time you enter a place.
  - `count-sold` (default): while the sub-goal is shown and not done, `artsie_trash_sold` is raised to `trash_picked` minus
    trash held (the game keeps no count of sales, so trash that left Pockets any other way counts too); at 100
    the mod calls `subgoal_set_done(5, 2, 2)`, as a sale would. Trash still stops after 101 piles. Seen (proof
    `fix-trash-sold`, sub-goal poked as shown): 101 picked up, none held: the original ROM leaves the count at 0;
    with the setting it is 101 and the sub-goal done. 30 held: count 71, not done, `trash_picked` untouched.
    Not seen: what the game shows when the sub-goal is done this way (no save with the real challenge).
  - Either way, the **Goals screen shows the count**: `goals_draw` (0x02036708, (m, g)) draws sub-goal s's line
    from `text_get(title string + s + 1)` (title strings u32[7][6] at 0x020C6004; the Artsies: 4089, so the trash
    line is 4092) at the BL `goals_line_text` (0x02036860); the mod's hook answers 4092 with the game's count
    added while the sub-goal is shown, not done and under 100: "Clean up the city! Recycle 100 piles of trash
    (42)" (it fits the panel; "(42/100)" was cut off at the edge). Seen (proof `fix-trash-goal`): bottom bar Goals (186,166), **Rep
    Mission** (192,110) is mission 5 (the groups; the S button is mission 6), the third goal (96,40) = Artsies.
- Not tested in play: picking up and selling real piles, and the Artsies' challenge itself (no save that far).

## Save file

- 8 KB EEPROM. 0x0000: 0x20-byte header ("URBZ0010"), u16 at 0x1E makes the u16 sum 0.
  Slots at 0x0020 and 0x1000, 0xFE0 bytes each; the u16 at slot+0xFDE makes the slot's u16 sum 0.
- A slot is a bit/nibble/byte stream (`save_write_*`, context 0x021470FC). Early in the game it
  uses **2,569 of 4,064 bytes (63%)**, so about 1.4 KB per slot is free.
- Known slot fields: +0x05 clock, +0x0B money (24-bit), +0x8E needs (u16[8], 8.8 fixed point: value × 256; proof `save-edit`).
- `urbz_save.py info/set/fix`. Proven: a save edited to §4,321 and 10% hunger loads with both.
- Saves to start from: `city.sav` (city, start), `lobby.sav` (first goal done), `apartment.sav` (renting
  the Small Brownstone, docs/areas.md). Load from power-on: `verify/scripts/loadgame.json` with `--sav file.sav`. Save from the city:
  `verify/scripts/savegame.json` (Options > Save Game > slot A > overwrite).
- **Save path (proven, Phase 5):** `save_game(slot)` 0x0207EBAC → `save_finish_slot` 0x0207EB28: clears the
  slot buffer (memset 0xFE0 at 0x0207EE68), resets the cursor, calls `save_serialize_all` (BL at
  **0x0207EB44**), then stores bytes used (cursor - buffer) at save_ctx+0x8C and the checksum. Bytes appended
  after `save_serialize_all` are therefore counted and checksummed (the mod core does this). Seen in the
  emulator: the cursor goes 0 → 2,569 on `city.sav`; then EEPROM write (0x20, 0xFE0).
- **Load path (proven):** `game_start` 0x0204BA98 (the first city entry after the title, from area_enter's
  BL at 0x0204C268) → `save_load_slot` 0x0207EE00 → `save_deserialize_all` 0x020356D8 (BL at **0x0207EE1C**).
  A new game runs `new_game_init` 0x02035C48 there instead. Readers mirror the writers (`save_read_bits/
  nibbles/bytes` 0x0207EC48/0x0207ECBC/0x0207ED78).
- **Boot:** `save_boot_init` 0x0207EE88 checks the header and reads both slots (BL at **0x0207EF00**) at
  frame 10-15, before the title screen.
- **Save memory:** 8 KB EEPROM (DeSmuME: 64 Kbit). Read/write `eeprom_read`/`eeprom_write`
  0x02015860/0x020157D4 `(offset, len, buf)`; the write locks the card itself and returns 1 on success.
  The game only touches 0x0000-0x001F and the two slots; **0x1FE0-0x1FFF is never written** (no ARM or
  Thumb caller; 0xFF in a real save). The mod core keeps its switch record there (proven: written, kept
  after power-off).
- **How big the game's data gets (static):** `save_serialize_all` writes fixed-size fields except one
  part: `0x0203D3FC` writes 24 lists with a count byte each and 3 bytes per item (placed/owned objects).
  Everything else has constant sizes. Measured: 2,569 bytes in both `city.sav` and `lobby.sav`, so 1,495 bytes
  are free early; each item in those lists costs 3 bytes. A save that runs out keeps the game's data and
  drops mod data (the Mods page says "save full").

## People (NPCs)

- **Character ids** in code are 31 + c, where c = 0..35 is the person in name order:
  name string 512 + c, bio string 471 + c (31 Bayou Boo, 45 Kris Thistle, 66 Sharona Faster).
  `talk_partner` (0x021474F8) holds the id of whoever you're chatting with (name = 481 + id).
- Relationships: 36 x 4 bytes at 0x02141154 (byte 0 = s8 relationship, -128 = never met; byte 1 =
  a timer that drops every 6 hours). Daily drift toward 0 happens in `FUN_0207acc8`.
- **Schedules:** `npc_schedule_table` (0x020E4FD8) holds a pointer per person (id - 31, 49 entries
  for ids 31..79) to `u8[24 hours][7 weekdays]` of area ids (weekday = day % 7; area 82 = out of
  town). `schedule_lookup(id, time)` (0x02065890) reads it. Kris: area 66 at night, 64 early
  morning, 65/63/70 by day, a different Saturday.
- **Relocation (the hook for NPC Life):** every 150 ticks (5 real seconds) `npc_relocate_tick`
  (0x02066110, from the world tick) asks `schedule_lookup` for every person 31..80: people whose
  schedule says the current area walk in from an entry point (`FUN_020657ac` picks it,
  `FUN_020656a4` spawns), people present whose schedule says elsewhere walk out (`FUN_020655c0`).
  The call at **0x0206665C** is where our code answers instead. Proven: `npc-visit` hooks it and
  Bayou Boo walks onto the roof at 10:40 (from a loaded save, schedules active).
- **Walking out (2026-10-13):** `npc_walk_out` sets entity +0x107 bit 1 and the target way point (+0x38/+0x3C).
  The wander state (`FUN_0200a1f0`, +0x105 = 7) then asks `path_request` for it and walks (+0x105 = 0x2D). On
  arrival (result byte +0x10B = 1): `path_cancel`, `entity_set_state(e, 1)` (gone), `npc_walked_out` (0x02065768).
  If the walk fails (result 3) off screen (+0xC bit 30 clear), they're removed the same way; **on screen, they go
  back to wandering with the bit cleared**, and the next relocation (5 s) tries again. P (`research/npc/who_where.py`,
  original game, Waters' Gym from 4:50 on day 2, you standing at the left lift): Sue Pirnova's walk out failed
  every time from 6:10 to past 8:30. The **Bug Fixes** mod (`mods/bug-fixes`) lets such people out: after 20 s
  of trying, within 64 px of the way point; after 60 s anywhere (proof `fix-stuck-leaving`: the original ROM and
  the mod, same run).
- Byte 0x02142234 = 0 at the very start of a new game makes everyone always present (schedules
  off); a loaded save has it at 1.
- `npc_present(id, area, time)` (0x02065AD0) decides if a person appears:
  1. if the byte at 0x02142234 is 0, always yes;
  2. hard-coded quest overrides (by quest flags);
  3. no, if the id is in the 10-entry visit list at 0x02142270 (they're placed in a spawn zone instead);
  4. the date partner (game_state+0xAEE) follows separate rules;
  5. otherwise the schedule table (no table = always yes).
- **Spawning:** an area's placement records (`{u16 7, u16, s16 x, s16 y, u8 id, u8 facing}`) are
  read on area load; `spawn_from_record` (0x02064ED8) asks `npc_present` and calls
  `spawn_npc(id, facing, x<<16, y<<16)` (0x0206499C). Area 70 (King Tower roof) has one record:
  Kris at (146, 255). Placement records only matter at area load; relocation (above) brings people
  in through entry points without one. Calling `spawn_npc` directly also works, but the game then
  walks the person out again at the next relocation if their schedule says elsewhere.
- Areas: 80 areas (ids 0-79), names = string 975 + id, data/graphics tables, door/elevator records,
  per-area people and heap: **docs/areas.md**.

## Entities and actions

- Entities (people, objects, the player) come from a pool of 0x148-byte structs at 0x0215E344
  (about 127), linked in an active list. Fields: +0x08 type (0 player, 7 person), +0x0A/+0x146
  character id, +0x0C flags, +0x18/+0x1C x/y (16.16), +0x4C behaviour function, +0x104 state,
  +0x105 action, +0xC7 animation.
- `entity_set_state` 0x02000E20, `entity_set_action` 0x02000E54, `entity_play_anim` 0x02000F34
  (people route to 0x020647E0, the player to 0x02037908). People can play animations through the
  same call, which Phase 6 (visible actions) will use.
- **Action effects:** while an action runs, its code calls `motive_apply_effect(needs, row, quality)`
  every tick. 70 rows at 0x020CEED4 (s32[8] per row, per tick). Examples: 12-20 eating (hunger),
  8-11 sleeping (energy), 44/62 showers (hygiene), 45-49 sitting (comfort), 59 toilet (bladder),
  64 bladder accident, 65 passing out, 39/41 Room drifting toward the area's score.
  `docs/effect_rows.md` lists every row and the code that uses it; **docs/objects.md** lists every
  object (catalog and placed), its actions, rows and how long each runs (from the code; the
  mechanism is proven in-game with the accident row).
- When a need hits 0, `motive_failure` (0x0205DEB8) starts its failure action (per-need
  animation table 0x18 bytes each).
- Proven: patching row 64's bladder gain from +2.0 to +1.0 made the accident refill bladder to
  49.6 instead of 99.6.

## People: movement and object use (Phase 6)

From the decompile (Ghidra 11, research/README.md) and emulator runs; proof `npc-use-object`.
- `npc_state_dispatch` (0x0200BE50) runs the state at entity+0x104; +0x105 is the action inside it,
  +0x108 a countdown, +0x10A the state to go back to, +0x10B a path result. Townspeople wander in
  **state 0x23** (`npc_wander_state` 0x0200A1F0): action 7 = stand (anim 0x04) for 150-300 ticks; then
  action 0x19 = walk (anim 0x0A) in a random direction for 30-90 ticks, stopping when blocked; Kris
  alone has a hard-coded random gesture (anim 0xD5, action 0x29). State 0x22 walks someone to an exit;
  state 0x1A (set by story scripts) walks between way points and uses objects. (Proven: Kris on the roof
  cycles actions 7/0x19/0x29 with anims 4/0x0A/0xD5.)
- **Paths:** `path_request(e, xy, &result, 0)` (0x0207DC08) walks e to a point (16.16) around obstacles;
  there are only 3 path slots (0x02146F1C, 0xA0 bytes each; count at 0x02146F18). Result byte: 2 walking,
  1 arrived, 3 failed. `path_cancel` (0x0207DB10) frees the slot.
- **Objects in the area:** a list from `object_list_head` (*0x0214490C): node `{next, ?, u16 object
  number @+8, ..., entity @+0x24}`. `object_class_table` (0x020EAF84), 0x24 bytes per object number:
  +4 list activities (node, u8 out[7]), +8 can start (node, person, activity), +0xC start, +0x10 tick,
  +0x1C usable by (node, person). `activity_need_table` (0x020EA9B0, 4 bytes per activity) = the need an
  activity serves. Example, Slice O' Life Pizza (51): 3 chairs (object 137, activity 56, comfort), 2 tables (158,
  none), sink (203, 68, hygiene), toilet (208, 66, bladder), 170 (18, fun; refuses townspeople), no food.
- **Using an object:** `npc_goto_object(person, object entity, activity)` (0x0200D720) finds the use spot,
  requests a path and sets state 0x26 (`npc_goto_object_state`); on arrival the object starts the activity
  (state 0x11, the activity at entity+0x100; it ends when its tick says so, then the person goes back to
  state +0x10A). Animations are the person's own: sit 0x6B, stand up 0x72, toilet 0x7B. Proven: Phoebe
  (54) and Gramma Hattie (43) walk to a café chair and sit; Phoebe uses the toilet.
- **Needs:** activities update entity+0x114, a block of 8 needs (s32, 0-100 in the top byte, +0x20 also
  written). Townspeople have **none** (null); `motive_apply_effect` skips null, but other code writes +0x20,
  and address 0 mirrors ITCM code, so give a person a block (64 bytes) before sending them. Without one a
  chair reads junk and stands them up at once.
- The game's own picker `npc_pick_object` (0x02008A18, action 0x3E) chooses by the person's low needs
  (only hygiene, energy, comfort, bladder, fun; never hunger) and only for people whose record has +0x36 set
  (`npc_record` table 0x020E5E5C, 0x38 bytes each: ids 33 39 41 52 54 58). Calling `npc_goto_object`
  directly works for anyone.
- **Who can use objects visibly:** `npc_has_anim(id, anim)` (0x020644A8) reads the person's animation list
  (`npc_anim_lists` 0x0211CDF4, 12-byte rows ending 0xC4). Everyone has 0x04 stand and 0x0A walk; the object
  set (0x20 0x21 0x69 0x6B-0x6D 0x7B: sit, stand up, toilet ...) only 33 39 41 45 52 53 54 58; gestures 0x87
  (20 people), 0x78 (19), 0xDB (16). Gramma Hattie (43, no 0x6B) sent to a chair stands on the chair in the standing pose.
  Seats refuse townspeople in their can-start check (activity 0x38 and type != player: chair 137's +8
  function 0x0202E934), though the sit animations exist; NPC Life skips that check for seats only.
- **Facing and chatting:** `entity_face(e, dir)` (0x02000F08), dir 1-8 clockwise with 8 = up on screen
  (7 = up-left, 3 = down-right: proven from Kris's walks). The wander state ignores actions it doesn't
  know (e.g. 0x30), so code can hold a person still, face them and play gestures, then hand them back
  with action 2 (proof `npc-act`).
- **The world's entities:** a linked list per context at `entity_lists` (0x02121AF0, 8 bytes each: head,
  first); context 0 = the world (people, objects); entity +0 = next. (Proven: the pizza place's list holds
  Phoebe, Gramma Hattie and 33.)
- **Animations the object activities play** (from each object class's start/tick code, constants only, plus
  Kris's sheets; tables can add more): seats 0x6B sit down / 0x6C / 0x6D seated loop, 0x72 stand up; beds
  0x21 lie down, 0x20 get up; shower (200) 0x69, towel 0xCC/0xCD; toilet 0x7B; fridge, microwave, stove,
  grill, vending 0x41 (eat a snack standing). Of the townspeople only Kris has 0x41; the 8 with the object
  set have 0x20/0x21/0x69/0x6B-0x6D/0x7B. Where they could be used (docs/objects.md "Objects placed by
  area records"): seats and invisible park benches in 10+ public areas, toilets in 5, vending machines in 3,
  grills in 3, showers in 5, beds (mostly invisible ones) in 6.
- **Start checks know animations:** the vending machine's (0x020ADB90) refuses a townsperson without 0x7F (press
  the button) and 0x41 (eat); nobody but the player has 0x7F. A person sent anyway stands still for that
  part, then eats (proof `npc-act-eat`).
- **Sitting too long (2026-10-08, proof `npc-seat-turns`, reported in Café Z-Bach):** NPC Life ended a use
  whenever the routine changed, with a 1 s rest; Sue's visit flips socialising/eating every few minutes, so
  she stood up and was sent back to the same love seat at once, again and again: she sat the whole visit.
  Now a use that still serves the new routine carries on (`still_wanted`), sits last 10-25 s (act_ticks run
  at 30 a second), and `watch_loose` stands up anyone still using an object with our needs block and no actor
  (stop byte 3 after 10 s, then back to wandering). Café Z-Bach (42) has 2 invisible love seats (156) and 2
  invisible computers (116) among its drawn booths; the Coffee Shop (34) has only invisible computers.
- **Extra seats (2026-10-13, proof `npc-new-seats`, `urbz_seats.py`):** a mod's `seats.json` adds object records
  (type 5) to a place's area file at build time (hidden generated mod `build/new-seats`). NPC Life ships three
  Invisible Park Benches (134) in Café Z-Bach: the left and back booths' red benches (facing 3) and the blue sofa by
  the rug (facing 7). With NPC Life the sofa is used often; the booths' walk-to spots are often out of the path
  search's reach. Positions and what can't be reached: docs/areas.md "Seeing a place whole".
- **Standing up again / using more of a place (2026-10-09, proofs `npc-seat-turns`, `npc-look`):** after a
  sit the game's wander left people standing between chair and table for a game hour (its random walks
  start blocked there): NPC Life now walks them back to where they were when it sent them (`came_from`).
  Seats are picked at random among the usable ones. Townspeople only have stand 0x04, walk 0x0A and (about
  20 of them) gestures 0x87/0x78/0xDB, plus the sit set for 8; so besides seats, NPC Life sends anyone to
  objects they can enjoy standing (`look_kind`: Admire 8, Gaze 25, Critique Graffiti 94, Watch TV 69,
  Drink 83, Wash Hands 68 calm; Dance 17, Boogie 18, Spin 100 lively; and by number the grill 1 and phones
  205-207): path to a spot 26/13 px off a corner of it (the other corners if that fails), face it, gesture
  now and then for 10-20 s. The game's own activity isn't started (it would wait for animations they don't
  have).
- **NPC Life test-play fixes (2026-10-12, proof `npc-testplay`):** Café Z-Bach's two invisible love seats (156)
  seat their sitters on shared spots: the game gives a sitter the first free place, so two people sent at once
  got the same one and were drawn on top of each other. NPC Life now sends one person to an object at a time,
  never two of its own to one object, learns where each object seats people (this visit) and skips it while that
  spot is taken; whoever sits down on someone else gets up again (the game then ends the shared object's use for
  both). The walk back after a use ends when they're within 8 px of the spot or stop moving (it waited out its
  15 s). Chats: the partner must be standing (one walking about kept walking off), may be up to 500 px away and
  isn't held back by the 10 s "already looked at" pause; socialising people try a chat before a seat. When the
  spot beside the partner has no path (result 3: a café table) the walker tries their left, right and far side,
  and a walk that ends within 72 px still becomes a chat (2026-10-12; counters `chat_end[4]`). Urbania
  Park has only a grill (1), 168 and two pay phones (206): free time now picks a chat, a stroll (`K_STROLL`: a
  path 80-200 px off, a look round) or a look in random order. Rest between actions 15-35 s (was 20-50).
  Toilet 10-15 s, shower 15-25 s (were up to 60 s). People still wander Urbania Park at 3 am, as the original
  timetables have them (Jonathan: leave it).
- **The player using an object (2026-10-09, research/objects/use_probe.py, face_scan.py):** every tick of
  the city (`world_tick` 0x02084104), with the player standing (state 0x10), the tile in front (facing +0x12,
  0-7, offsets at 0x02113B68) is checked against each object's bounds (node +0x7C..+0x82); a hit becomes the
  pointed-at object (*0x021473FC, the arrow over it). A then calls `FUN_02067a70`: the class +0x1C check,
  then `FUN_02067760` builds the menu: class +4 (activities: node, u8 out[7] at entity+0x132), each item's
  text from the activity table (+2), then Put in Pocket (0x2C) and Move (0x25) at home. Choosing one
  (`FUN_020675f4`) calls class +0xC start(node, player, activity), which takes a use record and walks there;
  then class +0x10 tick(node, person) runs each tick (`FUN_02069338`) until it returns 0 or 2. Seat ticks
  (e.g. `FUN_0206be3c`) add their effect row with `motive_apply_effect(needs, row, class+0x23 quality)` and
  ask `object_use_continue` (0x020691D0) whether to go on (the need full and its timer out = stand up).
  Copies (`like`) and imported furniture work the same way as their originals (proofs furniture-quality,
  furniture-actions; the more-furniture pieces checked one by one).
- **The object's question box (2026-11-22, proof `auto-interact`, `mods/auto-interact`):** the list lives on the
  **object's** entity, not the player's: `FUN_02067a70(object entity, player)` stores the player at +0x120, the node at
  +0x11C; `object_menu_build` 0x02067760 fills u8 choice[7] at object+0x132 (zero-ended), adds Put in Pocket 0x2C and
  Move 0x25 at home (not for objects 0xC7 0xE6 0xF1 0xD3 0xEE 0x82 0x74), copies their texts to `dialog_text` and
  opens the box with BL 0x0204E038 (= `set_state_here(2, 0, 0)`) at 0x02067A10, then sets the object's state 0xF. In state
  0xF action 0 becomes 0x38; at 0x38 `object_menu_result` 0x020675F4 reads `dialog_result` (0-based index into
  the list; 0x7FFFFFFF = cancelled). Writing 0 there and not opening the box starts the first choice
  as if it had been touched (the pay phone: no box, the Sim walks to it and pays). The pointed-at pointer
  0x021473FC may hold the node or the entity (+0x11C/+0x24 point at each other).
- **Furniture quality and new actions (2026-10-09):** `objects.json` `"quality"` writes the class row's byte
  +0x23 (the multiplier index; proof furniture-quality: x2.03 for 5 vs 1). `"actions"` get activity numbers
  120+ in a moved activity table (3 words point at it: 0x02008BEC, 0x02067A48, 0x02067A4C; the game has
  0-119, several called "Sit", e.g. 41 and 56). code/objects, at power-on, gives those objects' class rows
  four wrappers (activities: the copied object's plus ours; can-start and start: as the activity named by
  "as" that the object offers; tick: marks the use as ours), and replaces `object_use_continue`: during one of
  our uses it adds our effects (written as a row of the game's effect table, so `motive_apply_effect` scales
  them by quality) and asks about our need ("until") or counts down "minutes" (20 ticks a game minute).
  Everything else is the copied activity's own (walking, animation, its own effects, the clock running fast
  in bed). Proofs furniture-actions, furniture-melonds.
- **Borrowed poses (2026-10-09, proof `npc-poses`, `urbz_poses.py`):** a mod's `poses.json` (NPC Life ships
  one) makes the builder add a hidden mod `build/new-poses`: for each townsperson missing a group (sit
  0x6B-0x6D, eat 0x41, toilet 0x7B, shower 0x69, sleep 0x21/0x20, gestures 0x87/0x78/0xDB for the 8 who
  have none), the donor = whoever has it with the closest standing silhouette (feet aligned, scaled to the
  same height: overlap + width + how much skin shows; someone of the same sex first, `WOMEN`, from the bios: Gary
  had borrowed Phoebe's body in the shower). Each donor sheet is scaled about the feet to the
  person's height and recoloured (`colour_map`, 2026-10-12): palettes are ramps (one hue light to dark);
  black (max channel <= 0x20) stays black; skin -> skin by brightness (`SKIN`, hand-checked per person, since
  brown clothes look like skin to any rule; in the shower `BODY_SKIN`, all of the donor's bare skin); every
  other donor ramp goes to the person's garment ramp it lands on when the two standing figures are laid
  over each other in 3 px cells, heads left out (`overlay_counts`), minus 0.25 x the brightness difference;
  all donor colours given to one garment are then laid light-to-dark over its pixels. Eat and gestures
  (standing) do the top and bottom half apart (a white shirt and white trainers become a red shirt and black
  shoes). Lincoln (47, wheelchair) eats in his own chair: the donor's upper body, coloured from the two upper
  bodies only, over his own seated frame (`restyle_in_chair`); he is never a donor. The head is swapped: the top 17% of
  the figure (as wide as the standing head, centred on what is there) is cleared and the person's own
  standing head drawn with its top on the figure's top. Frames wider than tall (lying down) keep the donor's
  head, recoloured. New sheets (gfx + layout per view, the quick encoder) are new assets numbered after pets'
  and objects'; the donor's frame scripts are reused. Each person's list is copied (original rows, then the
  new ones, records with relocs) and `npc_anim_lists[id]` points at it; NPC Life's `has_anim` then lets them
  use seats, food, toilets, showers and beds, and chat with gestures. 35 people, 150 sets, 714 files, ~3.9 MB, 22 KB of lists (sit's three
  rows share one record array); made
  once in about a minute and kept in `build/cache/poses/<hash of the code and the game>`. People whose list
  another mod hooks (`urbz_anims.py`) are skipped, and `skip` in poses.json (a list of people, or
  {person: [groups]}: 47 Lincoln gets no sit, toilet or shower; the old docs had Lottie, 48, in the chair: she isn't;
  62 Mokey is left out: the game draws him its own way, also unmodded: entity +0xC0 bit 0x80, +0xC8 = 0x20 where
  everyone else has 0xD0-0xF8, a long red coat instead of his white lab-coat palette, and setting his animation
  doesn't change his picture, so borrowed rows never show; proof `npc-pose-gallery`).
  (The code region went past the 160 KB start-up copy with all mods on; it now goes into the static code
  then: "Code regions over 160 KB" above.)
- **Ending a use early:** the object's activity record has 2 user slots at +0x0C (12 bytes: person, ...,
  stop byte at +9). Stop byte 3 = abort: the person stands up and resumes wandering next tick (proven);
  2 had no visible effect.

## The player's animations (2026-10-13, research/objects/player_anims.py, tests/mods/player-anim)
- Every id 0x00-0xE3 played on the player and looked at: 0xCC-0xE3 draw nothing usable (a green blob), 0xC4-0xCB
  garbage; these are the "PLACEHOLDER" sign (a pose that was never made): 0x25 0x2B 0x57 0x5C 0x5D 0x6E 0x77
  0x78 0x87 0x8C 0x8D 0xA6. 0x6F/0x70 lie on a sofa (nap), 0x71 get up from it. 0x82/0x83 hold a sheet of paper
  in front of the chest, standing. **There is no seated reading animation**, so "Read a Book" (more-furniture
  430) keeps the seated loop; a real one would need new art.

## The player's walk (2026-11-19, mods/walk-style, proof `walk-style`)
- The player has five walks: **0x0A** (the normal one) and one per Rep group: **0x0E Streeties, 0x0C Nerdies,
  0x0D Richies, 0x0B Artsies** (groups 0-3, names strings 829-832; members: table 0x020F6F34, 4 x 8 ids,
  `FUN_0207CCC0`). Proven: each played while walking and photographed (build/proofs/walk-style-walks.png:
  Streetie hands in pockets, Nerdie hunched, Richie upright, Artsie arms out), on the male body; the female
  body is not checked yet.
- `walk_pick` (0x02037860) is asked for every player animation (player_play_anim's call at 0x02037930, and the
  player's set-up at 0x020095A4 asks for 10). For 10 it returns the group's walk when `sim_flags[input_state+4]`
  bit 0x20 is set and `player_walk_busy(0)` (0x020377DC) is 0; the group is `rep_groups`+0x14 (0x02141254,
  4 = none). From the decompile only: what bit 0x20 means (when the original game shows a group walk) is not known.
- Walk Style answers both calls: "Game's own" keeps the game's answer, the others give their walk whenever the
  game asked for 10 and `player_walk_busy(0)` is 0. Proven with the city save (no group walk there: the game says
  0x0A): r0 after the call = 0A 0A 0E 0C 0D 0B for the six choices.
- Info pages that fit (5 lines or fewer) get R / L in `mod_page_t.keys_down` (core, 2026-11-19); the page is drawn
  again when its text changes.
- **Hair on the group walks (OPEN, 2026-11-19).** The owner saw the hair vanish on the Nerdie walk going
  diagonally towards the camera; their save (build/Urbz DX.sav) is male, hair style 3 (look byte 2 = 2).
  Found so far: hair art per walk = `city_hair_tables`[gender][style] -> anim table -> 5 view records (male style 3,
  Nerdie 0x0C: gids 6731/6733/6735/6737/6739, 16 frames each, as many as the body's 12027-12035). In **6737
  (view 3) frames 10 and 12** export as bare tile strips (`png export`: no layout describes them) with only 2 tiles
  drawn, where the other frames have a 9x12 picture: a likely cause (the hair would blink in those 2 steps), and
  then it is the original game's art, not the mod. **Not proven:** strips also show up in hair the game draws fine
  (e.g. whole Normal-walk assets of male style 4 and female styles), so a strip alone means nothing; the 2-tile size
  is the suspicious part. Same 2 frames in male style 2 (6295). Next: load the owner's save (P._ram_run + _load(),
  poke the style after the load: mod_on_load sets it from the save), walk Nerdie with DOWN+LEFT / DOWN+RIGHT
  (`hold`), shoot every frame and zoom on the head; compare with the frame numbers above. A scan of all walks x
  styles x sexes for strip frames: 37 assets (list it again with the scan in this session's notes if needed).
  Mistake to avoid: the mod's struct moves when the core grows; find it with `find_magic` (WALK) every time
  (stale pokes made Normal and Nerdie look the same).
- **Pictures on info pages** (core, 2026-11-19, `mod_page_t.icon`): up to 4 extra buttons of the info menu
  (action 0x70 + i, label 0xF00D = empty). After `menu_redraw` made their sprites from the menu's sheet, the
  core gives each sprite a `ui_sprite_table` row: `set_palette_row(e, 8 + i)` first, then `entity_records`
  (it loads the record's palette into the sprite's current row: `FUN_02001050` -> `FUN_0202005C(palette, row
  x 16, 16)`; the other order left them black), then `entity_set_frame(e, 0)`. Their labels are drawn by the core
  in the 5th line's text tiles (0x51 + 0x28 x 4 + 20 i); label i of the game's own would share line i-1's tiles
  (stray letters seen). Proven (proof `walk-style`, build/proofs/walk-style-page.png): coloured pictures, tap
  results, nothing left on the Mods page after Back. `mod_page_t.selected` adds the goals' selection frame
  (`ui_sprite_table` row 14, gid 641, 8 frames of a shine going round; palette row 12) as one more button on
  that picture's spot; the core steps its frame every 4 ticks (seen: it changes between two shots).
- **The Rep group pictures** (the S goals, Goals > S): `ui_sprite_table` rows 373 Streeties (gid 3461),
  378 Nerdies (3440), 377 Richies (3446), 396 Artsies (3380), each with its own palette; seen by tapping each on
  the S goals screen with the 4 goals poked active (`goal_table` mission 5).

## Player body on a person (Phase 6 prototype, mod `npc-body-proto`)

- An entity draws either one sprite (people: slot 0, records set by their animation list) or, with
  **+0xC5 bit 0x40** (`entity_layered` 0x020031F4), up to 4 layered slots like the player: slot k at
  +0xCC + 12k = {+1 palette row, +2 sprite number (player: 4-7), +4 sprite memory (filled by the
  renderer; +6 = 0xFFFF until then), +8 animation table}. The "Load an Urb" screen draws saved Urbs this
  way (`FUN_02046F94`).
- `player_body_setup(e, look)` (0x0208327C) fills the slots from a look and e+0xC7; then
  `entity_set_frames(e, anim, player_facing_table[e+0x12])` (0x02003130) and
  `entity_set_script(e, scripts[anim], 0)` (0x02002B68), as the player's own update (0x020374B8) does.
  `load_look_palette(look, a, b)` (0x020833E0) builds the look's two rows into OBJ rows a and b.
- Look colours: clothing colour c = palette 11542 row c % 16, shades 1-3 (c < 16) or 4-6 (c >= 16);
  skin tone and hair colour = rows of 11543 (skin in shades 1-5, hair in 6-10).
- People get OBJ palette rows from a pool starting at 9 (the pizza place with 3 people: 9, 10, 11;
  12-15 empty). The player body needs 2 rows per person, so at most ~3 people could have it at once.
- Proven (proof `npc-body-prototype`, RAM; pictures in melonDS and DeSmuME): switched on, Kris is drawn
  with the female player body in a look of ours (brown skin, dark hair, white and denim), idles and walks
  with the player's animations; switched off, her own sprite comes back. Without the layered flag and
  sprite numbers, the parts are drawn scattered. Limits: the female body always has a skirt and no cap;
  person animations the player lacks (ids >= 196 or empty) show as standing.

## Pets (Phase 8 exploration from the code + game text; Phases 9-10 made them real pets)

- The game's pets are **Splicer Island's spliced animals**: you extract DNA from amber and splice genes at the
  Splicer Lab (minigames, strings 155/156/162), then show pets in the **Pet Show** card game (154/160).
  8 species (`pet kind` 0-7), each in 3 colours: Jackalope, Dodo, New World Dragon, Konga Gorilla, Simosaurus,
  Triceratops, Unicorn, Veloci-Rooster (the "chicken"); their "golden gift" catalog items 50-51 and 8203-8210.
  Kinds 8-12 are another animal (one shared sheet; probably the Dancing Nutria, object 236).
- **You own up to 3 pets:** 3 slots of 0x18 bytes at `pet_slots` 0x02141BA2 (game_state+0xA82): +0 kind
  (0x11 = empty), +1/+2 copied to the pet (+0x13F, +0x140 = colour variant), +3 s8 (picked as "best pet" by
  the highest value: likely affection).
- **In the world:** area record type 6 (`spawn_pet_record` 0x020729A4): param 0x0D-0x0F = your pet in slot 0-2,
  0x10 = your best pet, other values = a fixed kind. Records in Carnival (2), Planet of Apes (10), Splicer
  Island (14: 5), Splicer Island Zoo (16: 7), Tar Pit (17: 2), Splicer Lab Basement (61: 2).
- `spawn_pet` 0x0207272C (kind, x, y, slot): entity type 6, id = kind, state 0x1D (wander/play
  `FUN_0200E9C4`), 0x39 petting (`FUN_0200DAD0`: the person plays anim 0x4E/0x4F/0x50, row 26), 0x1E, 0x30,
  0x31; behaviour 0x02071EBC; walking speed per kind `pet_speed` 0x020C1AFC (16.16).
- **Art:** `pet_anim_lists` 0x0211D680: per kind a u32[anim] list of 5-facing records (same format as people);
  colours: `pet_palettes` 0x020F5F04 (kinds 0-7: 3 palette ids each), 0x020F5920 (others, one each).
- Objects 225 (Chicken), 236 (Dancing Nutria), 237 (Pet, "This is your pet, @2") have no class functions but
  the "removed" one (0x02065224). Catalog pet things: Dawg House (58), Robot Pet (127, its own code).
- **Critters: the chicken you carry home** (a different system from the Splicer pets). Entity type 9,
  7 kinds (`critter_table` 0x020C3400, 0x14 bytes: setup function, ?, ?, speed, flags): 1 white chicken,
  2 dark rooster, 4 carnivorous plant (Living Artemisia), 5 Dancing Nutria (+ dirt mound), 6 white bird;
  0 and 3 have no sprites (inferred: the fly swarm). Sprites: `critter_anims` 0x020C348C (0x28 per kind =
  5 x {records, frame script}); palettes `critter_palettes` 0x020C2FD4 (palette game id per kind);
  behaviour 0x0202A61C; `spawn_critter` 0x0202AB18 (kind, facing, ?, x, y).
  The Chicken **item** (object 225) is what you carry: placing it (allowed for object 225 alone among
  numbers >= 224, at most 6 per lot: 0x0203F698) creates the object, whose "removed" function
  (0x02065224) turns it into critter kind 1; picking the critter up (`critter_pickup` 0x0202A0DC, run by the critter's behaviour 0x0202A61C in state 0x12; action 0x0E counts down +0x108) puts object
  225 back in the inventory (critter kind 5 -> object 236 likewise). Placed chickens are saved as placed
  objects. A catalog pet (a dog) = a new critter kind with its own sprites + a new object, and those 4
  places taught about it.
- **A new pet kind (proof `pet-new-kind`, mod `mods/pets`):** the Puppy (object 386, copies the
  Chicken, sold by shop list 9) placed at home becomes a critter of the new kind 7. The three critter
  tables move into the mod (20 references), kind 7 copies the dark rooster (placeholder art), the Puppy's
  class row gets the mod's "removed" function (spawns kind 7), and a stub in the pick-up code
  (critter_pickup, state 0x12 / action 0x0E; the "kind" test at 0x0202A2D8) gives back object 386.
  Proven: placed, kind 7 exists; put in the pick-up state, it goes back to Pockets as object 386 and the
  critter is gone. Not yet: picking it up with real inputs, the Puppy's own art and sounds.
- **Critter art (2026-10-05):** `critter_anims[kind]` = 5 slots x {records, frame script (0 = the game's
  default timing)}; records = 5 directions x {gfx gid, layout gid, palette, param}; `critter_facing_map`
  0x020C22B4 maps the 8 facings to directions 0 1 2 3 4 3 2 1 (the other 3 are mirrored). **Seen**
  (watch on `critter_play_anim` 0x02029804 with a placed Chicken): slot 0 when it appears and stands,
  slot 1 when it walks; slots 2-4 belong to other behaviours (0x020298E4, 0x02029AB4, 0x02029C64) and the
  chicken reuses its walk there. Frame counts: chicken stand 6, walk 9 per direction; rooster 10/6/12;
  nutria 7/-/2+. Size around the feet: chicken x -14..22, y -15..19; rooster x -22..32, y -54..21.
- **Walk/stand switch is Chicken-only (fixed 2026-10-05, proof `pet-walk`):** when a critter starts or stops
  moving, `critter_behaviour` plays slot 1 / slot 0 only if its kind is 1 ("ldrh r0,[r4,#0xA]; cmp r0,#1" at
  `critter_walk_kind` 0x0202A73C and `critter_stop_kind` 0x0202A760). Other kinds keep whatever slot they
  have, so a Puppy copied from the rooster slid along in its stand pose. `code/pets-kit` jumps both tests to
  stubs that treat every pet as kind 1. Behaviours of the other kinds (decompile): 0x020298E4 (state 0x3E),
  0x02029AB4 (0x37, random slots 0-3 on timers), 0x02029C64 (0x33-0x58, the Living Artemisia eating).
- **Critters are not kept (proven 2026-10-06, `research/pets/persist_probe.py`):** a pet (or Chicken) let loose
  at home is gone after leaving the area and coming back, and after save + load (it isn't in Pockets either).
  code/pets-kit keeps "my pets" (12 x {u16 object, u8 kind, facing, s16 x, y, u8 area, hunger, happy,
  flags}): added when placed, dropped when picked up, positions noted every second, saved in the mod save data
  ('PET1'), and respawned with `spawn_critter` on the first tick after entering a home area or loading (the
  load happens inside the first area entry, so `mod_on_area_enter` alone is too early). Proof `pets-persist`.
  Pitfall: the core's save buffers are not word-aligned; a u32 store there lands on the wrong address.
- **Pet icons (2026-10-06, proof `pets-icons`):** `urbz_import.pet_art` renders `icon.png` (standing,
  three-quarter view); `urbz_pets` hands the folder to `urbz_objects` (`_icon`), which builds the record with
  `urbz_art.build_icon` (laid out like the Chicken's icon) and gives the pet object a new model number 633+
  in the moved icon table (the model only picks the icon: pet objects never stay placed).
- **Picking up is also kind-limited (from the code, 2026-10-05; not yet done with real inputs):** the
  chicken/rooster `critter_table` +8 handler `critter_tap` (0x0202A4F8, called when the player in state 0x10
  is at the critter) returns unless kind-1 <= 1 (`critter_tap_kind` 0x0202A570). code/pets-kit treats pets
  as kind 1 there too. How the player triggers it is still unknown: A presses and walking into a placed
  Chicken in the vanilla game didn't call it in the harness. Phase 9 opens its own menu instead (below).
- **Critter sprite memory:** a critter frame may use 32 tiles; a 40-tile frame drew as garbage (see
  docs/other-games.md). Critter speed = `critter_table` +0xC (16.16; chicken 0x22000, rooster 0x30000);
  pets.json `"speed"` scales it.
- **Saving (seen once, 2026-10-04, not a proof; since Phase 9 pets-kit keeps them, see above):** a Chicken (or Puppy) placed at home and left running,
  then saved (Options > Save Game) and loaded, was gone; the game did the same with the original Chicken,
  so free-running critters don't seem to be saved. Caveat: home was reached with the experimental
  `--goto 68`, not by walking there. To check on the Thor: does a chicken you let loose survive a save?
- **Pets as data (proofs `pets-data`, `pets-art`, 2026-10-05):** `urbz_pets.py` (builder stage) reads
  `pets.json` (name, object, `from` = chicken/rooster/nutria, price, sell, art folder), gives each pet a
  critter kind (7, 8, ...), copies the critter tables into a hidden generated mod with the new rows, and
  hands the pets' objects (copies of 225) to `urbz_objects.py`; `code/pets-kit` (precompiled) reads the
  list `{object, kind}` before the moved behaviour table ('PETM') and does the spawn and the pick-up for
  every pet. Art: `urbz_art.py template` writes the starting animal's frames (88x88, feet at (32, 64)),
  `palette.png` and `timing.json`; the builder converts them (`build_pet_art`: graphics + layout per slot
  and direction, a script, a 32-byte palette file) and numbers new art after any a mod ships itself.
  Proven: Puppy (kind 7) and Kitten (kind 8) spawn and go back to Pockets as 386 / 389; a placeholder
  Kitten drawn in orange from a new palette is shown in the game with new art files, built together
  with an `urbz_anims` mod. Code region: the copy limit (160 KB) now counts only what is copied at boot;
  the moved asset tables are zero-filled by the start-up (all of pets + furniture + assets: ~182 KB of heap).
- **Talking to a pet (Phase 9, proofs `pets-menu`, `pets-gate`):** the game's own collision route to
  `critter_tap` never fired for the player, so code/pets-kit checks on every tick: A pressed (keys-down at
  `input_state`+10, bit 1), the player idle (state 0x10), a pet within 56 px (`entity_faces` player -> pet).
  It then opens the game's question box: strings u16[5] at `dialog_text` 0x02146EB8 + screen*10 (question +
  up to 4 options; we use 292 "What do you want to do?", 725 "Pet", 3657 "Play", 3632 "Feed", 3654 "Put in
  Pocket"), flags at `dialog_flags` 0x02146ECC + screen*12 (+3 = 1, +4 = 0 for a question), `dialog_open`
  0x02078530; the answer is a u32 at `dialog_result` 0x027C00D4 + screen*0x8C (1-4 = option, 0x7FFFFFFF =
  cancel; `screen_index` 0x027C0004). The pet then walks up to you (<= 20 px, at most 3 s) and:
  Pet = the Splicer petting (player anims 0x4E/0x4F/0x50, motive row 0x1A), Play, Feed (takes one Pet Treats,
  object 396, from Pockets; none = a buzz and a sad pet), Put in Pocket (object back in Pockets, critter
  flags |= 2 so the world loop frees it). Afterwards it sits by you for about 10 s.
  Pitfall: after the box the player is left in state 0; the game's own boxes put him back in 0x10 themselves.
  Left in 0 he can't walk, and Save Game / Quit just buzz: `menu_hit_test` asks `can_save` 0x0206F8C4
  (player state 0x10 or game phase != 3, his action != 5, top screen in state 1, ...).
- **Pet actions (Phase 9):** each pet has its own table of 12 actions (stand, walk, sit, lie, sleep, sniff,
  play, eat, happy, sad, petted, scratch) rendered from the source game's animations (`urbz_import.ACTIONS`,
  `ACTION_ANIMS`; birds use calm ones to stay within 32 tiles). The list before the critter table ('PETA':
  {u16 object, u16 kind, u32 actions} x n, n) points at it; `pets_play(e, action)` does what
  `critter_play_anim` does (`entity_records` by facing + `entity_script`). Missing actions fall back to stand.
- **Turning a pet (2026-10-07, research/pets/facing_probe.py):** `entity_face` only changes the facing byte
  (+0x12). The picture is chosen when the records are set, and the left/right mirror is a separate bit
  (+0x8C bit 28) set by `entity_set_mirror(e, 4)` (0x020032D4: mirrored when the facing is above 4); the game
  calls it after every turn, and our steering didn't, so steered pets (strays, pets coming to you or going
  to bed) walked and sat the wrong way round. `pet_face` in pets-kit turns, mirrors and redraws the current
  action; `pets_play` mirrors too.
- **Which pet A talks to (2026-10-07, research/pets/pick_probe.py):** the pet you face within reach, the
  nearest of those, else a stray within 40 px (before: the first in the list, so with two side by side A at
  one could open the other). The player is stopped when the box opens and kept still while the pet comes up
  and is petted (he used to slide along the street), and the pet stops 28 px away (it was 20 and pushed him).
  Townspeople start a chat from their touch callback `person_touch` 0x02071CE0 (installed from the literal at
  0x02072914): the player in state 0x10, facing them, A pressed. pets-kit points that literal at its own
  `pets_person_touch`: when a pet you face is nearer than the person, the A is left for the pet (a stray next
  to Gramma Hattie's spot used to be out of reach while she stood there).
- **pets-kit and NPC Life share townspeople (2026-10-08):** both drive people by setting their action to 0x30
  (the wander state ignores it). The strays' adopter is never someone already at 0x30 (an NPC Life chat or
  visit), and NPC Life only takes people standing at action 7, so neither takes the other's.
- **Behaviour (Phase 9, proof `pets-life`):** pets-kit replaces the pets' behaviour (+0x4C) with
  `pets_behaviour`: wander with the game's own critter movement, and every few seconds pick an idle action
  (sniff, sit, scratch, lie, play, happy); at night (22:00-07:00) sleep or lie; hunger or happiness under 25 =
  mope (sad, sit, lie). On entering home the pets walk up to greet you.
- **Needs (Phase 9, proof `pets-needs`):** hunger -1 every 5 game minutes, happiness -1 every 7; Feed +45
  hunger, Pet +20 happiness, Play +30 happiness and -8 hunger. Saved with "my pets". Neglect only makes them
  mope.
- **Where pets are sold (Phase 9, proof `pets-shop`):** shop list 9 (the Chicken's) is the **Bayou Bazaar**
  clerk at the Sim Quarter Farmer's Market (area 13, clerk char id 15 at (967,545)); shops are state 17 with
  the list = clerk char id - 6; list headers at 0x02141280 + 8*i, list 9's slots at 0x021414F0. Today's stock
  is saved with the game. Pet Treats (object 396, $10) sell there too.
- **Beds (Phase 10, proof `pets-beds`):** objects 397-401 (Dog Basket, Cat Bed, Rabbit Hutch, Small Pet House,
  Bird Cage; imported from Apartment Pets, `like` 58 = the Dawg House: decorative, a big enough sprite) and
  pets.json `"bed"`. At night (and sometimes for a rest) a pet looks through `object_list_head` for a placed
  bed of its type no other pet has taken (placed for real: node+0x32 = 1 and a position; while you carry an
  object to place it the list also holds a preview node), walks there and plays `bedsleep` / `bedlie` at the
  bed's position + pets.json `"bed_spot"` (default [30, 4]: the middle of a one-tile bed).
- **Draw order (2026-10-07):** each frame the world loop (0x02003xxx) insertion-sorts the entities it draws by a
  place at e+0x60..0x66 (s16 x4: a point (u, v, u, v), or an object's footprint (u0, v1, u1, v0) on the floor
  grid; `FUN_020b0a10` turns a screen position into u, v). Earlier in the list = drawn over. A point inside an
  object's footprint goes over it; one overlapping it from behind goes under. Flags at e+0xC: 0x40000 = sorted
  this way, 0x80000 = flat on the floor (a puddle: everyone goes over it), 0x100000 = goes over others. The
  place is filled each frame by the entity's +0x5C callback `entity_sort_bounds` 0x02001314 (people have it,
  objects have their own); **the game's critters have none**, so their place stayed 0 and list order decided:
  a pet in its bed was drawn behind it. pets-kit gives its pets and strays that callback.
- **Shelves don't crowd out the game's goods (2026-10-07, proof `game-intact`):** a shop list stocks `n`
  random picks a day (`shop_defs` 0x020C76E8: {u8 capacity, u8 n, u8 keep, pad, u16 *items}, item 387 = pick
  at random by the sell masks; e.g. Drifter Woods (18) 3, the Bayou Bazaar (9) 4, the furniture shop (4) 3).
  When mods add objects to a list, urbz_objects raises its n (up to double, at most the capacity 8) with a
  longer items array in the generated mod, so the game's own goods still show up about as often.
  No doubles: `shop_pick` 0x0203C53C tries a rarity tier 50 times for something not yet on the shelf, then
  the tier below; when even the common tier is used up it added its last try anyway, a second copy (two
  Pet Treats, two of item 370). With bigger shelves that happened most days, so the generated mod also turns
  that branch (0x0203C5A8) into "give up": the slot stays empty. `game-intact` checks a week of shelves.
  Random numbers (`game_rand` 0x0207A118) come from a Mersenne Twister; the same save and script roll the
  same every run, so the proof pokes `rand_left` 0x02146EFC differently each day to see different stock.
  `game-intact` also checks byte for byte that the game's 386 objects, 7 critter kinds and 8,311 strings are
  untouched with every player mod on, and that a Chicken still becomes a chicken.
- **Imported icons (2026-10-07):** an icon is drawn as big as fits the box (4,5)-(28,27) of its 32x32 square
  (`urbz_import._fit_icon`); scaled by height alone, wide pieces (the Dog Basket) spilled over the shop slots.
- **Placed object art must list its cells biggest first (fixed 2026-10-06):** a sheet where a 16x16 cell came
  after an 8x8 one drew the wrong tiles for every later cell (detached legs on the hutch; a test grid showed
  it); the game's own sheets always go 32 -> 16 -> 8, so `urbz_anims.frame_data` sorts them that way now.
- **Moving house (proof `pets-move`):** when you come into a home area that isn't the one a pet was in (and not
  another room of the same home), pets-kit moves it there, next to you. `move_home` puts placed beds into the
  moving crate like everything else.
- **The Pets page (proof `pets-page`):** pets-kit is a visible mod now (`"title": "Pets"`, not switchable); the
  core shows a non-switchable mod with a page as just its page. Lines: "Puppy: in bed, 72/80" (food/fun), then
  the strays.
- **Messages:** the game's plain message box = `message_box(icon, string)` 0x0203A084: text[0] = the string,
  and a UI sprite `ui_sprite_new(0xF1, icon model, 0xF, 0)` 0x0204EBF4 stored at dialog_flags+8 (the box's
  icon); then `dialog_open`. Without that icon the box never came up (why the first messages were one-answer
  questions). The world stands still while it's open; A closes it and the game clears dialog_flags (+8 = 0:
  how pets-kit knows). pets-kit shows the pet's own icon (the object text table's first word = icon model)
  and fills @1/@2 itself (message_box would set @1 to the player's name). Not before 300 ticks after
  entering an area (under the load pop-up a box never shows). @1/@2 are filled with `dialog_arg` 0x02033B78
  (text, slot): 64-byte buffers at 0x02140EA0 +
  screen*0x100 + slot*0x40 (the rent sign's "@1 for a $@2 deposit").
- **The strays quest (proofs `strays-appear`, `strays-trust`; code/pets-kit/strays.inc):** while goal m0g5
  ("Find a Place to Live", 0x02141940 + 5*0xC) is active, the two pets marked `"stray"` (Puppy, Kitten) are
  spawned in Urbania Park (19) at (776,668) / (836,664) with their own behaviour: wander within ~110 px of
  their spot, back away while you move (until fed once), come up to you when you stand still for 2 s. A at one:
  Feed / Leave (then Pet / Feed / Take Home / Leave): a treat a day raises trust (0-2); at 2 Take Home puts
  its object in Pockets. Then the nearest townsperson (type 7, id 31-79, wandering) is sent to the other stray
  with `path_request` (action 0x30 holds them, as NPC Life does), and "X adopted the stray Kitten"; it follows
  them until they leave. Saved per stray: trust, flags (fed, gone), the day fed, the adopter. The game's own
  pick-up (critter_tap) refuses strays (`pets_tap_kind`).
- **Early shop:** Drifter Woods' stall in Urbania Park (clerk 24 at (864,253), shop list 18: gifts) sells Pet
  Treats, the Dog Basket and the Cat Bed (proof `pets-stall`). The Second Looks Thrift Emporium (73, list 10)
  is an auction ("Up for Auction this Pet Treats!"), so no.
- Adding a Splicer species would mean new rows in the per-kind tables (sprites, palettes, speed, names),
  new art (2 views per animation), and teaching the splicer and pet show about it; not looked at.

## Buyable objects (catalog)

- Two parallel tables indexed by object number, 0x14 bytes per row:
  `object_text_table` 0x020E6D48 `{u32 model, u32 description string, u32 name string,
  u32 catalog page, u32 ?}` and `object_info_table` 0x020E8B70 `{u32 price, u32 common, u32 uncommon, u32 rare, u32 ?}` (the 3 shop masks: bit = shop list).
- Catalog pages: 0 Appliances ... 5 Utilities, 7 = not sold (model 632).
- Proven: object 200 "The Savvy Shower" repriced from $230 to $99 shows in the Catalog.
- **The Catalog only shows; it can't buy.** Seen in the emulator (2026-10-04): tapping an item shows its name,
  price and text; tapping again or A does nothing. Its pages list every object whose page field (+0xC) is that
  page, in object order, 4 per row with up/down arrows. Seen: setting the Chicken's page (object 225, page 7)
  to 4 puts "Chicken - $20" with its icon at the end of the Recreation page. Page 6 (objects 181-198) has no
  button. Counts per page (model != 632): 13, 88, 14, 33, 9, 11, 18.
- **Shops sell; stock is picked daily.** The game state has 24 item lists, headers at **0x02141280** + 8*i
  `{u8 count, u8 capacity, u16 pad, ptr}`, slots of 6 bytes `{u16 object, u8, u8 variant}`, 0x184 = empty.
  Lists 0-20 are shop stocks, 21 holds up to 50 (unknown use, empty in city.sav), 22 = game_state+0x15C
  (8; gifts/rewards: `0x40`, `0x41`... added on day change), **23 = Pockets** (game_state+0x154, 8 slots).
  - `shop_defs` 0x020C76E8: 24 x `{u8 capacity, u8 n, u8 keep, pad, u16 *items}`; an item 0x183 means
    "pick one at random". `restock_all` 0x0203D370 runs `restock_shop` 0x0203CA30 for lists 0-20 on a
    new game and on each new day (`FUN_02083DB0`, flag 8).
  - Random picks (`shop_pick` 0x0203C53C): 60% common, 30% uncommon, 10% rare; candidates are objects whose
    `object_info_table` word +4 (common), +8 (uncommon) or +0xC (rare) has **bit = list number**, not
    already in stock. So a new object is sold in a shop by setting one bit.
  - From the masks: the Chicken (225) is common stock of **list 9** (with easels 173 and items 370-375;
    probably the Farmer's Market, not checked). List 3 = appliances (0, 2, 5, 9, 13...), 4 = seats,
    10 and 20 = general furniture.
  - The shop screen (`shop_ui` state at 0x02148F30, +4 = list number) shows 4 items of that list; buying
    (`shop_buy` 0x0209CDA8) checks money, adds the item to **Pockets** (`list_add` 0x0203D0CC) and takes
    the price. Not yet seen in the emulator (the shop that sets +4 is not found yet).
- **Placing from Pockets.** Seen: writing a Chicken into Pockets (count 0x02141338 = 2, slot at
  0x02141892 = `E1 00 00 00`) shows it in Pockets ("Chicken / A plump little chicken from Uncle Hayseed's
  Farm."); double-tapping it on the city roof says "You cannot place this item here!".
  `place_object_check` 0x0203F698: only at home (`is_home_area` 0x02046AF4: the 3 bytes per row of
  0x020C812C, or an unlocked one of 11 extra lots at 0x020C8110: areas 17, 24, 32, 52, 78, 25, 53, 55, 57,
  56, 46), fewer than 63 objects on the lot, objects 104-113 and 185-196 have extra rules, numbers >= 224
  are refused except 225, and at most 6 chickens (count of placed 225s > 5 refuses).
  **Proven (proof `pet-place`):** the starting home is area 68 (Skyline Penthouse; lot byte 0x02141230 = 0 picks
  row 0 of 0x020C812C). Double-tapping an item in Pockets puts the player in carry mode (state 0x16): the
  item is held, the D-pad walks, a tile in front shows red (blocked) or yellow (free); A places (input bit 1),
  L/R rotate (0x200/0x100; from the code), B cancels. Placed there, the Chicken leaves Pockets and a few
  seconds later walks around as critter kind 1.
- **Seven tables are indexed by object number** (found 2026-10-04): text and info (386 rows, objects
  0-385), and five that only cover the 253 placeable objects (0-252; 253+ are Pockets items: food, gifts,
  quest things): class 0x020EAF84 (0x24), `object_shapes` 0x020E6954 (u32, footprint data),
  `object_models` 0x020F1D28 (u32, art), `object_variants` 0x020F211C (5 x u32 per colour variant),
  `object_anims` 0x020F34E0 (0x1C; read by `FUN_02067D04` for the look to draw). They sit packed in the game's data, so they can't grow in place.
  The Catalog scans objects 0-279 only (`cmp #0x118` at 0x0201C6E8 and 0x0201C920); objects 280-385
  are all on page 7. `shop_pick` looks at 386 (literal 0x0203C6AC). 387 and 388 are markers (random
  pick, empty slot); the save keeps object numbers in 9 bits.
- **New objects (Phase 8 kit, `urbz_objects.py`):** a mod's `objects.json` adds rows (`like`: the object to
  copy; name/description text gets new string numbers 8311+; price, page, model, the shops that sell it).
  The builder then makes a hidden mod (`build/new-objects/`) with copies of all seven tables (room for the
  new rows), points the 132 references in the game's code at them, raises the Catalog loops and the shop
  count, and adds `code/objects` (precompiled), which makes the game's number checks (so far: the place
  check, which refuses 224+ except the Chicken) ask about the copied object. Text banks may now hold new
  strings after the game's 8,311 (the game reads strings by number, with no count).
  **Proven (proof `objects-new`):** objects 386 and 389 appear on the Recreation page with their new names,
  prices and texts; the moved text table is in the code region; object 390, a copy of the Country Class
  Chair (136), put in Pockets is placed at home and drawn like the original (an entity with id 390 and a
  sprite). Found on the way: `object_anims` starts at 0x020F34E0 (not +4); copied one field off, new
  objects drew nothing (386) or garbled sprites (389+). Saving and loading placed new objects works (proof
  `pets-gate2`: the basket is still there). A save holding new objects in a build without the mod that adds
  them used to load to a black screen; see "Saves and missing mods". Copied and imported furniture is used like its original ("The player using an object").
- **Saves and missing mods (2026-10-07, proof `save-without-mod`, code/core/objguard.c):** placed objects
  live in a store at `placed_store_ptr` *0x021418BC (u16 total, 160 records of 10 bytes {u16 object, ...},
  per area (82) u8 first @+0x642+4a, u8 count @+0x643+4a; read by 0x0203D5F8 after the 24 item lists).
  Right after a load the mod core takes out anything this build doesn't know (number >= the object count, or
  a new row no mod defined) from the store and the item lists and keeps it in the mod block as "OBJS"; a
  later load with the mod puts it all back. Unknown shop stock is just dropped. The original game (no mod
  core at all) still can't load such a save.
- **Object art (2026-10-04, read from the data, rendered with urbz_composite):** `object_models[obj]` points
  at 5 records `{gfx gid, layout gid, 0, 0}` (one per facing; facings 1/7 use the front view, 3/5 the back
  view, mirrored for the others). Chair 136: front 0x706/0x707 (one ~56x48 frame), back 0x708/0x709; bed
  131: 2 frames per view (made / slept in). The art has no palette of its own: it is drawn in a 16-shade
  ramp and `object_variants` (5 small numbers per object) pick the palette rows that colour it (the 5
  catalog colours). **Icons** (Pockets, Catalog, shops): `ui_sprite_table` 0x020CB134, 16-byte records
  indexed by the object's model number (`object_text_table` +0): chair (model 151) = 0x703/0x704 with its
  own palette 0x705 (~24x32). Pets: `critter_anims` rows are the same records (chicken: stand 6 frames,
  walk 9, 16x24, own palette). Plan for drawing new art: docs/dev/plan-phase8.md.
- **Own-palette objects (proven 2026-10-05, proofs `import-furniture`, `import-melonds`):** object setup
  (0x020681F0) normally colours an object with its `object_variants` ramp rows; only object 0x3D (the Personal
  Painting, variants 0xFD) gets a palette of its own from `entity_palette_table` (0x0206D724). The check at
  `own_palette_check` 0x02068258 is NOPed and the call at `own_palette_call` 0x02068264 goes to
  `objects_own_palette` (code/objects): 0x3D as before, else a lookup in the builder's `OPAL` list (`{u16 obj, u16
  0, u32 palette asset x4}`, before `NEWO`). Objects with `"art"`/`"import"` get variants 0xFD x5 and their own
  palette asset.
- **Icon table moved:** `ui_sprite_table` 0x020CB134 has 632 rows and only two literal references (0x0204E7B0,
  0x0204ED20). The objects mod copies it into the code region and appends rows; new art gets model numbers 633+.
- **The camera (measured from the art, 2026-10-05):** orthographic, turned 45 degrees, looking 30 degrees down (floor
  tiles 2:1), ~42 px per metre. Furniture art's origin is the west corner of its tile; records 0-2 show the piece
  facing up-right (yaw 135 for a model facing -Z), 3-4 facing down-left (yaw -45). Pets: dir k = yaw 180 - 45k
  (dir0 away, dir2 right, dir4 towards you). Code: `urbz_import.py` (`view`, `rasterize`).
- A mod that writes into the old tables (e.g. a price with a `u32` hook) can't be combined with a mod that
  adds objects: the builder stops and asks for an `objects.json` change instead.
- Clothes are not separate art: Create-a-Bod's "Threads" page is palette choices. The look is
  10 bytes at **0x02141144** (gender, skin, hair style, hair colour, shirt style, 4 clothing colours,
  shoes); only gender and hair style pick sprite art. Tables and save fields: **docs/player-look.md**.

## The moving crate (2026-11-20, mods/moving-crate, proof `moving-crate`)

Read from the code (Ghidra 12.2 decompile) and seen in DeSmuME on the original ROM from `apartment.sav`; names in
code/game.sym.
- **What fills it:** renting a home (`move_home`) runs `pack_home` 0x0203C774 on the old home's areas: each placed
  thing goes into **item list 21** (the crate, 50 slots; `crate_list_ptr` = game_state+0x158, header 0x02141328) until
  it is full (the rest is lost), then the area is cleared; and the flag game_state+0x111 (home_lot+1) is set.
- **Where it stands:** object 230 "Moving Crate" is an area record in each rentable home's main area (22, 23, 30, 38,
  67, 74; not the first home, 68). The record's object spawn (0x020684A4) makes it only when `crate_here`
  0x02046948 says so: the current area is the lot's main area and the flag is set. Seen: in a whole run past
  midnight the flag was read only there (on area load); the "the longer you wait, the more it costs you" text has
  no code behind it that we found.
- **Its menu** (class 230: list 0x020624D8, dispatch 0x020623DC): Open the Crate (0x0F) always, Return the Crate
  (0x10) only when list 21 is empty. Return clears the flag and deletes the crate for good.
- **Open** plays the open animation, then `set_state_other(17, 0, 21)`: the shop screen on list 21, titled
  "Moving Crate", with Pockets on the left (seen). A double-tap (or a tap on the item already chosen) on a crate
  item takes it into Pockets (`shop_buy` path: no price for lists 21/22). There is no Sell button, and the
  screen's input (`shop_input` 0x0209BA50) skips the Pockets side for lists 21 and 22 (0x0209BB08-0x0209BB14),
  although `shop_sell` 0x0209CC50 already handles them: no price check, the item goes into list 21 (for list 22
  too), and it leaves Pockets **even when list 21 is full** (the item would be lost).
- **Pockets icon:** an item's icon is its `object_text_table` model's row of `ui_sprite_table`; the crate's model is
  632 (none). A no-icon object (135 seen) in a shop-screen list left the touch screen black for good (bottom
  master brightness stuck at 0x8010): another reason the mod gives the crate an icon.
- **Placing from Pockets** (`pockets_use` 0x0203DF08): a double-tap asks 0x0203FA80 ("a use item?"), then
  `place_object_check`; 230 was refused there by the 224+ rule (traced). Allowed, the game's carry mode takes the
  crate like furniture, places it as a normal placed object of class 230 (same menu) and saves it.
- **Placed objects of the area on screen are not in the store:** after a load at home `placed_store` was empty
  (total 0) and the placed crate a node of `object_list_head` (seen); other areas' things are in the store
  (`store_remove` 0x0203C82C drops a record; the burglar 0x020A72AC uses it).
- **mods/moving-crate** (always on): action 0x10 is always offered and renamed Move the Crate (text 3624): it puts
  230 into Pockets ("Your Pockets are full!" otherwise) and deletes the crate; `crate_here` becomes "in the main
  area and the crate is not in Pockets or placed anywhere" (list 21 is first cleared of any 230, which
  `pack_home` puts there when the crate was placed in the old home); the Pockets side works on the crate screen
  (refused when the crate is full or for the crate itself); the place check lets 230 through (a stub at
  0x0203F804); the crate's icon is the Soap Box's (model 97). **Proven (proof `moving-crate`):** put in, full crate
  refuses, take out, Move, put down, its menu, save + load (one crate, at the new place), renting the Large
  Brownstone (the old crate taken out of list 21, the new home has its crate). Not checked: melonDS, a crate put
  down in another room of the home, the Townhouse and penthouse homes.

## Jobs and money

- `add_stat(kind, delta)` (0x02035350): kind 2 = money (capped at 999,999, refreshes the HUD);
  kinds 0/1 = u16 counters at 0x02141B88 / 0x02141B86.
- When a shift ends, the results code calls `add_stat(2, earned)` at **0x020516AC**, where earned
  = s16 at +4 of the job session (`*0x021420D0`). Proven by setting earned to 50: money +50.

## Game state

- One big struct of globals at **0x02141120** (324 code references): +0 0x55AA55AA marker,
  +4 money, +0xC clock, +0x14 social table (relationships at +0x34), +0xE4 needs, +0x820 goal table
  (`goal_table` 0x02141940; layout and quest overrides in docs/areas.md), +0xB05 game phase, +0xB10 per-sim flags.

## Game states (screens)

- Each screen (0 = top, 1 = bottom) runs a state; `set_state(state, mode, param)` 0x0204DF7C (through
  0x0204E028). State table **0x020CD8B4**: 43 rows of `{enter, update, exit}` (states 0-42), e.g. 1 city
  (0x0204C158 / 0x0204C014 / 0x0204BE4C), 5 title, 6 settings, 7 save game, 34 (0x22) the city's bottom
  menus. The update loop calls `table[state].update` (0x0204DCE0).

## Power-on: logos to the city (2026-11-20, mods/quick-start, proof `quick-start`)

Seen in DeSmuME (set_state traced on the original ROM with `verify/scripts/loadgame.json`), then used by the mod:
- Per-screen blocks at **0x027C0070** (x 0x8C): +0 state, +4 mode, +8 param, +0x2C the pending state | 0x80
  (+0x30 mode, +0x34 param). `cur_screen` 0x027C0004 is the screen being run. Here screen 0 holds the city
  (the top screen) and screen 1 its menus (state 0x22).
- The frame loop is in ITCM (0x01FF89F0). For each screen: when the pending state differs, it starts a fade-out
  (phase byte 0x027C006C + screen = 2); when the fade ends, phase 1; after both screens, each phase-1 screen
  gets its pending state (exit, copy, enter) and a fade-in. `fade_busy` 0x02080654 is nonzero while either
  screen fades.
- Frame 15: both screens enter **state 3** (the logos; its update `state_logos_update` 0x02045640 runs on screen
  0 only). After the logos (with A / START presses, about frame 1,300) it calls `set_state_here(0x21)` (the
  title's top) and, on screen 1, `set_state_here(5)` (the title). **State 0x21's enter (0x020AB840) calls
  `game_reset` 0x02035D5C**: it clears the game globals and allocates the placed-objects buffer
  (game_state+0x664). Loading a save before it writes through a 0 pointer and wipes low memory (a crash at a
  garbage address, seen).
- Load-an-Urb is state 7 (mode 0) on screen 1. Its confirm (`load_confirm` 0x0204781C): save_ctx+0x80 = slot,
  `save_read_slot`, `save_slot_valid`, `load_transition` (u8 0x027C0064) = 1, `set_state_other(1, 5, 0)` (the
  city, load mode) and `set_state_here(0x22)`. `game_start` then reads save_ctx+0x80. The slot-info table it
  checks (`save_slot_info` 0x02141F80) is still 0 during the logos. It also pauses the music stream around the
  read (0x02098420 / 0x02098388); the resume clears 8 KB at a pointer that is 0 before the title's music.
- The logos leave both screens' fade level at 16 (dark): `screen_level(0x3F, 0)` (0x0208093C) sets it to 0,
  as it is when Load-an-Urb confirms. A state change while a fade runs never finishes (screen 1 stayed pending).
- Quick Start does: wait until `fade_busy` is 0 in state 3's update, then `game_reset`, the confirm's steps (no
  music pause) in screen 1's turn. City from power-on at frame 69 (2.5 s) instead of about 2,040 with presses.
  Quit and the game's other ways back go to the title (state 5), never state 3. Not checked: melonDS and real
  hardware, the music after a quick start (no sound in the harness).

## Bottom-screen menus (Urb Info, Options, ...) (proven, Phase 5)

- Menus are data. `menu_table_ptrs` 0x020F5654 = 5 menu pointers (**0 = Urb Info 0x020F5744**, the city's
  default bottom menu; 1 = a 6-button sub-menu 0x020F57B0, the Catalog's pages; 2 = Options 0x020F56EC;
  3 = 0x020F5844; 4 = 0x020F56A8, not yet known). [PROVEN 2026-11-20: the array read in RAM, and `menu_ui+8`
  = 0 with Urb Info on screen; an older note here said 4 = Urb Info.] The open menu's index is `menu_ui+8` (`menu_ui` = *0x02144CEC:
  +4 open, +8 menu, +0xC action, +0x10 selected). Only 4 literal words point at the array
  (`menu_ptr_lit_1..4`), so it can be moved and lengthened.
- A menu: `{u32 gfx, u32 layout, 0, 0, u32 pal16, u32 pal256, u16 count, u16}` then `count` buttons of
  20 bytes `{u32 x | y << 8 | icon frame << 16, u32 action, u32 sub-menu, u32 param, u32 label string}`.
  Options: Settings (61,57) action 6, Save Game (198,57) action 7 (param 1), Quit (128,120) action 0x28;
  Urb Info uses the 2 x 2 grid (61,57) (198,57) (61,120) (198,120). Action 0 = open the sub-menu.
- `menu_hit_test` 0x0206FBF4 (one caller, BL at 0x0206FDAC): 32 x 32 box around each icon; returns the
  action (and plays sound 1), or opens a sub-menu and returns -1. `menu_dispatch` 0x02070494 runs actions
  (unknown ids just close the menu). Menus are touch-only (the D-pad doesn't move between buttons).
- `menu_redraw` 0x0207026C builds one icon sprite per button (8 slots at 0x02144D10), then
  `menu_draw_text` 0x02070038: it clears the text layer rectangle tiles (2,1)-(30,18) (= x 16-248,
  y 8-152), draws the title (font 3, `text_draw(1, 128, 8, ...)`) and each label centred under its icon
  (font 1, `text_draw(0x29 + 0x28 * i, x, y + 18, ...)`). Each label has 40 text tiles (about 160 px);
  longer text corrupts the screen, and 8 labels run out of tiles (6 work, as in the game's own 6-button
  menu). Text drawn outside the cleared rectangle stays on screen.
- Icon colours: palette row = icon frame, except Options, which uses row (button + 1) for buttons after
  the first (the core sets row = frame for its menus).
- Proven: the core's Options has a 4th button "Mods" that opens a Mods page (menu 5) and info pages
  (menu 6); real touches switch mods (proof `mods-page`, `npc-life-page`).
- Urb Info buttons (0x020F5744): Jobs (61,57) action 0x14, My Xizzles (198,57) 0x1D, Catalog (61,120)
  sub-menu 1, Rep Group (198,120) 0x0E; icon frames 0-3. The game stores the touched action at `menu_ui+0xC`
  and calls `menu_dispatch` (0x0206FDE0), which runs it and closes the menu for ids it doesn't know.
- **A 5th Urb Info button** (2026-11-20, `mods/cellphone`, proof `cellphone`): a mod can read the core's
  menu array through `menu_ptr_lit_1`, put a copy of Urb Info with `count` 5 at index 0 (the 5th button at
  (128,88) fits between the four, label under it), and take over the touch handler's `BL menu_dispatch`
  (`menu_dispatch_call` 0x0206FDE4) to see its own action (0x50). Not calling `menu_dispatch` (mods/cellphone never does) leaves the menu
  open (the handler returns unless the action is 0x22), as the game's refused Save Game; a `wrap` can't do
  that (the dispatch always runs after it and closes the menu). [PROVEN: proof `cellphone`, the refusal.]
  The core's own menus (Options, Mods, info) are untouched.
- **A new picture for it** (2026-11-20, `kit/urbz_menuicons.py`, mod.json `"urb_info_icon"`): Urb Info's sheet
  is gfx 10362 / layout 10363 (4 frames, 4bpp, 32 x 32) with palette 10361, whose rows 4-15 are unused filler
  (0x001F). The builder appends a 5th frame and puts its colours in row 4; a button with icon frame 4 then
  shows it in its own colours. [PROVEN: proof `cellphone`, every opaque pixel of mods/cellphone/icon.png on
  screen at (128, 88); the other four buttons look unchanged in the same screenshot.]

## Chats (social menu) and the sex check (2026-11-18)

Found in the code (capstone) and watched in DeSmuME from a real save (Sue Pirnova, 58, on a street):
- A chat shows 4 choices. Their codes are 4 bytes at **`social_options` 0x02147508** (struct 0x021474F8
  +0x10). Code c < 0x4A is string **3732 + c** ("Talk about Aliens" ... 0x40 Tease, 0x43 Flirt, 0x44 Hug,
  0x45 Kiss); 0x4A = FRIENDLY STUFF (3806, always the 4th), 0x4D move in/out (3809/3810), 0x4E What's Up?
  (3731), 0x53 The 411 (3812), 0x54 empty. Drawn by `social_draw_options` 0x0208DC1C. [PROVEN: watch
  write on 0x02147508 + screenshots]
- **`social_pick_options` 0x0208F230** draws choices 1-3 at random (`0x0207A118(max)`, max 0x45, or
  0x46 with save flag 11), no repeats. For Flirt or Kiss it reads the person's sex (**`npc_sex`
  0x02066A28** = person record +0x27, 0 male) and the player's (`player_look`+0): **same sex -> draw
  again** (0x0208F3BC `ldreq sb,[sp,#0xc]`). People 36, 40, 43, 44, 53, 69, 70 never get them. [PROVEN
  for the patch: mod `any-romance` (nop at 0x0208F3BC), player poked to female, 41 rounds with Sue:
  Flirt shown 3x, Kiss 1x. The vanilla count for the same run was not finished.]
- `add_relationship` 0x0207B074: with save flag 10 (game_state+0x100) a gain with someone of the other
  sex earns 1 more point (0x0207B0E0 `cmp r1,r0`); `any-romance` makes it count for everyone. [code only]
- `npc_sex` is also used at 0x02009880 to pick a voice sound (0x112/0x11A/0x11B). [code only]
- **Replies** (2026-11-20, mod `friendly-topics`): a plain topic's reply comes from the person's table,
  **`social_replies` 0x02114698** + (person - 31) * 0x274 (people 31-66 only; past 66 it is other data; person
  31 uses 0x02114014 when game_state+0x959 is set). One 8-byte entry per topic {u32 text, s8 relationship
  change, u8 animation}; entry = code * 8, except 0x15 -> 0xB0, 0x16 -> 0xA8 (swapped), 0x43 Flirt -> 0x230,
  0x46 -> 0x238 (`social_reply_entry` 0x0208ABF4). `social_add_rel` 0x0208FFF4 adds the change plus
  `rep_bonus` 0x0207CC3C (-2..+2 from `rep_groups` and the person's group, `rep_group_of` 0x0207CCC0): above
  0 = happy reaction, below 0 = unhappy. Changes are -8..+4; most people like 25-40 of the ~70 topics.
  [PROVEN: proof `friendly-topics` checks 12 dealt sets against this table]
- FRIENDLY STUFF (0x4A) opens a sub-menu (0x4B, 0x4C, 0x4D or empty, 0x4A); 0x4A there calls
  `social_pick_options` again (call 0x0208E064). A chat opening calls it at 0x0208E3D8. [PROVEN: watched]
- When a need is very low the player won't chat (lines 3723-3730, e.g. 3730 for Room); tests fill the
  needs first (poke `player_motives`).
- Opening a chat in tests: walk up to a person (D-pad = world axes: DOWN +y, RIGHT +x, ~1.2 px a frame),
  hold them in place by poking their entity +0x18/+0x1C every frame, face them, A.

**Player sex in a save:** `python urbz.py save set in.sav out.sav --gender male|female` (slot +0x1E bit 7).
Proven 2026-11-18: a female save switched to male loads with the male body, `player_look`+0 = 0.

**Harness on Windows (py-desmume 0.0.9):** `backup.import_file` leaves the EEPROM blank, so `.sav`
loading failed and every `--from`/`--sav` run stopped at the title (Load-an-Urb skipped). `_emu` now fixes
the save type at 8 KB on Windows and, if the import didn't take, writes the `.sav` into a frame-0 savestate's
backup chunk (`_backup_via_state`). `fcntl` falls back to `msvcrt`. Linux behaviour is unchanged (not
re-run there).

## The chat screen (2026-11-22, mod `better-chat`)

Read from the code (capstone) and watched in DeSmuME (Lottie Cash; the owner's Kris savestate shows the "!" and "?"):
- The bottom screen is 12 sprites, pointers at **`chat_sprites` 0x02142014**: +0x50 green tick (frame 0xC), +0x54 up
  arrow (0xD), +0x58 down arrow (0xB), +0x5C back (0xA), +0x60..0x6C the four topic rows (frame 9), +0x70..0x7C their
  icons (frame 0x19C + topic code; 0x4E "!" = 0x19B, 0x53 red "?" = 0x1EA; palette row 0x3B). Sprite +8 low half is the
  button id (rows 0x28-0x2B, back 0x2C, up 0x2D, down 0x2E, tick 0x2F), +0xA the frame, +0xC flags (bit 0 shown), +0x105
  palette row. `sprite_anim` 0x02001DC0 (rows: 0 normal, 1 selected, 2 pressed; an arrow showing an icon is blank in 1).
- The state machine is **`chat_update` 0x0209092C** (dialog block `dlg_ui` 0x027C0000 + `[+4]` * 0x8C: +0x74 mode, +0x78
  selected row or -1, +0x84 state: 0 topic list, 1 FRIENDLY STUFF's sub-menu, 2-6 replies and the quest/gift boxes). The
  game's tick stands still in a chat (`mod_on_tick` ran 18 times in 250 frames): use a `wrap` on `chat_update`.
- State 0: `chat_touch` 0x0208B280 hit-tests the 8 rectangles at 0x02119F54 (rows 0-3, up 4, down 5, tick 6, back 7) and a
  touch on a row selects and confirms at once; `chat_keys` 0x0208B0A0 moves the selection with UP/DOWN (0x40/0x80 in
  input_state+10, no wrap, DOWN stops at an empty row) and returns 1 for A. It lights the selected row only on frames with a
  D-pad key pressed.
- `social_pick_options` puts 0x4E ("!", when the person has a quest: table 0x020D7AD4 via 0x02065044) in row 0, and 0x53
  ("?") for people 45 (Kris) and 37 while game_state+0x81C is 0 in row 0 or 1: Kris at the start showed one normal topic.
- During a reply the game itself shows the two arrows (white, paging the message), the tick bright, back dim.
  [PROVEN in DeSmuME: proof `better-chat` (forced "!"/"?" on Lottie, since the real quest needs Kris at the start)]
- Topic icon frames (`chat_icons`): 0x19C + code, except 0x4A FRIENDLY STUFF 0x1E3, 0x4B-0x4D 0x1E4-0x1E6,
  0x4E 0x19B, 0x53 0x1EA; every icon gets colour set 0x3B (`sprite_palette`(icon, 0x3B)). `chat_icons` flips
  `screen_index` (eor 1) while it runs. `sprite_new` takes the sprite from the list of the current `screen_index`
  (0x02006264: pool 0x02122300 + screen * 0x210): made while the top screen is run, a chat sprite shows on the top
  screen. Seen: `chat_init` is entered with screen 0, the chat's frames (`chat_update`) run with 1. [PROVEN in
  DeSmuME: proofs `better-chat`, `better-chat-four` (screenshots)]
- Reply states: state 3 (`chat_touch_call3` 0x02090E5C) goes on with touch 6 or 7, states 5-7
  (`chat_touch_call5` 0x0209111C) with touch 6 or A. FRIENDLY STUFF's sub-menu is state 1; its BACK row (0x4A)
  deals a new list through `chat_icons_call2`; the back button closes the chat. [PROVEN: proofs above]

## The telephone (2026-11-19, read from the code; not yet run)

The home phone (objects 205-207) is an object activity, `phone_activity` 0x020A5168 (state in the use record
+6). Its parts:
- `phone_open` 0x020A4D94: the people you know (relationship > -101, via 0x0207AB3C over the 36 at
  0x0214114E) sorted by name into **`phone_list` 0x02148F48** (s8[36], -1 = end; count u8 0x02148F44); then the
  choice box: at home (`is_home_area(home_lot, current_area)`) Call Services (324) / Call Friend (325) / Invite a
  Friend Over (334), away only Call Friend.
- `phone_list` holds person **ids** (31-66, e.g. Kris 45; seen in RAM), so a name is string 481 + id
  (= 512 + c).
- `phone_names_page` 0x020A4B7C: 4 names (string 481 + id) from **`phone_page` 0x02148F40** (u8, first shown
  entry); choosing Call Friend or Invite sets it to 0 first (0x020A5474, 0x020A5490).
- **`phone_call` 0x020A495C (list index, use record)**: "There is no answer." (1144) when the person is out of
  town (schedule 82), a roommate (game_state+0xAEE/0xAEF) or one of 3 at 0x0211BDF0; else "for a little while /
  a few hours / quite a while" (1145-1147, from the schedule) + where (1057 + area); +1 relationship; shows the
  person's face: `ui_sprite_new(0xF1, face_image(c), 0xF, 0)`, **`face_image` 0x0204E5A4**.
- **`phone_invite` 0x020A477C (list index, use record)**: no answer as above; else yes with chance
  relationship / 2 + 10 (0 if negative): 1137 "That sounds like fun! I'll be over in a few minutes." and
  **`npc_visit_home` 0x02064768(c)** (zone 0 for 3,600 ticks); no: 1138 / 1140.
- Both set use record +6 = 0x1F and end with `dialog_open` 0x02078530.
- The name list (states 0x1C Call / 0x1D Invite, 0x020A5574 / 0x020A5624) reads `dialog_result`: 0 = next
  page (page += 4, wraps), 0x7FFFFFFF = cancel, n = `phone_call` / `phone_invite`(page + n - 1).
  `mods/phone` skips the two page = 0 stores (0x020A5478, 0x020A5494). Built, not yet run.
- Faces on the bottom screen (2026-11-19, emulator, research probe; not finished): `ui_sprite_new(0xF1, 470 + c,
  0xF, 0)` with `screen_index` = 1 from the world tick makes a sprite in the bottom list. The UI sprite's update
  (+0x4C = 0x0204E7B8) puts it back at its slot's place every frame (table 0x020CA88C, 8 bytes per slot; 0xF1 =
  19,45); with +0x4C = 0, `entity_set_pos` 0x02001DAC places it (6 shown in a grid). **But they showed old menu
  icon tiles, not faces**: the picture is not loaded into the bottom screen's sprite memory this way. Swapping
  a menu button's sprite record (0x02003254) and forcing its frame (+0x97, 0x02001DC0) changed nothing on screen.
  Next to try: create them while the bottom screen is drawn (inside `menu_redraw`, as the menu icons are).
- The phone's list now opens a touch screen like the Mixer's ("Object touch screens" below): `mods/phone`
  takes over the two calls to the 4-name box (0x020A547C Call, 0x020A5498 Invite).
- **The player's phone animations** (read from the code, Ghidra 12.2; proven for the home phone, proof
  `phone-screen`, DeSmuME, RAM only: the home phone is at the lot's top edge, out of the camera's view). The
  phone kind is the object node's +0x3B, set by `phone_activity` state 0 from the object: 205 -> 1, 206 -> 2,
  207 -> 3, 212 -> 0. `phone_anims` 0x0211BDF4 holds 3 rows x 4 kinds x {facing, object look (6 = none),
  player animation}: pick up (state 8, via 0x020685F0, sound 0x1C9) 0x52 for kind 0, **0x51** for 1-3; hang up
  (state 0x1E, sound 0x13B) 0x53 + object look 3 for kind 0, 0x04 (stand) for 1-3; end of use (state 1,
  0x02068564: it also sets the player's facing to 4) look 0 + 0x04. The game picks up, waits for the
  animation's end (entity +0xC bit 0x400000), shows the menus and the answer while holding it, waits 30 ticks
  (state 0x1F, player +0x10E) and hangs up. `object_set_look` 0x02067D04(object entity, look).
- After the faces screen (`mods/phone` 0.7, proven, proof `cellphone`, DeSmuME): the city is back ~16 frames
  after the second touch; the mod holds the player in state 0 (+0x104; the D-pad then moves nothing) until the
  phone is put down, and makes the call 12 ticks into the pick-up: the answer is up at ~70 frames (it was
  ~144 when it waited for the pick-up's end, with the player free to walk).
  The world tick (and `mod_on_tick`) does not run while the answer box is on the top screen (state 2).
- **From anywhere** (2026-11-20, `mods/cellphone` with `mods/phone` 0.5, proof `cellphone`): the touch screen
  needs no phone use. `mods/phone` builds `phone_list` itself (as `phone_open` does: `people_known`
  0x0207AB3C(`social_book` 0x0214114E, buf, 36, -100, 100 on the stack), then sorted by the first 4 bytes of
  each name), fills `return_spot` with the player and `area_entry_point` = 0xFF, and opens the same
  screens. The city comes back with the player on the same spot; the home phone's pick-up (0x51) and
  stand (0x04) play with no object. Other mods reach this through `phone_api` (magic 'PONE' and its own
  address, in mods/phone/code/main.c).
  `mods/phone` plays the same steps itself once the city is back: pick up, the call when the animation ends
  (or after 120 ticks: 0x51 never set the end bit in the test), hold during the answer, hang up 20 ticks after
  it closes; it stops if the player's animation is no longer its own (they walked off).
- **The phone's own menu from anywhere** (2026-11-20, `mods/phone` 0.6 `phone_api` version 2 `open_menu`,
  used by `mods/cellphone` 0.2; proof `cellphone`, DeSmuME): the player plays 0x51, then `phone_open` (the
  question box, top state 2); the city (and `mod_on_tick`) waits while it is up, then `dialog_result` (0-based)
  is read as `phone_activity` state 0x1A does: away from home the only choice (0) means Call Friend (1);
  0 = `phone_services_box` 0x020A4CFC (Fire / Repair Man / Maid), 1 / 2 = the faces screen. Services
  (state 0x1B): choice 0/1/2 = service 0/2/3; `phone_service` 0x02148F3C = the choice; `service_order`
  0x020A4F74(service, record) shows the answer and sets record +6 = 0x1F (or "not enough money", 4 and 0x1E);
  30 ticks later the hang-up and `service_call` 0x020647D0(service). Seen once at home: Fire, "The fireman
  is coming!", the fireman came. Money is not taken by `service_order` itself (not checked where it is).
- **No box for one choice** (2026-11-20, `mods/phone` 0.9; proven, proofs `pay-phone` and `cellphone`,
  DeSmuME): `phone_activity` state 0x19 calls `phone_open` at 0x020A5390 and sets state 0x1A, which reads
  `dialog_result` on the next tick (away from home 0 means 1). `mods/phone` takes over that call
  (`phone_choices`): at home the game's box; away it builds `phone_list` itself and sets the result to 1
  (Call Friend), so the faces open with no box (state 0x1C, or a hang-up when nobody is known). The pay
  phone (206, Urbania Park) needs money first ("You don't have enough Simoleons!" with 0). The cellphone
  away from home skips the pick-up too and opens the faces at once (~18 frames after the touch).
- **The cellphone on its own** (2026-11-20, `mods/cellphone` 0.3, `mods/phone` 0.9 `phone_api` version 3;
  proven, proofs `cellphone-alone` and `cellphone`, DeSmuME): the menu above now lives in `mods/cellphone`;
  the Phone mod only offers `open_faces(invite)`. Without it (or switched off) Call Friend / Invite use the
  game's own box of names, **`phone_names_box` 0x020A4C8C** (item 0 "More", then 4 names from `phone_page`;
  `dialog_result` 0 = the next 4, n = `phone_call` / `phone_invite`(page + n - 1, record)), as states 0x1C /
  0x1D do; a zeroed 12-byte record stands in for the use record; 30 ticks on the line, then the hang-up.

## Object touch screens (Mixer, Oven, wood carving) (2026-11-19, read from the code; Ghidra 12.2 decompile)

- Using a Mixer (215/216), a stove/oven (9-11) or a carving bench runs activity 0x24 through `0x0206A460`:
  when the use animation is done it saves where the player stands (`return_spot` 0x02141C2C: +0 the player,
  +7 facing, +8 x, +0xA y; `area_entry_point` 0x02141C28 = 0xFF), ends the use (record +6 = 1) and switches both screens:
  `set_state_here(0x25, mode, 0)` (top: state 37, the help text) and `set_state_other(0x1F, mode, 0)`
  (bottom: state 31, the touch screen). Mode: 0 Basic Mixer, 1 Deluxe Mixer, 2 Oven, 8-11 carving.
- Each screen has a block at `screen_block` 0x027C0000 + screen x 0x8C: +0x74 mode, +0x78 the part drawn
  first, +0x84 the sub-screen the update runs, +0xD4 `dialog_result`; `set_state` writes the next state
  (| 0x80) at +0x9C, mode +0xA0, param +0xA4.
- `obj_screen_modes` 0x020C2BF4, 16 bytes per mode: background graphics (two), a count, the title string
  (4151 Basic Mixer, 4152 Deluxe Mixer, 4153 Oven, 4154-4157 workbenches / Wood Carving). **Modes 3-7 are
  unused copies of mode 0**, so the game's own state 31 runs safely in them (the phone uses 7).
- State 31: enter `obj_screen_enter` 0x02028888 (backgrounds, `obj_screen_buttons` 0x0202859C: a picture,
  the round button kind 5 at (130,101) hidden and the close button kind 4 at (128,179) in slot 0x10, then
  `obj_screen_draw` 0x02027D90(0xD) the title, (0xE) the 8 ingredients in a 4 x 2 grid); update
  `obj_screen_update` 0x02028814 by sub-screen (0 -> 1; 1 = `obj_screen_touch` 0x02027290: ingredient
  touches, and the close button: `set_state_here(0x22, 0, 0)` + `set_state_other(1, 2, current_area)`, the
  city coming back without placing everyone again). State 37: enter `obj_help_enter` 0x02024D94 (title from
  the mode's row, help 163 carving / 164 mixer / 165 oven).
- Buttons: `button_new` 0x0206F6F4(kind, palette, 0, shown, x, y) makes a round button (UI sprite 0x4C,
  picture 0x267, frame kind x 3, update 0x0206F4A8) **placed by its centre**; `button_hit` 0x0206F5E8(slot)
  tests a 30 x 30 box around `obj_buttons` 0x02142014[screen x 20 + slot]. (The menu icons the face probe
  used, ui_sprite_new(0x8A..), are placed by their top-left corner instead: half a button too far right.)
- **`obj_screen_buttons` needs buttons 12 and 13** (proven 2026-11-19, the owner's melonDS game froze):
  after the mode's own sprites (only modes 0/1 mixers, 2 oven and 8-11 carving make any) it clears flags at
  +0xC of `obj_buttons`[12] and [13]. In modes 3-7 both are empty, so it reads and writes address 0x0C.
  DeSmuME lets that pass; melonDS (and the DS) takes a data abort: both screens black for good (the melonDS
  savestate showed CPSR 0x97, abort mode, return address 0x0202863C). `mods/phone` lends it two spare
  buttons for the call and empties the slots again. Proof `phone-screen` watches 0x08-0x17.
- While the touch screen is open the top screen is not the city, so the city (and every object use) waits.
- `map_fill` 0x020180FC(layer, x, y, w, h, value), in tiles (h and value on the stack): the mixer clears its
  panel with (1, 1, 12, 16, 7, 0). Called with only 4 arguments it fills garbage sizes: the screen stayed
  black for ~400 frames and came up broken (seen in the emulator).
- **Proven (proof `phone-screen`, DeSmuME, 2026-11-19):** `mods/phone` opens this screen in mode 7 for Call
  Friend and Invite (top 37 / bottom 31, both mode 7). Faces load fine here: `ui_sprite_new(0xB0 + i,
  face_image(id), 0xF, 2)` shows the person's face in square i (Kris), and `button_hit(i)` finds it once the
  sprite is kept in `obj_buttons[i]`. Moving the selection frame (button slot 8): its u16 +8 = 0xB0 + i **and** `0x02000E54`(frame, 0) (UI sprite
  state +0x105 = 0); the UI sprite update 0x0204E7B8 moves it to its slot's place only in state 0 (as the
  mixer's touches do). Without the second step it stayed on the first square (the owner saw it; proven).
  The round button (slot 0xF) shows by setting bit 0 at +0xC. After the green button: bottom 34, top 2
  (the game's answer box from `phone_call`, string 1122 for Kris at the Law Offices); the red arrow (slot
  0x10) goes back to the city with no message. Pictures: build/proofs/phone-screen-*.png. Not yet seen: melonDS.
- **The touch screen's layers (bottom engine, read from VRAM in DeSmuME):** all 16-colour 256 x 256.
  BG0 (char 2, map 29) empty; BG1 (char 2, map 31) the text; BG2 (char 0, map 30) the frames: squares, panel and
  the blender's 3 squares (columns 24-28, rows 4-15; blank tile entry 0x011B); BG3 (char 0, map 28) the purple
  right-hand panel with the blender drawn in it (plain tile entry 0x6002 from column 21; column 20 is its edge).
  Char 0 tiles in use go up to 297; BG palette rows 9-15 are not used by BG2/BG3. `map_fill`'s layer is the BG
  number. (`mods/phone` up to 0.7 drew its phone picture over the blender here; 0.8 has Pockets' look instead,
  "The Pockets screen" below.)
- Face colours: each square's sprite needs its own OBJ palette row (`ui_sprite_new(0xB0 + i, picture, i, 2)`,
  as the mixer's ingredients); with one shared row (0xF) every face took the last one's colours (seen). Each
  face's `ui_sprite_table` record names its own palette (faces 470-479 seen: all different).
- Coming back from the touch screen the player faced 4 instead of the 6 they used the phone from, so A no longer
  pointed at it; `mods/phone` puts the facing from the start of the use back for 15 ticks (seen: the phone opens
  again). A first version wrote the 0xFF to 0x02141C2C (the player pointer) instead of `area_entry_point`:
  afterwards the game lost the player (A did nothing). Fixed; keep the two apart.
- `mods/phone` remembers the last person called or invited (1 byte of mod save data) and opens on their page with
  them selected (seen in DeSmuME; not yet across a save and load). With nobody called yet (or them not listed) the first face is
  selected, where the game draws the frame anyway (before, the frame showed there but A only selected it).
- Keys on the phone screen (proven, proof `phone-screen`, DeSmuME): `input_state`+10 (keys pressed this frame)
  read in sub-screen 1. D-pad moves the selection (6 across, 2 down since 0.8; left / right and R / L go on to the other
  page), A calls, B hangs up; a second touch on the selected face calls (a held touch counts once). If the last
  person called now has a negative relationship, the game's list leaves them out; the screen opens on page 1
  with nothing selected (proven).
- **The Rep Group's relationship bar** (read from the code, Ghidra 12.2; used by `mods/phone`, seen in
  DeSmuME): the Rep Group screen's enter 0x0207C264 loads palette record 0x020F6F00 into sprite palette row
  0xD (`palette_load` 0x0206D8D8(row, record)), makes `entity_new`(0x19) with `entity_records`(e,
  0x020F6F04), row 0xD, position (117, 60), +0x90 &= ~0xC00, +0xC |= 0x200009. 0x0207B8D8 sets its frame to
  (relationship + 100) / 2 (0-100; the marker moves along the bar) and puts the number in a text sprite
  (string 0xCA "@1", `0x0204EAD8`(0x85, &text)) 0x78 px to its right. The leave 0x0207C168 hides (+0xC bit 0)
  and frees (`entity_free` 0x02003838) it. `mods/phone` shows the same bar under the name (the number with
  `text_draw`) instead of where the person is, made on state 31 enter and freed when the screen goes (proof
  `phone-screen`).
- **Beads given (2026-11-20, read from the code; owner's save agrees):** each person's 4 bytes around
  `relationships` are {`rel_flags` 0x02141153, relationship, ?, `bead_given` 0x02141156} + 4 x (id - 31).
  `rel_flags` bit 4 is "a friend": `add_relationship` sets it when the relationship climbs to 50 and **clears it
  below 50** (count at 0x02141152), so it is not the Beads. The Beads gift (0x0208C258, in the chat-result
  function 0x0208BA40) writes 1 to `bead_given`, adds the Beads (0x020B29E8 on 0x02141234) and queues the
  "@2 gave you some Beads!" notice (queue 0x02141C70, 64-byte records, flag 0x100; shown by 0x0203A4F0, string
  426). `add_relationship` at 75 or more sets bit (i + 2) of the bit table 0x021421F8 only while `bead_given`
  is 0. In the owner's save the two people at 74 and 78 have `bead_given` 1, everyone else 0. Not yet seen
  being written in the emulator (no gift made in a run).
- **Beads on the phone screen** (`mods/phone` 0.4, proof `phone-screen`): when the selected person's
  `bead_given` is set, the Collectibles screen's red Beads (`ui_sprite_table` row 20, OBJ palette row 0xC)
  show under the relationship bar (entity like the bar's, made and freed with it). Seen in DeSmuME with
  `bead_given` poked (the first five people yes, the rest no); not yet seen in melonDS.
- The Collectibles screen (Pockets > Collectibles): its X-shaped frame is background tiles (bottom BG1, columns
  1-7, rows 4-8), not a sprite; the red Beads in it are the sprite above.
- Round button kinds (`button_new`, seen): 0 left, 1 right, 2 up, 3 down arrows, 4 the red back arrow, 5 the
  green tick, 6 a green arrow. Re-entering state 31 from its own update (`set_state_here(0x1F, mode, 0)`)
  redraws the whole screen (the phone's page arrow does this).
- **Who the phone lists (original game):** `FUN_0207ab3c` takes people whose relationship is between -100 and
  100 but compares the byte unsigned, so anyone with a negative relationship is left out (Daddy Bigbucks at -19
  in the owner's game: 9 of 10 known people listed). The relationship is the s8 at 0x0214114E + 6 + 4 x (id -
  31); -128 = never met.

## The Pockets screen, and the phone with its look (2026-11-20)

- **Pockets** is bottom state 13 (0x0D; enter 0x0204036C, update 0x02040244, leave 0x020401C4), mode 0 (mode 8
  is Collectibles), opened from the city's bottom bar (the bag at (236,166), then Pockets at (84,75)); seen
  in DeSmuME: bottom state 13 while the top stays the city (state 1) and runs. (Read from the code and the
  VRAM; the screen itself is the game's, untouched.)
- Its layers (bottom engine, read from VRAM in DeSmuME, 16 colours, 256 x 256): BG0 BG control 0x1C09 (char 2,
  map 28) the text; BG1 0x1B02 (char 0, map 27) the panel, picture 0x288D (0x288E for Collectibles); BG2
  0x1D00 empty; BG3 0x1F11 (char 4, map 31) the frame, picture 0x286E, left there by the city menu (state 0x22,
  enter 0x0207121C). The text colours are palette 0x286D in BG row 0 (`0x02017990`(palette, row)).
- Loaders: `0x020185E8`(layer, BG control) sets a layer up and turns it on; `0x02018494`(0) turns the
  layers off; `0x02018410`(0x1000) turns sprites on; `0x020183EC`(layer, 0) clears it; `0x02017CF4`(layer,
  picture, palette row, 0, 0, 0) loads a picture's tiles, map and palette onto a layer;
  `0x02034884`(BG control) makes `text_draw` draw on that layer.
- The panel (map 27): 5 squares on the left (3 + 2, a gap at columns 9-12, rows 6-10) and 4 x 2 on the right;
  each square is 5 tiles wide with its edges, 4 tiles apart (edges shared); map row 10 is both the squares'
  bottom edge and the text box's top line (the line alone is the tile under the gap, columns 10-13). The text
  box is rows 10-17, the small box (money) rows 17-22, columns 10-21.
- Its sprite slots (`ui_slot_pos` 0x020CA88C, {s16 x, y, x2, y2} per slot; `ui_sprite_new` and the UI sprite
  update read x and y): 0x45-0x49 the left squares, 0x4C-0x53 the right ones ((132,36) ... (228,68)); the
  selection frame is `ui_sprite_new`(0x4C, 0xE, 0, 2). Text places (0x0203EED0 / 0x0203EED4): the title
  (128,5), the item's name (120,87), its text (18,97), the money (128,147).
- **The phone's touch screen with Pockets' look** (`mods/phone` 0.8; proven, proofs `phone-screen` and
  `cellphone`, DeSmuME): still the Mixer's state 31 in mode 7 (everything above about it holds), but in its
  enter the phone sets the bottom layers up as the city menu and then Pockets do (0x286E on BG3, the city
  menu's BG2 picture 0x2891 with BG control 0x1E12, then BG2 emptied with 0x1D00, 0x288D on BG1, 0x286D), and
  lays the squares out again 7 x 2 from map column 2 to the text box's right edge (column 30) with the right
  squares' own tiles (centres (36 + 32 i, 36) and (36 + 32 i, 68)). The proof reads the BG controls, the map
  and the BG colours while open. The faces use UI sprite slots 0xB0-0xBD (the Mixer's 8 squares and 6 more of its), whose
  `ui_slot_pos` entries are set to the squares while the screen is open and put back when it closes (the
  proof reads them both times; put back too when state 31 opens in another mode). The name, the
  relationship bar and the Beads are in the text box, the page ("1/2") in the small box, the page arrows on
  either side of the name (20,94) and (236,94), the red arrow stays at (128,179).
- **Pockets' colours come from the city menu, not from its pictures** (proven 2026-11-20: watched writes to the
  BG palette, compared all 16 rows with the real Pockets'): 0x288D and 0x286E bring no palette; 0x286D is 16
  colours in row 0; rows 1-4 come from the city menu's BG2 picture 0x2891 (`0x02071194`), which Pockets keeps.
  The Mixer's enter loads its own 256 colours (its BG3 picture 0x71D), so phone 0.8 at first drew Pockets'
  panel in the Mixer's colours: close to right in DeSmuME, grey-green in the owner's melonDS game. The phone
  now loads 0x2891 as the city menu does; the proof checks all 16 rows equal the real Pockets'.
- **14 faces a page:** each face needs its own sprite palette row (their colours differ; each uses all 15).
  The round buttons use 0xA and the bar 0xD; the Beads (6 colours, their own row was 0xC) and the selection
  frame (7 colours, row 0xF) now take the bar's row 0xD instead (owner: another colour is fine): the Beads
  are made first, so the bar's colours replace their red (they show grey), and the frame's row is set to 0xD
  after the game makes it (its own colours are nearly the bar's first 7, but the 7th is black: a darker
  outline). The 14 rows left for faces: 0-9, 0xB, 0xC, 0xE, 0xF.
- Not yet seen: melonDS; the Mixer, Oven and carving bench screens after the phone (their slots are put back:
  proven in RAM, not looked at on screen).

## CPU caches

- NitroSDK routines (r0 = address, r1 = size): `DC_FlushRange` 0x020B7C70 (clean + invalidate data cache
  lines), `DC_WaitWriteBufferEmpty` 0x020B7C8C, `IC_InvalidateRange` 0x020B7C98. The mod core calls them
  after changing game bytes at runtime. (DeSmuME doesn't model the caches, so this is from the code.)

## The mod platform (Phase 5)

The mod core (`code/core/`, built in whenever a mod can be switched in-game or uses events) owns these
game hooks and calls only the mods that are switched on: boot (0x0207EF00), world tick (0x0204C06C),
area entered (0x0204C58C), game start (0x0204C268), save (0x0207EB44), load (0x0207EE1C), menu labels
(0x0207016C), menu taps (0x0206FDAC) and menu text (0x0207039C). The mod table is at 0x0214DE20
(`code/include/mod.h`). Everything below is proven by `tests/proofs.py`:
- switches: call, wrap, jump stubs test a switch byte; data hooks are written/restored by the core
  (`toggle-call`, `toggle-data`); the switch record survives power-off (`switch-persist`);
- per-mod save data after the game's data, back after power-off; defaults on a vanilla save; a build
  without mods loads the save; a switched-off mod's data is carried through saves (`save-block`);
- the Mods page (`mods-page`) and NPC Life (`npc-life-*`).

## Open questions

Closed in the last gap pass (details in docs/player-look.md and docs/areas.md): clothing slot pixels, the
hair-style limit, street object palette rows, record types 29/30, quest overrides, and a legitimate save past the
first goal (`verify/saves/lobby.sav`). Still open:
- Which quest switches which record group (`swap_record_groups`) in each area: Phase 7 work.
- The streets need the whole tower chapter (about 12 more goals) in a real game; tests use `--goto` from
  `lobby.sav`.
- Type-30 spawn zones and the visit list are read from the code, not yet seen at runtime.
- Hardware (see "Secure area" below): not tested on a flashcart or real DS.

## Secure area and hardware

- The ROM's first 16 KB of ARM9 code (ROM 0x4000-0x7FFF, RAM 0x02000000-0x02003FFF) is the secure area. Retail
  cards store it encrypted, and header 0x6C holds a CRC of the encrypted bytes. Our ROM (like most dumps) holds
  it decrypted.
- Code mods must change the SDK module params at 0x02000ADC (the autoload table that copies our code to
  0x0214DE20), which sit inside the secure area. There's no way around this with an autoload block: any added
  code that loads at boot needs those params. We don't (and can't, without the console's key tables) recompute
  0x6C; the builder recomputes the header CRC at 0x15E.
- Risk: low. Flashcart loaders and emulators boot decrypted ROMs and don't check 0x6C; DeSmuME runs every proof.
  ROMs without code mods keep the secure area byte-identical. To do before a release: boot a code-mod build on a
  flashcart (or melonDS) once.
