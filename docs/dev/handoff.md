# Handoff (2026-10-12, Phase 10 done + pet and NPC Life fixes, borrowed poses perfected)

For the next Claude session (cloud or local). Read this, then `CLAUDE.md`, `docs/dev/plan.md`, `docs/dev/plan-phase10.md`.

## The project
Jonathan's "dream version" of *The Urbz: Sims in the City* (DS, USA, ROM SHA1
3c01cc5cf3491b5d0ab626ccfb5bcad370662f2f): a living city where townspeople have needs, jobs, money and rent,
saved with the game, plus new content. Approach (decided): **keep modding the real DS game** and replace systems
one at a time with our own C code via hooks (no full rewrite). New systems are portable C with a thin connector.
Players choose mods on the PC (manager) and switch them on/off in the game (Options > Mods).

## Where things stand
- Phases 1-6 done. The kit: extract/build (vanilla byte-identical), mods overlay, text, PNG, fonts, palettes,
  save tool, C code mods, mod platform (switches, mod save data, Options > Mods), headless DeSmuME harness,
  melonDS harness (**melonDS is the gate**: it's what Jonathan uses on his AYN Thor), emulator proofs
  (`tests/proofs.py`).
- **Phase 6 (this session)**, all proven (details and evidence: `docs/dev/plan-phase6.md`, `docs/systems.md`
  "People: movement and object use" and "Player body on a person"):
  - Placement: people are always where their original timetable puts them; out-of-town hours become visits.
    Bug fixed: the connector jumped the sim clock and skipped hour decisions.
  - The people code decompiled (Ghidra 11 headless, see research/README.md; the decompile is not in git):
    wander state 0x23, path requests (3 slots), the object list and classes, `npc_goto_object`.
  - NPC Life v2 visible actions (`mods/npc-life/code/act.inc`): sit, toilet, chat with gestures.
  - Prototype `npc-body-proto`: Kris with the player's body and animations (now a test mod, `tests/mods/`).
  - Kit: `urbz_save.py set --clock`, harness `poke` script step, `snapshots(pokes_at=)`, melonDS harness
    waits for its window and measures the menu bar (the melonds proof now matches DeSmuME 100%),
    urbz_patch tracks `.inc`/`.h` files, test mod `tests/mods/obj-probe`.

- **Phase 7 (same session), decided with Jonathan:** people keep their own look (the player-body prototype stays
  a prototype); NPC Life has **no needs, money or rent** any more: daily routines from the clock and the place,
  with per-person habits and daily variation, nothing saved (week plan from the game's week number). New
  animations come from **drawn art**: `urbz_anims.py` (template / build). Found and fixed: the game never
  loaded added assets; `urbz_build.py` now moves its asset tables (hidden generated mod `new-assets`).
  Proofs: npc-life-days/stays/visit/reload, npc-act, npc-act-eat, npc-act-release, npc-anims.

- **Phase 8 exploration (2026-10-04), pets and furniture first** (Jonathan: "we plan later once we learn how to
  work on things"). Proven: the Catalog only shows (page field), shops restock daily from per-object masks, buys
  land in Pockets, double-tap + walk + A places at home (proof `pet-place`: a Chicken becomes a walking critter);
  new objects through `objects.json` (proof `objects-new`; the builder moves seven object tables, adds new text,
  `code/objects` answers the place check as the copied object); a new pet kind (proof `pet-new-kind`,
  `mods/pets`). A copied chair (390) places and draws. Pets and furniture are separate mods
  (`mods/pets`: 386, 389-429; `mods/more-furniture`: 430-511; proof `mods-split`). Open: the pet's own art; saving placed new objects; picking up with real inputs; shops not yet seen in the emulator.

- **Pets (2026-10-05)**, Jonathan: "everything within the capabilities of the game, just new stuff; start
  with pets". Done: pets as data (`mods/pets/pets.json`: Puppy 386 from the rooster, Kitten 389 from the
  chicken; `urbz_pets.py` builder stage, `code/pets-kit`), pet art (`urbz_art.py template/preview/
  placeholder`, converted at build), shared art numbering across mods, the 160 KB boot-copy limit now
  ignores zero-filled tables. Proofs `pets-data`, `pets-art`. Facts: 5 drawn directions per slot, slot 0
  stand, 1 walk (docs/systems.md "Pets").

- **Imports (2026-10-05)**, Jonathan: "take our pick of the assets we want to move over". Readers for the other
  Sims DS games (`urbz_fullfat.py`: Apartment Pets + Castaway; `urbz_sims3.py`; `urbz_nsbmd.py`: The Sims 2 DS),
  a software renderer at the Urbz camera (`urbz_import.py`: 30 degrees down, 45 round, ~42 px/m), a gallery
  (`python urbz.py import gallery`, 1,516 models, local only), `"import"` in `objects.json` / `pets.json`.
  Furniture now has its own art, palette (`code/objects` `objects_own_palette` + OPAL list) and icon (icon
  table moved, models 633+). `mods/pets` imports the beagle and cat. Sources: a local `sources.json` with
  Jonathan's ROM paths (cloud: /root/urbz/others/*.nds). Proofs `rom-grow`, `import-render`,
  `import-furniture`, `import-pet`, `import-melonds` (they SKIP without sources). Formats:
  `docs/other-games.md`; camera, own palettes, space: `docs/systems.md`.

- **2026-10-05, Jonathan:** clock-speed, npc-visit and npc-body-proto are no longer player mods; they live in
  `tests/mods/` as test fixtures (the platform proofs use them). Player mods: npc-life, pets, more-furniture.

- **All Apartment Pets animals (2026-10-05):** the game only switched kind 1 between walk/stand (pets slid)
  and only let kinds 1-2 be picked up; code/pets-kit fixes both for every pet. mods/pets has 8 pets (386,
  389-395); caged animals walk with an idle + hop; `"coat"` for breeds; frames must fit 32 tiles (pet_art
  shrinks). Proof `pet-walk`. Picking up with real inputs is still not found.

- **Phase 9 (2026-10-06): pets completed** (plan + status: `docs/dev/plan-phase9.md`; facts: `docs/systems.md`
  "Pets"). code/pets-kit keeps "my pets" (saved, respawned at home), each pet has its own icon, 12 rendered
  actions and its own behaviour (idles, sleep at night, greet, mope), hunger and happiness; A at a pet opens the
  game's question box (Pet / Play / Feed / Put in Pocket); Pet Treats (396); pets and treats are sold by the
  Bayou Bazaar clerk at the Sim Quarter Farmer's Market (shop list 9). Saves: `verify/saves/apartment.sav`,
  `apartment-pets.sav`, `pet-shop.sav`. Proofs pets-persist, pets-icons, pets-menu, pets-life, pets-needs,
  pets-shop, pets-gate (melonDS: buy, save; home; place, Pet, Feed, save; loaded: all kept).

- **Phase 10 (2026-10-06): pets, the rest** (plan + status: `docs/dev/plan-phase10.md`; facts: `docs/systems.md`
  "Pets"). The strays quest in Urbania Park (code/pets-kit/strays.inc: two strays, trust over two days, Take
  Home, a townsperson adopts the other), pet beds and cages (objects 397-401, imported; pets sleep on them),
  pets follow you when you move house, Options > Mods > Pets page, pet things at Drifter Woods' stall
  (Urbania Park, list 18). Kit: placed-object art cells biggest first (fixes broken imported furniture),
  compressed chunks cached in build/cache/chunks (pet builds 9 min -> 25 s), mod.json `"title"`. Save
  `verify/saves/urbania.sav`. Proofs pets-beds, pets-move, pets-page, pets-stall, strays-appear, strays-trust,
  pets-gate2 (melonDS).
- **Pet bug fixes (2026-10-07, after Jonathan played):** steered pets walked backwards (the game's mirror bit,
  `entity_set_mirror` 0x020032D4, wasn't set: `pet_face`); A at one of two close pets opened the other (now the
  faced, nearest one); the player slid along while petting (kept still now); shop shelves no longer get the
  same thing twice (urbz_objects patches `shop_pick`'s give-up branch). Probes research/pets/facing_probe.py,
  pick_probe.py, pet_probe.py; `game-intact` checks a week of shelves. 62/62 proofs.

## Next
- **2026-11-18 (Windows session):** `save set --gender`; the owner's build/Urbz DX.sav switched female -> male
  (backup `build/Urbz DX.sav.bak-female`, proven in DeSmuME). New mod `mods/any-romance` (Flirt/Kiss with
  either sex; docs/systems.md "Chats"): shown working, no proof in tests/proofs.py yet, vanilla comparison
  not finished. Harness now loads `.sav` on Windows. Next asked for: a phone mod (call or invite known
  people; Rep Group screen remembers the last person).
- **2026-11-19, phone touch screen:** Ghidra 12.2 is at `C:\Tools\ghidra_12.2_PUBLIC` on the Windows PC (the
  decompile in `C:\proj\gh` is another game's). `mods/phone` v0.2: Call Friend / Invite open the Mixer's
  touch screen (state 31/37, mode 7) with faces in its 8 squares, the name and place in the panel, the green
  button calls (docs/systems.md "Object touch screens"; proof `phone-screen`). Owner chose the mixer layout
  (2026-11-19). Same day: the owner's phone picture (mods/phone/phone-ts2.png -> `mods/phone/art/make_art.py`
  -> code/phone_art.inc) replaces the blender, and an arrow pages through more than 8 people (proof covers 11).
  The owner's melonDS savestate `build/Urbz DX.ml1` knows 10 people (main RAM at file offset 0x24; the
  `.sav` next to it has no saved game). Open: melonDS check, Call Services still uses the game's box. Ask
  the owner where phone-ts2.png comes from before committing it (ts2 = The Sims 2? then it is game art).
- **2026-11-19, phone freeze in melonDS (fixed):** Call Friend froze the owner's game in melonDS (black
  screens) at Urbania Park's pay phone. Cause: `obj_screen_buttons` writes to buttons 12/13, empty in mode 7 =
  a write to 0x0C (DeSmuME ignores it, melonDS aborts); docs/systems.md "Object touch screens". Fix in
  `mods/phone` (spare buttons); proof `phone-screen` now fails on any touch of 0x08-0x17. Old ROM kept as
  `build/Urbz DX.nds.bak-phone-freeze`. Not yet confirmed by the owner in melonDS with the new ROM.
  Windows tip: a melonDS savestate (`.mlN`) holds main RAM at 0x24 and the ARM9 registers after "ARM9";
  loading one also rewrites the `.sav` with the state's copy (back the `.sav` up first).
  The owner then confirmed it works in melonDS and asked for: D-pad / A / B on the phone screen and a double
  touch to call (done, proven); a negative relationship with the last person called (proven safe). Asked
  next: a smaller name panel and a third row of faces (12 a page): not started. Then the owner asked for the
  Rep Group's relationship bar instead of the place: done (docs/systems.md "The Rep Group's relationship
  bar"; seen in DeSmuME at 5, 20, 100; proof `phone-screen` checks it is made and freed). Not yet seen in
  melonDS. Decompile on Windows: research/README + `research/ghidra/DecompFromMap.java <dir>` (arm9 section 0 + funcs.txt "addr thumb" lines in <dir>,
  functions from code/functions.json, `try` around the Thumb context change).
- **2026-11-20, Beads on the phone:** the owner first asked for the Collectibles screen's X-shaped frame on the
  faces of people who gave you their Beads (phone, then Rep Group), then changed it to: the red Beads picture
  under the relationship bar. Done in `mods/phone` 0.4 (proof `phone-screen` checks it; docs/systems.md "Beads
  given"). `bead_given` / `rel_flags` named in code/game.sym (read from the code, owner's save agrees; the gift
  itself not yet seen in a run). Old ROM kept as `build/Urbz DX.nds.bak-phone-beads`. Not yet seen in melonDS.
  Rep Group screen: not started (it is the game's own screen, enter 0x0207C264).
  Same day: the selection frame now follows (UI sprite state 0, docs/systems.md); the green button stays
  hidden and the page arrows sit on the panel (right = next, left = back, only with more than 8 people);
  calls by two touches or A. Proof `phone-screen` checks the frame's place. Not yet seen in melonDS.
- **2026-11-19, phone: first face selected + the player on the phone** (owner asked): the first face is now
  really selected when nobody was called yet; back in the city the player picks up the phone (the game's own
  animation, 0x51 home / 0x52 pay phone), holds it during the answer and hangs up (docs/systems.md "The
  telephone"). Proof `phone-screen` checks both (RAM: the home phone is out of the camera's view, so no
  picture of it yet). **Kit fix:** the mod-switch `wrap` trampoline (`_switch_wrap_trampoline`,
  kit/urbz_code.py) loaded the switch byte into r0, so a wrapped function got 1 as its first argument; it now
  uses r12 (test_code_encodings updated). Not yet seen in melonDS.
- **2026-11-19, Walk Style:** new mod `mods/walk-style` (the owner asked for it: player only, chosen in the game
  on the Mods page). Options > Mods > Walk Style, R / L: Game's own, Normal 0x0A, Streetie 0x0E, Nerdie 0x0C,
  Richie 0x0D, Artsie 0x0B (the game's Rep group walks; docs/systems.md "The player's walk"). The owner then asked
  for the S goals' group pictures as buttons: core pages now take up to 4 pictures (`mod_page_t.icon`, `tapped`),
  and pages of 5 lines or fewer get R / L in `keys_down`. Harness: `hold` / `release` script steps. Proof `walk-style`.
  Not checked: the female body, melonDS, and what sim_flags bit 0x20 means.
  Later the same day the owner asked for: (1) the 4 group pictures as buttons, with the goals' selection frame on
  the chosen one and only the "Walk: ..." line (done, proof walk-style passes); (2) **to do next: move the 4
  buttons higher on the page** (`ICON_Y` in mods/walk-style/code/main.c, now 124; labels sit 18 px below);
  (3) **the hair vanishing on the Nerdie walk, diagonal towards the camera** (owner's Sim: male, hair style 3):
  paused by the owner, findings in docs/systems.md "Hair on the group walks (OPEN)".
  The full proof run after the core changes: the first one (51/81) was spoiled by building while it ran; one was
  started again (scratchpad proofs3.log) - check its result or run `python3 tests/proofs.py` again, alone.
- **2026-11-20, Friendly Topics:** new mod `mods/friendly-topics` (the owner asked: "at least one positive talk
  topic each time"; re-dealing with FRIENDLY STUFF was a pain). Both calls to `social_pick_options` go to ours;
  if no drawn topic is liked (reply table + Rep bonus > 0), the last random one becomes a random liked plain topic
  (never Flirt/Hug/Kiss). Facts: docs/systems.md "Chats" (Replies). Proof `friendly-topics` (12 deals with Lottie
  Cash, mod stepped in 2x). Probes: research/chat/. Not checked: choosing the added topic, melonDS.
- **2026-11-20, Quick Start:** new mod `mods/quick-start` (owner asked: skip the logos and the main menu and load
  a slot, configurable). State 3's update (the logos) is ours: once the logos' fade-in ends it runs `game_reset`
  (what the title's top screen does) and Load-an-Urb's confirm steps. City at frame 69 from power-on. Choice on
  its page (R / L: Last saved, Slot 1, Slot 2, Title screen), kept in the save; `mod_on_boot` reads both slots
  from the cartridge (the newest save's choice; each save gets a rising number). Hold SELECT at power-on for the
  title. Facts: docs/systems.md "Power-on: logos to the city" (the frame loop's phases, the fade level, why
  `game_reset` and no music pause). Proof `quick-start`. Not in `mods.json` yet (the owner chooses). Not checked:
  melonDS, the music after a quick start.
- **2026-11-20, Cellphone:** new mod `mods/cellphone` (owner asked: a Cellphone button in the middle of Urb
  Info, placeholder icon, the Sim uses a phone from anywhere). Urb Info is menu **0** (docs said 4; fixed in
  docs/systems.md "Bottom-screen menus"); the mod swaps in a 5-button copy and wraps `menu_dispatch`. It
  needs `mods/phone` (0.5), which now has `phone_open_anywhere` (its own list, `return_spot` from the player)
  published as `phone_api` (magic 'PONE'); the cellphone finds the Phone row by name hash and scans near its
  code. Call Friend only. Placeholder icon = Urb Info frame 1 (the red X). Proof `cellphone`; `phone-screen`
  still passes. Not checked: melonDS; a real icon; Invite / Call Services from the cellphone.
- **2026-11-20, Cellphone 0.2 (Phone 0.6):** owner asked: the Cellphone should open the phone's options like
  using a phone object, not go straight to Call Friend. The Phone mod's `phone_api` is now version 2 with
  `open_menu`: pick up, the game's own question box (`phone_open`: Call Services / Call Friend / Invite at home,
  only Call Friend away), then Call Services' box and order, or the faces screen (docs/systems.md "The
  telephone"). Proof `cellphone` now checks the box (top state 2, player 0x51); `phone-screen` still passes.
  Seen at home (apartment save): three choices, Fire, the fireman came. Not checked: melonDS, Invite from the
  cellphone, Repair Man / Maid, where the service's price is taken.
  Same day, owner: a refused Cellphone (Phone mod off, or busy) closed Urb Info. Now the mod takes over the
  call of `menu_dispatch` (`menu_dispatch_call`) instead of wrapping it and skips it on a refusal: a buzz,
  Urb Info stays open (proof `cellphone` checks the icon in the screenshot). Not checked: the other 4 buttons
  after the change (they go through the same call to the game's `menu_dispatch`).
  Then (owner: "Urb Info still vanishes"): the successful touch also skipped closing now, so Urb Info stays on
  the bottom screen while the phone's box is up, as with a phone object (it had gone empty). Proof checks it.
  Later (owner: "after Cellphone, Urb Info's buttons vanish and any other tab crashes"): not reproduced with the
  current build/Urbz DX.nds (03:22) in DeSmuME, from the city save, the apartment save and the owner's own .sav:
  cancel, call, invite, tabs during and after the box, Jobs: all fine. All of the owner's melonDS savestates
  (.ml1-.ml8) are older than that ROM; .ml4 (02:35) holds the OLD cellphone code (menu_dispatch wrapped, which
  closed Urb Info). A savestate keeps the old code but the game then reads the new ROM's files: the likely cause.
  Told the owner to boot the ROM and load the in-game save instead. Not confirmed by the owner yet.
- **2026-11-20, Phone 0.7 (owner: the answer came late after picking someone, and the Sim could walk off in
  between, with strange results):** measured in DeSmuME: the answer came ~144 frames after the second touch
  (16 ticks of waiting, then the whole pick-up). Now: 2 ticks, and the call is made 12 ticks into the pick-up
  (answer at ~70 frames); the player is held in state 0 (as pets-kit does) from the city's return until the
  phone is put down, then given back 0x10. Proof `cellphone` checks: answer up within 100 frames with the
  D-pad held all that time, the Sim on the same spot; after hanging up, state 0x10 and the D-pad walks.
  `phone-screen` still passes. Not checked: melonDS; the pay phone (its hang-up waits for the animation's end,
  held all that time).
- **2026-11-20, Moving Crate:** new mod `mods/moving-crate` (owner asked: the crate after moving house
  undismissable, movable, and able to take things). Always on. Menu: Open the Crate / Move the Crate (text 3624
  renamed from Return); Move puts object 230 into Pockets and the game's own carry mode puts it down (place check
  stub lets 230 through); the area-record crate is made whenever it isn't in Pockets or placed; the crate screen's
  Pockets side now puts things in (the game already had the code, the input skipped list 21; full crate refused).
  Icon: the Soap Box's (model 97). Facts: docs/systems.md "The moving crate"; names in code/game.sym. Proof
  `moving-crate` (put in, full, take out, Move, place, menu, save + load, rent the Large Brownstone). Not checked:
  melonDS, the crate in a home's second room, the other homes. A no-icon object (135) in a shop-screen list
  leaves the touch screen black (seen on the original ROM; not looked into).
- **2026-11-20, Phone 0.8, Pockets' look** (owner asked: the phone's screen should look like Pockets, keeping
  everything it does; chose "keep the current screen, change its look" over opening the real Pockets screen,
  so the city still waits behind it and the top keeps the help text). Still state 31 mode 7; its enter now
  loads Pockets' panel (0x288D), the city menu's frame (0x286E) and text colours (0x286D) and lays the squares
  out 6 x 2 with Pockets' tiles: **12 faces a page** (the most the sprite palette rows allow; the owner had
  asked for 12 earlier). Name, bar and Beads in the text box, the page ("1/2") in the small box, page arrows
  in the bottom corners, the red arrow in the middle. The faces' UI sprite slots (0xB0-0xBB) are moved while
  open and put back after. The phone picture over the blender is gone (no place for it; `art/make_art.py`
  removed, the owner's local phone-ts2.png / phone_art.inc are no longer used). Facts: docs/systems.md "The
  Pockets screen, and the phone with its look"; new names in code/game.sym. Proofs `phone-screen` (now 14
  people: 12 + 2; checks the BG controls, the panel map and the slots) and `cellphone` pass. Not checked:
  melonDS; the Mixer / Oven / carving screens after using the phone (slots proven put back in RAM only).
  Same day, the owner's melonDS picture: Pockets' panel in wrong colours (grey-green). Cause: the panel and
  frame bring no colours; Pockets uses the city menu's (from its BG2 picture 0x2891), and the Mixer's enter had
  put its own there. Fixed by loading 0x2891 as the city menu does (proof now compares all 16 BG colour rows
  with the real Pockets'). The owner then asked for a 7th column and the arrows beside the name (their mock-up),
  and allowed the Beads in another colour: 7 x 2 = **14 faces a page**, the Beads and the selection frame share
  the bar's palette row (Beads grey, frame outline darker). Proofs `phone-screen` (16 people: 14 + 2) and
  `cellphone` pass. Not seen in melonDS yet.
- **2026-11-22, Better Chat:** new mod `mods/better-chat` (owner: "better conversation ux"). "!" (What's Up?) and the red "?" (The 411)
  move onto the up / down arrow buttons so there are always 3 normal topics; the arrows come back for replies; a tap selects,
  a second tap on the same one within 20 chat frames confirms (also A and the green tick); D-pad UP from the first row goes
  to the "!" / "?", LEFT / RIGHT between them (the focused one blinks); the first topic is selected after a reply (and at
  open). Hooks: the two `bl chat_icons` calls (so it runs after Friendly Topics), the state-0 touch and key calls, a `wrap`
  on `chat_update`. Facts: docs/systems.md "The chat screen". Proof `better-chat` (forced codes on Lottie). Not checked: the
  real Kris case in a run (savestate `build/Urbz DX.ds9` is the old ROM), melonDS. One tap on a topic no longer confirms.
- **2026-11-22, Better Chat 0.2** (owner: "! and ? are in the wrong screen", outline white, tap anywhere in a reply,
  option for a 4th topic): our sprites are now always made in the bottom screen's sprite list (`screen_index` 1 around
  `sprite_new`); icons use the topic icons' colour set 0x3B and rows 4-6 (the tick's icon used to share the outline's
  row 7); a new touch in a reply (states 3, 5-7) anywhere but the arrows = the green tick; option "4 topics" (Mods page,
  R / L, 1 save byte): FRIENDLY STUFF on the tick, 4 normal topics. Proofs `better-chat` and new `better-chat-four`
  pass. Not checked: the option on the Mods page and in a save, melonDS.
- **Jonathan:** set up `sources.json`, run the gallery, pick furniture (and pets); test on the Thor.
- Pet rough edges done 2026-10-07 (draw order, plain message boxes, saves without the mod, A near a townsperson: docs/dev/plan-phase10.md).
- Pets test-play fixes 2026-10-09 (Jonathan: "perfect pets and npc-life", pets first; he chose all 9 fixes, a treat a
  day): shared palettes (pets.json/objects.json "palette" groups), sleep through the night, no endless sleep, slower
  needs, Pets page scrolls (R/L, core), pets stay with you, greeting, Play gesture, only your own pets can be pocketed
  (his bug report). docs/dev/plan-phase10.md "Test-play fixes".
- NPC Life test-play 2026-10-12 (~30 game hours, all four mods; research/npc/soak_probe.py, now with CLOCK=):
  fixed two people on one seat (Café Z-Bach's two invisible love seats share a spot: one of ours per object,
  learned spots, one walk to an object at a time), frozen standing after a sit (walk-back ends when there or
  stopped), no chats in big places (partners up to 500 px, walking initiators, socialising chats before sitting),
  monotonous park (strolls; free time in random order), long toilet visits. Jonathan: leave the 3 am park
  wanderers as the original game has them. Proof npc-testplay.
- NPC Life bug pass (2026-10-08/09): nobody sits for a whole visit or stands trapped at a chair; seats picked
  at random; everyone also goes to things they can enjoy standing (proofs npc-seat-turns, npc-look);
  research/npc/soak_probe.py watches an area for stuck people.
- Borrowed poses (2026-10-09, Jonathan chose "borrow and restyle"): `urbz_poses.py` + `mods/npc-life/poses.json`
  give the 27 people without them sit/eat/toilet/shower/sleep (and the 6 without eat, Kris toilet), built at
  build time into hidden `build/new-poses` (docs/systems.md "Borrowed poses"). Proof `npc-poses` (Gramma sits at
  the pizza place, Luthor at Cafe Z-Bach); melonDS checked with research/npc/poses_melon.py. They look a bit
  off-model up close; `python urbz.py poses preview <dir> <person>` shows them.
  npc-act's chat check moved to Cafe Z-Bach (at the pizza place Hattie now sits instead of chatting).
- Borrowed poses perfected (2026-10-12, Jonathan: "all missing animations for npc-life", "perfect them all"):
  the 8 without gestures (40, 60, 61, 63-66) get borrowed ones for chats; Lottie (48) gets everything (she is
  NOT in a wheelchair; Lincoln, 47, is): Lincoln eats in his own chair (`restyle_in_chair`), borrows sleep, no
  sit/toilet/shower, never a donor. New recolouring: hand-checked SKIN tables, near-black = black, donor
  garments go to the garment they land on with the standing figures laid over each other (`overlay_counts`,
  heads left out), top and bottom halves apart for standing poses, all colours given to one garment laid
  light-to-dark together. Mokey (62) left out: the game draws him its own way even unmodded (red coat, his
  animation never changes his picture). Proof `npc-pose-gallery` (tests/mods/pose-gallery: six people at a time
  stood in a row in Urbania Park playing each pose; sheets build/proofs/npc-pose-gallery-*.png). Review sheets:
  `python urbz.py poses preview`.
  Chats: when the spot beside the other person can't be reached (café tables) the walker tries the other three
  sides, or talks from where the walk stopped if within 72 px; npc-act's chat check falls back to Urbania Park
  at 19:30 (the café's three people are rarely free together).
- 2026-10-13 (Jonathan: do all of chairs, poses up close, reading animation, test-play; Mokey dropped): extra seats
  (`seats.json`, `urbz_seats.py`, proof npc-new-seats: Café Z-Bach booth benches + sofa; research/areas/area_map.py
  stitches a whole place), borrowed poses from someone of the same sex (`WOMEN` in urbz_poses.py). Reading: the
  game has no seated reading animation (research/objects/player_anims.py looked at all 228 player ids), so "Read a
  Book" keeps the seated loop. Test-play with all four mods (café, Urbania Park evening, pizza, apartment with
  pets; ~5 game hours each): nothing stuck; soak_probe's "frozen" flags were 20 s chats and looks. Harness:
  city states are made one at a time per ROM (parallel soaks had loaded each other's saves).
- 2026-10-13, getting ready to publish (Jonathan: no outfits/hair/characters; perfect what's there, then make the
  repo easy for others, e.g. Reddit). Chosen: MIT license, same GitHub account with a clearer name
  (`The-Urbz-Sims-in-the-City-Mod-Kit`), history kept with Jonathan as author (Claude as co-author). Done: the tools moved into
  `kit/` with one entry point `urbz.py` (`python urbz.py build`, `... mod list`, ...); README rewritten for
  newcomers, `docs/playing.md`, `docs/modding.md` (the old README's guide), `docs/README.md`, `CONTRIBUTING.md`,
  `LICENSE`, `.github/` templates; dev notes in `docs/dev/`. All 88 commits re-authored (Jonathan
  <ROM-HackZ@users.noreply.github.com>). Left for Jonathan: rename the repo on GitHub, `git push --force`, make it
  public. 77/77 proofs on the new layout (pets-gate2 on a rerun: melonDS sometimes misses a tap).
- Furniture (2026-10-09, Jonathan: "address furni"; chose own strength + brand-new actions + a set picked by
  Claude): copies and imports already worked (sit, sleep). `objects.json` now has `"quality"` (1-5) and
  `"actions"` (new menu items that play like Sit/Sleep plus their own effects, until a need is full or for some
  minutes; code/objects). `mods/more-furniture` = 13 pieces from Sims 3 / Apartment Pets with qualities and
  5 actions. Proofs furniture-quality, furniture-actions, furniture-melonds. The code region with all mods
  (306 KB) is now part of the static code (no 160 KB limit; files the arm9 covers move to the end).
  Jonathan said no to the player-body prototype (it stays a test mod).
- Still open: The Sims 2 DS skinning/animations and its 16 black models; fold `urbz_anims.py` into
  `urbz_art.py`; a reading animation for "Read a Book" (it plays Sit).
- Later phases (docs/dev/plan.md): outfits, hair styles, characters.

## Setup in a fresh environment
1. Clone https://github.com/ROM-HackZ/The-Urbz-Sims-in-the-City-Mod-Kit ; the person you work with supplies their own ROM (never in git).
2. `pip install -r requirements.txt` (`--break-system-packages` if needed); `apt install clang lld llvm`.
3. `python urbz.py extract <rom> project` then `python urbz.py build --vanilla` → `[IDENTICAL to original]`.
4. `python3 tests/proofs.py` (about an hour; **never two proof runs at once**, they share `build/proofs/`),
   `python3 tests/test_code_encodings.py`, `python3 tests/test_mod_manager.py`,
   `python3 mods/npc-life/sim/run_test.py`.
5. The manager window needs tkinter (Windows Python has it; on Linux use a Python with tkinter + `xvfb-run`).

## Key facts to not relearn
- Engine names/addresses: `code/game.sym`; details + evidence: `docs/systems.md`, `docs/areas.md`.
- Savestates contain game code → test code mods from `--city` / `--from lobby` (cached per ROM hash).
- `verify/scripts/savegame.json` saves from the city; `loadgame.json` + `--sav` loads after a power-off.
- The entity pool is on the heap and moves with the code region size: scan for people (`people_in`).
- Menu labels have 40 text tiles (about 160 px) and a menu holds 6 labels safely; text outside x 16-248,
  y 8-152 isn't cleared by the game.
- `tests/mods/fast-clock` = a game minute per tick; relocation is then only every 150 game minutes.
- Switch a mod in a test: poke `CORE+4 = 0x80000000 | on << 8 | index` (find 'CORE' with `find_magic`).
- Townspeople have no needs block (entity+0x114 = 0): give them one before any object use (address 0
  mirrors game code). `npc_goto_object` ignores a failed path request: check `path_count` < 3 first.
- Objects: change or add them only through a mod's `objects.json` (`urbz_objects.py`); once any mod adds
  objects, the object tables move and the builder refuses hooks into the old tables. Tables indexed by
  object number: 7 (docs/systems.md); `object_anims` starts at 0x020F34E0 (a 4-byte mistake there made
  new objects invisible or garbled). New numbers: 386 and 389-511 (387/388 are markers).
- Critters (chickens, the Puppy) are entities type 9; their update is 0x0202A61C, pick-up = state 0x12.
- Only 8 people have object animations (33 39 41 45 52 53 54 58); seats refuse townspeople in their own
  check. Person 43 is Gramma Hattie (names: string 512 + id - 31).
- To see Kris in melonDS: `urbz_save.py set verify/saves/city.sav x.sav --clock 16:30` (King Tower roof, 16-19h).
- Anyone's day from the routines: `python mods/npc-life/sim/run_test.py day <id> [week] [weekday]`.
- The game's start checks know animations (vending needs 0x7F + 0x41); NPC Life skips them for seats and snacks.
- Added assets need the moved asset tables (urbz_build does it); keep one mod adding assets (numbers are
  baked into urbz_anims' generated data).

## Rules (from CLAUDE.md)
- Never commit ROMs, `project/`, `build/`, `*.dst`, or anything extracted from the game.
- Never edit `project/`; every change is a mod.
- Prove claims in the emulator and add a proof; mark docs facts proven only with evidence.
- Commits end with the Co-Authored-By / Claude-Session lines.

## Working with Jonathan
- Plain language, short sentences, say what's proven vs not. He uses "plan mode" by asking in a message.
- Don't spawn heavy subagents casually.
- Open items for him: try NPC Life v3 on the Thor; draw animations; `manager.bat` on Windows. He chose not to
  roll out the player body.
