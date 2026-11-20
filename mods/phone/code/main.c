/* Phone: Call Friend and Invite a Friend Over open a touch screen that looks like Pockets, with the faces of
   the people you know in its 12 squares (docs/systems.md "Object touch screens" and "The telephone").

   The game's object touch screen is state 31 on the bottom screen and state 37 (the help text) on the top;
   its mode picks the object (0/1 mixers, 2 oven; 3-7 are unused copies of the mixer's row). The phone uses
   mode 7, so the game runs the mixer's screen and its close button. This code swaps its backgrounds for
   Pockets' (the panel, the frame and the text colours) and lays the squares out 6 x 2 with Pockets' tiles,
   then adds, the way the mixer does for ingredients: a face in each square (UI sprite slots 0xB0-0xBB, moved
   to the squares while the screen is open), the selection frame on the face touched, its name, the Rep
   Group's relationship bar and the Beads in the text box, the page in the small box. Like the mixer, the phone use ends while
   the screen is open (the city waits behind it) and the player goes back to where they stood. The call
   or invitation itself is the game's own (phone_call / phone_invite: the answer, the face, +1
   relationship, the visit), made once the city runs again, with the player picking up the phone, holding
   it while the answer shows and hanging up (the game's own animations; see mod_on_tick). */
#include "game.h"
#include "mod.h"

#define PHONE_MODE   7
#define COLS         7                 /* the squares: 7 across, 2 down */
#define PER_PAGE     14
#define GRID_C0      (30 - 4 * COLS)   /* the grid's left edge (map column): it ends on the text box's right edge */
#define S_CALL       0x1C              /* phone use states: Call Friend's list, Invite's list */
#define S_INVITE     0x1D
#define STR_HELP     8311              /* mods/phone/text/strings.tsv */
#define STR_CALL     325               /* "Call Friend" */
#define STR_INVITE   334               /* "Invite a Friend Over" */
#define STR_NAME     481               /* + person id (31-66): the name */

#define G(name, type) GAME_FN(ADDR_##name, type)
#define screen        GAME_VAR(ADDR_screen_index, u32)
#define BLOCK(s, off) (*(volatile u32 *)(ADDR_screen_block + (off) + 0x8C * (s)))
#define MODE(s)       BLOCK(s, 0x74)
#define RESULT0       BLOCK(0, 0xD4)   /* the top screen's dialog_result */
#define touched       GAME_VAR(ADDR_touch_state + 0x52, u16)
#define phone_list    ((volatile s8 *)ADDR_phone_list)
#define phone_count   GAME_VAR(ADDR_phone_count, u8)
#define phone_page    GAME_VAR(ADDR_phone_page, u8)
#define buttons       ((u8 *volatile *)(ADDR_obj_buttons + 0x50 * screen))
#define B_CURSOR      8                /* the selection frame (made by obj_screen_buttons) */
#define B_OK          0xF              /* the round green button: stays hidden (A or two touches call) */
#define B_CLOSE       0x10
#define B_SPARE1      12               /* the mixers' blender squares: obj_screen_buttons expects them */
#define B_SPARE2      13
#define B_NEXT        9                /* ours: the next page (the mixer keeps blender squares in 9-11) */
#define B_PREV        10               /* ours: the page before */
#define K_LEFT        0                /* round button kinds: 0 left, 1 right, 2 up, 3 down, 4 back, 5 tick */
#define K_RIGHT       1
#define NEXT_X        236              /* the arrows on either side of the name, under the faces (owner's */
#define PREV_X        20               /* picture, 2026-11-20); the red arrow (the game's, slot 0x10) stays */
#define ARROW_Y       94               /* in the bottom strip's middle (128,179) */
#define SLOT_SQUARE   0xB0             /* UI sprite slots of the 12 squares (the mixer's 8 squares and its 4
                                          blender ones, unused in mode 7): moved to our squares while open */
#define SQ_X(i)       (8 * GRID_C0 + 20 + 32 * ((i) % COLS))   /* a square's centre (as Pockets' slots 0x4C-0x53) */
#define SQ_Y(i)       (36 + 32 * ((i) / COLS))

#define set_state_here  G(set_state_here, void (*)(int, int, int))
#define set_state_other G(set_state_other, void (*)(int, int, int))
#define obj_screen_buttons G(obj_screen_buttons, void (*)(void))
#define obj_screen_draw G(obj_screen_draw, void (*)(int))
#define obj_screen_touch G(obj_screen_touch, void (*)(void))
#define button_hit      G(button_hit, int (*)(int))
#define button_new      G(button_new, u8 *(*)(int, int, int, int, int, int))
#define ui_sprite_new   G(ui_sprite_new, u8 *(*)(int, int, int, int))
#define face_image      G(face_image, int (*)(int))
#define text_get        G(text_get, const char *(*)(int))
#define text_draw       G(text_draw, void (*)(int, int, int, const char *, int))
#define text_font       G(text_font, void (*)(int, int, int))
#define map_fill        G(map_fill, void (*)(int, int, int, int, int, int))
#define TEXT_LAYER   0                 /* Pockets' text layer (BG0) */
#define clear_panel()   map_fill(TEXT_LAYER, 1, 10, 30, 7, 0)  /* the text box's rows (y 80-135) */
#define ui_sound        G(ui_sound, void (*)(int))
#define entity_new      G(entity_new, u8 *(*)(int))
#define entity_records  G(entity_records, void (*)(u8 *, u32))
#define set_palette_row G(set_palette_row, void (*)(u8 *, int))
#define entity_set_pos  G(entity_set_pos, void (*)(u8 *, s32, s32))
#define entity_set_frame G(entity_set_frame, void (*)(u8 *, int))
#define entity_free     ((void (*)(u8 *))0x02003838)
#define palette_load    ((void (*)(int, u32))0x0206D8D8)   /* (sprite palette row, palette record) */
#define RELATIONSHIP(id) (*(volatile s8 *)(ADDR_relationships + 4 * ((id) - 31)))
#define BEAD_GIVEN(id) (*(volatile u8 *)(ADDR_bead_given + 4 * ((id) - 31)))
#define BAR_PAL_ROW  0xD
#define NAME_Y       87                /* the text box: the name where Pockets writes the item's, the bar under it */
#define BAR_X        30
#define BAR_Y        103
#define NUM_X        178
#define PAGE_Y       147               /* the small box (Pockets' money) */
/* The red Beads (the Collectibles screen's, ui_sprite_table row 20) under the bar: this person gave you theirs */
#define BEAD_SPRITE  20
#define BEAD_PAL_ROW 0xD               /* the bar's colours (owner 2026-11-20: another colour is fine) */
#define CURSOR_ROW   0xD               /* the selection frame too: its own greys are nearly the bar's */
#define BEAD_X       106               /* drawn about 8 px left of and 3 px above this */
#define BEAD_Y       128
/* Sprite palette rows: each face needs its own (their colours differ); 0xA is the round buttons', 0xD the
   bar's, which the Beads and the selection frame share (their own rows were 0xC and 0xF). That leaves 14
   rows, so 14 faces a page. */
static const u8 face_row[PER_PAGE] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xB, 0xC, 0xE, 0xF };
#define ui_sprites   (*(const u8 *const *)0x0204ED20)   /* ui_sprite_table (moved when a mod adds objects) */
#define phone_call      G(phone_call, void (*)(int, void *))
#define phone_invite    G(phone_invite, void (*)(int, void *))

/* Pockets' look (state 13's enter, 0x0204036C, and the city menu's frame, state 0x22's enter 0x0207121C):
   the bottom screen's layers set up as Pockets has them (BG0 the text, BG1 the panel, BG3 the frame; BG2
   off), their backgrounds loaded by the game's own loader, and the text colours (palette 0x286D, row 0). */
#define layers_off   G(layers_off, void (*)(u32))
#define display_on   G(display_on, void (*)(u32))
#define layer_setup  G(layer_setup, void (*)(int, u32))
#define layer_clear  G(layer_clear, void (*)(int, int))
#define bg_load      G(bg_load, int (*)(int, u32, int, int, int, int))
#define pal_load     G(pal_load, u32 (*)(u32, int))
#define text_layer   G(text_layer, void (*)(u32))
#define CFG_TEXT     0x1C09            /* as Pockets: char 2, map 28 (0x020C77B0) */
#define CFG_PANEL    0x1B02            /* char 0, map 27 (0x020C77AC) */
#define CFG_FRAME    0x1F11            /* char 4, map 31 (the city menu's, 0x020F563C) */
#define CFG_MENU_BG  0x1E12            /* the city menu's BG2 (0x02071194) */
#define CFG_EMPTY    0x1D00            /* Pockets' BG2: empty (0x020C77A8) */
#define PIC_MENU_BG  0x2891            /* the city menu's BG2 picture: it brings the BG colours (all rows) */
#define PIC_PANEL    0x288D            /* Pockets' panel (squares, text box, small box) */
#define PIC_FRAME    0x286E            /* the city menu's frame */
#define PAL_TEXT     0x286D
#define PANEL_MAP    ((volatile u16 *)(0x06200000 + 27 * 0x800))

/* Pockets' panel has 5 squares on the left and 4 x 2 on the right. The right ones' tiles, row by row (map
   rows 2-10: top edge, 3 inside, the middle edge, 3 inside, bottom edge): column 14 the left edge, 15-17 a
   square, 18 the edge between two, 30 the right edge; column 0 the plain dark. Laid out again 6 across from
   column 4, each square is 5 tiles wide with its edges (centres x 52 + 32 i, y 36 / 68). */
static u16 tile_tpl[9][7];             /* per row: plain, left edge, square x 3, between, right edge */
static u16 box_top[3];                 /* map row 10 is also the text box's top: its left corner (column 1), the
                                          plain line (10-13, under the gap) and its right corner (30) */
static u8 tpl_ok;

static void grid_draw(void)
{
    volatile u16 *m = PANEL_MAP;
    if (!tpl_ok) {
        for (int r = 0; r < 9; r++) {
            const volatile u16 *row = m + 32 * (r + 2);
            u16 t[7] = { row[0], row[14], row[15], row[16], row[17], row[18], row[30] };
            for (int k = 0; k < 7; k++)
                tile_tpl[r][k] = t[k];
        }
        box_top[0] = m[32 * 10 + 1], box_top[1] = m[32 * 10 + 10], box_top[2] = m[32 * 10 + 30];
        tpl_ok = 1;
    }
    for (int r = 0; r < 9; r++) {
        volatile u16 *row = m + 32 * (r + 2);
        const u16 *t = tile_tpl[r];
        int last = r == 8;                       /* the bottom edge, on the text box's top line */
        for (int c = 1; c <= 30; c++)
            row[c] = last ? box_top[1] : t[0];
        if (last)
            row[1] = box_top[0], row[30] = box_top[2];
        row[GRID_C0] = t[1];
        for (int k = 0; k < COLS; k++) {
            row[GRID_C0 + 1 + 4 * k] = t[2];
            row[GRID_C0 + 2 + 4 * k] = t[3];
            row[GRID_C0 + 3 + 4 * k] = t[4];
            row[GRID_C0 + 4 + 4 * k] = k + 1 < COLS ? t[5] : t[6];   /* the last: the box's right corner */
        }
    }
}

static void pockets_look(void)
{
    layers_off(0);                               /* BG2 (the mixer's frames) stays off */
    display_on(0x1000);
    /* as the city menu does (city_menu_enter, then 0x02071194): its frame on BG3, then its BG2 picture, which
       brings the colours the frame and Pockets' panel are drawn with (the panel and the frame have none; the
       Mixer's enter has just put its own there: the panel showed in its colours) */
    layer_setup(3, CFG_FRAME);
    bg_load(3, PIC_FRAME, 2, 0, 0, 0);
    layer_setup(2, CFG_MENU_BG);
    bg_load(2, PIC_MENU_BG, 0xF7, 0, 0, 0);
    /* then as Pockets does (pockets_enter): BG2 emptied, the text layer, the panel, the text colours */
    layer_setup(0, CFG_TEXT);
    layer_clear(0, 0);
    layer_setup(1, CFG_PANEL);
    layer_setup(2, CFG_EMPTY);
    layer_clear(2, 0);
    text_layer(CFG_TEXT);
    bg_load(1, PIC_PANEL, 1, 0, 0, 0);
    pal_load(PAL_TEXT, 0);
    tpl_ok = 0;                                  /* a fresh panel: read the squares' tiles again */
    grid_draw();
}

/* The squares' UI sprite slots (ui_slot_pos: s16 x, y, x2, y2 each) moved to our squares while the screen is
   open, and put back when it goes (and before the game's own touch screens open, should it ever close another
   way). The selection frame and the faces go wherever their slot says. */
#define SLOT_POS     ((volatile s16 *)ADDR_ui_slot_pos)
static s16 slot_saved[PER_PAGE][4];
static u8 slots_moved;

static void slots_move(void)
{
    for (int i = 0; i < PER_PAGE; i++) {
        volatile s16 *p = SLOT_POS + 4 * (SLOT_SQUARE + i);
        if (!slots_moved)
            for (int k = 0; k < 4; k++)
                slot_saved[i][k] = p[k];
        p[0] = p[2] = (s16)SQ_X(i);
        p[1] = p[3] = (s16)SQ_Y(i);
    }
    slots_moved = 1;
}

static void slots_back(void)
{
    if (!slots_moved)
        return;
    for (int i = 0; i < PER_PAGE; i++)
        for (int k = 0; k < 4; k++)
            SLOT_POS[4 * (SLOT_SQUARE + i) + k] = slot_saved[i][k];
    slots_moved = 0;
}

typedef struct { u8 *player; u8 pad[3]; u8 facing; s16 x, y; } return_spot_t;
#define return_spot (*(volatile return_spot_t *)ADDR_return_spot)

static u8 *use_rec;                    /* the phone use (record) seen last */
static u8 *use_node;                   /* its phone (object node: +0x24 the entity, +0x3B the phone kind) */
static u8 phone_kind;                  /* 0 pay phone, 1-3 home phones (rows of phone_anims) */
static u8 kind;                        /* S_CALL or S_INVITE */
static u8 page;                        /* first list entry shown */
static s8 sel = -1;                    /* square selected on this page */
static s8 picked = -1;                 /* list index chosen, made once the city runs again */
static s8 armed = -1;                  /* square touched last: a second touch on it makes the call */
static s8 want_sel = -1;               /* square to select once the next page is drawn (D-pad) */
static u8 wait;                        /* ticks to wait before making it */
#define WAIT_TICKS   2                 /* was 16 (owner 2026-11-20: the answer came too late) */
static u8 dummy_rec[12];               /* phone_call / phone_invite set the use's state in it */
static u8 last_id;                     /* the last person called or invited (0 = nobody yet); saved */
static u8 start_facing;                /* facing when the phone use started (turned towards the phone) */
static u8 refacing;                    /* ticks left to face the phone again after the screens close */
static u8 *face[PER_PAGE];             /* the faces on this page (sprites of the screen: freed with it) */

static void show(int slot, int on);
/* The selection frame (button slot 8, a UI sprite) onto square i: its slot (+8) and back to state 0 (+0x105),
   when the UI sprite's update (0x0204E7B8) moves it to its slot's place, as the mixer's touches do. */
#define ui_sprite_state ((void (*)(u8 *, int))0x02000E54)
static void move_cursor(int i)
{
    u8 *c = buttons[B_CURSOR];
    if (c) {
        *(u16 *)(c + 8) = (u16)(SLOT_SQUARE + i);
        ui_sprite_state(c, 0);
    }
}
static void bar_make(void);
static void bar_free(void);
static u8 *bar;                        /* the relationship bar (an entity of this screen) */
static u8 *bead;                       /* the Beads picture under it (the same kind of entity) */

/* Every tick of a phone use (wrap phone_activity): keep the record. */
void phone_seen(void *node, u8 *rec)
{
    use_node = node;
    use_rec = rec;
    if (rec && *(u16 *)(rec + 6) == 0 && return_spot.player)   /* the use starts: the way we face the phone */
        start_facing = return_spot.player[0x12];
}

static int count(void)
{
    int n = 0;
    while (n < 36 && phone_list[n] >= 0)
        n++;
    return n < phone_count ? n : phone_count;
}

static void open_screens(void);

/* Call Friend / Invite chosen (instead of the box of 4 names): open the touch screens. */
void phone_faces_open(void)
{
    u8 *rec = use_rec;
    kind = rec ? *(u16 *)(rec + 6) : S_CALL;
    phone_kind = use_node ? use_node[0x3B] & 3 : 1;   /* set by the use's start (0 pay phone, 1-3 home) */
    if (rec) {                         /* end the use and stand back where we were, as the mixer does */
        u8 *pl = return_spot.player;
        *(u16 *)(rec + 6) = 1;
        GAME_VAR(ADDR_area_entry_point, u8) = 0xFF;   /* no entry point: back to return_spot */
        if (pl) {
            return_spot.facing = pl[0x12];
            return_spot.x = (s16)(*(s32 *)(pl + 0x18) >> 16);
            return_spot.y = (s16)(*(s32 *)(pl + 0x1C) >> 16);
        }
    }
    open_screens();
}

/* For other mods (the cellphone, 2026-11-20): the faces screen from anywhere, with no phone object. They
   find this table by its magic and its own address (mods/cellphone/code/main.c). The cellphone runs the
   phone's menu itself (with the game's own boxes when this mod is missing or switched off). */
typedef struct {
    u32 magic, self, version;
    int (*open_anywhere)(void);        /* version 1: straight to Call Friend */
    int (*open_menu)(void);            /* version 2 only (the menu is the cellphone's since version 3): 0 */
    int (*open_faces)(int invite);     /* version 3: Call Friend (0) or Invite a Friend Over (1) */
} phone_api_t;
#define PHONE_API_MAGIC 0x454E4F50u      /* 'PONE' */
#define people_known G(people_known, int (*)(void *, s8 *, int, int, int))

static u8 *player_entity(void)
{
    for (u8 *e = *(u8 **)ADDR_entity_lists; e; e = *(u8 **)e)
        if (*(u16 *)(e + 8) == 0)
            return e;
    return 0;
}

/* The first 4 letters of a person's name, as phone_open sorts them. */
static u32 name_key(int id)
{
    const u8 *s = (const u8 *)text_get(STR_NAME + id);
    return (u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3];
}

/* phone_open's list without its choice box: the people you know (relationship -100..100), by name. */
static void make_list(void)
{
    s8 known[36];
    int n = people_known((void *)ADDR_social_book, known, 36, -100, 100), out = 0;
    phone_count = (u8)n;
    for (; out < n; out++) {
        int best = -1;
        u32 key = 0xFFFFFFFFu;
        for (int j = 0; j < n; j++)
            if (known[j] >= 0 && (best < 0 || name_key(known[j]) < key))
                best = j, key = name_key(known[j]);
        if (best < 0)
            break;
        phone_list[out] = known[best];
        known[best] = -1;
    }
    for (; out < 36; out++)
        phone_list[out] = -1;
}

/* The cellphone: the Call Friend screen wherever the player stands. The player comes back to the same
   spot and picks up, talks and hangs up with the home phone's animations (there is no phone object). */
static int open_anywhere_as(int k)
{
    u8 *pl = player_entity();
    if (!pl)
        return 0;
    make_list();
    use_rec = 0;
    use_node = 0;
    kind = (u8)k;
    phone_kind = 1;
    start_facing = pl[0x12];
    return_spot.player = pl;
    return_spot.facing = pl[0x12];
    return_spot.x = (s16)(*(s32 *)(pl + 0x18) >> 16);
    return_spot.y = (s16)(*(s32 *)(pl + 0x1C) >> 16);
    GAME_VAR(ADDR_area_entry_point, u8) = 0xFF;
    open_screens();
    return 1;
}

static int phone_open_anywhere(void)
{
    return open_anywhere_as(S_CALL);
}

static int phone_open_faces(int invite)
{
    return open_anywhere_as(invite ? S_INVITE : S_CALL);
}

__attribute__((used, retain)) const phone_api_t phone_api = {
    PHONE_API_MAGIC, (u32)&phone_api, 3, phone_open_anywhere, 0, phone_open_faces
};

static void open_screens(void)
{
    page = 0;                                    /* the page of the last person called */
    for (int i = 0, n = count(); i < n; i++)
        if (last_id && phone_list[i] == last_id)
            page = (u8)(i - i % PER_PAGE);
    sel = -1;
    want_sel = -1;
    phone_page = 0;
    picked = -1;
    RESULT0 = 0x7FFFFFFF;
    set_state_here(0x25, PHONE_MODE, 0);         /* top: the help screen */
    set_state_other(0x1F, PHONE_MODE, 0);        /* bottom: the touch screen */
}

/* State 31 enter, after the game's own buttons: a face in each square. */
void phone_screen_buttons(void)
{
    if (MODE(screen) != PHONE_MODE) {
        slots_back();                            /* the mixer's squares where the game has them */
        obj_screen_buttons();
        return;
    }
    slots_move();                                /* before the selection frame is made at the first one */
    /* obj_screen_buttons changes the flags (+0xC) of buttons 12 and 13, which only the mixers' modes make
       (their blender squares). In mode 7 they are empty: the game wrote to address 0x0C, which DeSmuME
       lets pass and melonDS (and the DS) stops on (a data abort: the screen went black and stayed so). */
    static u32 spare[2][4];
    buttons[B_SPARE1] = (u8 *)spare[0];
    buttons[B_SPARE2] = (u8 *)spare[1];
    obj_screen_buttons();
    if (buttons[B_CURSOR])                       /* the frame made its colours row 0xF: it takes the bar's */
        set_palette_row(buttons[B_CURSOR], CURSOR_ROW);
    buttons[B_SPARE1] = 0;
    buttons[B_SPARE2] = 0;
    int n = count();
    for (int i = 0; i < 8; i++)                  /* the mixer's ingredient buttons: none */
        buttons[i] = 0;
    for (int i = 0; i < PER_PAGE; i++) {
        face[i] = 0;
        if (page + i < n)                        /* its own palette row, as the mixer's ingredients */
            face[i] = ui_sprite_new(SLOT_SQUARE + i, face_image(phone_list[page + i]), face_row[i], 2);
    }
    sel = -1;                                    /* the last person called, if on this page */
    armed = -1;
    if (want_sel >= 0 && page + want_sel < n && face[want_sel]) {   /* the D-pad went to another page */
        sel = want_sel;
        if (buttons[B_CURSOR])
            move_cursor(sel);
    }
    int keep = want_sel >= 0;
    want_sel = -1;
    for (int i = 0; !keep && i < PER_PAGE && page + i < n; i++)
        if (last_id && phone_list[page + i] == last_id && face[i]) {
            sel = (s8)i;
            if (buttons[B_CURSOR])
                move_cursor(i);
        }
    if (sel < 0 && face[0]) {                    /* nobody called yet (or not on this page): the first face,
                                                    where the game draws the frame anyway */
        sel = 0;
        if (buttons[B_CURSOR])
            move_cursor(0);
    }
    buttons[B_NEXT] = n > PER_PAGE ? button_new(K_RIGHT, 10, 0, 1, NEXT_X << 16, ARROW_Y << 16) : 0;
    buttons[B_PREV] = n > PER_PAGE ? button_new(K_LEFT, 10, 0, 1, PREV_X << 16, ARROW_Y << 16) : 0;
}

/* The text box under the squares: the selected person's name, the relationship and the Beads. */
static void draw_panel(void)
{
    clear_panel();
    if (bar)
        *(u32 *)(bar + 0xC) &= ~1u;
    if (bead)
        *(u32 *)(bead + 0xC) &= ~1u;
    if (sel < 0)
        return;
    int id = phone_list[page + sel];
    if (bead && BEAD_GIVEN(id))                  /* they gave you their Beads */
        *(u32 *)(bead + 0xC) |= 1;
    text_font(3, 0, -1);
    text_draw(0x1F, 128, NAME_Y, text_get(STR_NAME + id), 1);
    /* the relationship: the Rep Group's bar and its number (-100..100) */
    int rel = RELATIONSHIP(id);
    if (bar) {
        int f = (rel + 100) / 2;
        entity_set_frame(bar, f < 0 ? 0 : f > 100 ? 100 : f);
        *(u32 *)(bar + 0xC) |= 1;
    }
    char num[5], *p = num + 4;
    int v = rel < 0 ? -rel : rel;
    *p = 0;
    do *--p = (char)('0' + v % 10); while ((v /= 10) && p > num + 1);
    if (rel < 0)
        *--p = '-';
    text_font(1, 0, -1);
    text_draw(0x47, NUM_X, BAR_Y + 2, p, 2);
}

/* The Rep Group's relationship bar (as its screen makes it, 0x0207C264): entity 0x19 with the record at
   0x020F6F04, palette 0x020F6F00 in sprite palette row 0xD; its frame (0-100) is (relationship + 100) / 2.
   It is the state's own entity: freed by us when the screen goes (the Rep Group frees its own on leave). */
static void bar_free(void)
{
    if (bar) {
        *(u32 *)(bar + 0xC) &= ~1u;
        entity_free(bar);
        bar = 0;
    }
    if (bead) {
        *(u32 *)(bead + 0xC) &= ~1u;
        entity_free(bead);
        bead = 0;
    }
}

/* The Beads: a UI sprite record as the mod core's info-page pictures (palette row first, then the record) */
static void bead_make(void)
{
    bead = entity_new(0x19);
    if (!bead)
        return;
    set_palette_row(bead, BEAD_PAL_ROW);
    entity_records(bead, (u32)(ui_sprites + 16 * BEAD_SPRITE));
    entity_set_frame(bead, 0);
    entity_set_pos(bead, BEAD_X << 16, BEAD_Y << 16);
    *(u32 *)(bead + 0x90) &= 0xFFFFF3FFu;
    *(u32 *)(bead + 0xC) |= 0x200009;
    *(u32 *)(bead + 0xC) &= ~1u;                 /* hidden until someone who gave theirs is selected */
}

static void bar_make(void)
{
    bar_free();
    bead_make();                                 /* first: it loads its red into the row, the bar's replaces it */
    palette_load(BAR_PAL_ROW, 0x020F6F00);
    bar = entity_new(0x19);
    if (!bar)
        return;
    entity_records(bar, 0x020F6F04);
    set_palette_row(bar, BAR_PAL_ROW);
    entity_set_pos(bar, BAR_X << 16, BAR_Y << 16);
    *(u32 *)(bar + 0x90) &= 0xFFFFF3FFu;
    *(u32 *)(bar + 0xC) |= 0x200009;
    *(u32 *)(bar + 0xC) &= ~1u;                  /* hidden until someone is selected */
}

/* State 31 enter: the title (instead of the mixer's name). */
void phone_screen_title(int part)
{
    if (MODE(screen) != PHONE_MODE) {
        obj_screen_draw(part);
        return;
    }
    pockets_look();
    text_font(3, 0, -1);
    text_draw(1, 128, 5, text_get(kind == S_INVITE ? STR_INVITE : STR_CALL), 1);
    int n = count();
    if (n > PER_PAGE) {                          /* the page, in the small box: "1/2" */
        char s[8], *p = s;
        int a = page / PER_PAGE + 1, b = (n + PER_PAGE - 1) / PER_PAGE;
        if (a >= 10)
            *p++ = (char)('0' + a / 10);
        *p++ = (char)('0' + a % 10);
        *p++ = '/';
        if (b >= 10)
            *p++ = (char)('0' + b / 10);
        *p++ = (char)('0' + b % 10);
        *p = 0;
        text_draw(0x55, 128, PAGE_Y, s, 1);
    }
}

/* State 31 enter, last: the first part to show (the mixer draws an item into the panel). */
void phone_screen_part(int part)
{
    if (MODE(screen) != PHONE_MODE) {
        obj_screen_draw(part);
        return;
    }
    bar_make();
    draw_panel();
}

static void close_screens(void)
{
    bar_free();
    slots_back();
    refacing = 15;                               /* the city comes back facing elsewhere: face the phone */
    set_state_here(0x22, 0, 0);                  /* bottom: the city's menus again */
    set_state_other(1, 2, GAME_VAR(ADDR_current_area, u32));   /* top: the city, coming back */
}

static void show(int slot, int on)
{
    u8 *b = buttons[slot];
    if (b)
        *(u32 *)(b + 0xC) = on ? *(u32 *)(b + 0xC) | 1 : *(u32 *)(b + 0xC) & ~1u;
}

/* Square i selected (a touch or the D-pad): the frame on it, the green button, the name in the panel. */
static void select(int i)
{
    ui_sound(1);
    sel = (s8)i;
    armed = -1;
    if (buttons[B_CURSOR])
        move_cursor(i);
    draw_panel();
}

/* The green button, A, or a second touch on the selected face: made once the city runs again. */
static void make_call(void)
{
    ui_sound(1);
    picked = (s8)(page + sel);
    last_id = (u8)phone_list[page + sel];
    wait = WAIT_TICKS;                           /* the city back first (mod_on_tick) */
    close_screens();
}

/* State 31 update, sub-screen 1 (the game's touches). */
void phone_screen_touch(void)
{
    if (MODE(screen) != PHONE_MODE) {
        obj_screen_touch();
        return;
    }
    int n = count();
    u16 keys = GAME_VAR(ADDR_input_state + 10, u16);   /* pressed this frame */
    static u8 was_touched;
    int tap = touched && !was_touched;           /* a new touch, not the same one held down */
    was_touched = touched != 0;
    if (!tap) {
        if (keys & KEY_B) {                      /* B: hang up */
            ui_sound(2);
            close_screens();
        } else if (keys & KEY_A) {               /* A: call the one selected (or select the first) */
            if (sel >= 0)
                make_call();
            else if (face[0])
                select(0);
        } else if (keys & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN | KEY_L | KEY_R)) {
            /* the 12 squares are 6 across, 2 down; left / right go on to the next or last page */
            int at = page + (sel < 0 ? 0 : sel), to = at;
            if (sel >= 0 && keys & KEY_RIGHT) to = at + 1 < n ? at + 1 : 0;
            if (sel >= 0 && keys & KEY_LEFT) to = at > 0 ? at - 1 : n - 1;
            if (sel >= 0 && keys & KEY_DOWN && sel < COLS && at + COLS < n) to = at + COLS;
            if (sel >= 0 && keys & KEY_UP && sel >= COLS) to = at - COLS;
            if (keys & KEY_R) to = page + PER_PAGE < n ? page + PER_PAGE : 0;
            if (keys & KEY_L) to = page >= PER_PAGE ? page - PER_PAGE : (n - 1) - (n - 1) % PER_PAGE;
            if (to - to % PER_PAGE != page) {    /* another page: draw it, then select there */
                ui_sound(1);
                page = (u8)(to - to % PER_PAGE);
                want_sel = (s8)(to % PER_PAGE);
                bar_free(), set_state_here(0x1F, PHONE_MODE, 0);
            } else if (face[to - page]) {
                select(to - page);
            }
        }
        return;
    }
    want_sel = -1;
    if (button_hit(B_CLOSE)) {                   /* hang up */
        ui_sound(2);
        close_screens();
        return;
    }
    int next = buttons[B_NEXT] && button_hit(B_NEXT), prev = !next && buttons[B_PREV] && button_hit(B_PREV);
    if (next || prev) {                          /* the next / last page (round from the end) */
        ui_sound(1);
        if (next)
            page = page + PER_PAGE < n ? page + PER_PAGE : 0;
        else
            page = page >= PER_PAGE ? page - PER_PAGE : (n - 1) - (n - 1) % PER_PAGE;
        bar_free(), set_state_here(0x1F, PHONE_MODE, 0);     /* the screen again, with them */
        return;
    }
    int tx = GAME_VAR(ADDR_touch_state + 0x0E, u16), ty = GAME_VAR(ADDR_touch_state + 0x10, u16);
    for (int i = 0; i < PER_PAGE; i++)          /* the 30 x 30 box button_hit uses, around the square */
        if (face[i] && tx >= SQ_X(i) - 15 && tx < SQ_X(i) + 15 && ty >= SQ_Y(i) - 15 && ty < SQ_Y(i) + 15) {
            if (armed == i && sel == i) {        /* the same face touched twice: call */
                make_call();
                return;
            }
            select(i);
            armed = (s8)i;
            return;
        }
}

/* State 37 enter (top): the title and the help text. */
const char *phone_help_title(int id)
{
    if (MODE(screen) == PHONE_MODE)
        id = kind == S_INVITE ? STR_INVITE : STR_CALL;
    return text_get(id);
}

const char *phone_help_text(int id)
{
    return text_get(MODE(screen) == PHONE_MODE ? STR_HELP : id);
}

/* The last person called, kept in each save slot (one byte). */
int mod_on_save(u8 *buf, int max)
{
    if (max < 1)
        return -1;
    buf[0] = last_id;
    return 1;
}

void mod_on_load(const u8 *buf, int len)
{
    last_id = (buf && len >= 1 && buf[0] >= 31 && buf[0] <= 66) ? buf[0] : 0;
}

/* The player on the phone, as the game's own phone use (phone_activity states 8, 0x19, 0x1E): back in the
   city they pick it up (the phone kind's animation, and the pay phone's handset off), the call is made
   when that is done, they hold it while the answer is on screen, and hang up after. */
#define phone_anims     ((const u8 *)ADDR_phone_anims)
#define ANIM(row, k, f) phone_anims[12 * (row) + 3 * (k) + (f)]
#define object_set_look G(object_set_look, void (*)(u8 *, int))
#define entity_play_anim G(entity_play_anim, void (*)(u8 *, int))
#define ANIM_DONE    0x400000          /* entity +0xC: the animation has ended (the phone use waits for it) */
#define SND_PICK_UP  0x1C9
#define SND_HANG_UP  0x13B
#define TOP_STATE    BLOCK(0, 0x70)
#define TOP_DIALOG   2                 /* the top screen's state while the answer is shown */
#define TALK_TICKS   20
#define CALL_TICKS   12                /* the answer comes this long into the pick-up (not at its end) */
#define PL_STATE     0x104             /* the player's state: 0x10 = free to walk, 0 = held (as pets-kit does) */
enum { A_NONE, A_PICK_UP, A_TALK, A_HANG_UP };
static u8 anim_step, anim_ticks;
static u8 *anim_pl;                    /* the player being animated (return_spot.player is cleared later) */
static u8 *held_pl;                    /* kept still from the screens' close to the hang-up (owner 2026-11-20:
                                          walking off in between did strange things) */

static void hold(u8 *pl)
{
    if (pl && pl[PL_STATE] == 0x10) {
        entity_set_state(pl, 0);
        held_pl = pl;
    }
}

static void release(void)
{
    if (held_pl && held_pl[PL_STATE] == 0)     /* (anything else is the game's own: leave it) */
        entity_set_state(held_pl, 0x10);
    held_pl = 0;
}

static void phone_look(int row)
{
    int look = ANIM(row, phone_kind, 1);
    if (look != 6 && (row != 1 || phone_kind == 0) && use_node && *(u8 **)(use_node + 0x24))   /* the game's own:
                                                    the hang-up look only for the pay phone */
        object_set_look(*(u8 **)(use_node + 0x24), look);
}

static void make_it(int i)
{
    if (kind == S_INVITE)
        phone_invite(i, dummy_rec);
    else
        phone_call(i, dummy_rec);
}

#define phone_open_box  G(phone_open, void (*)(void))
#define is_home_area    G(is_home_area, int (*)(void *, int))

/* The phone's choices (call phone_open in phone_activity state 0x19). At home the game's box (Call Services
   / Call Friend / Invite). Away its box would hold only Call Friend: no box, the list as phone_open makes
   it and the answer "Call Friend" (1), read by state 0x1A on the next tick, which opens the faces (or
   hangs up when nobody is known). */
void phone_choices(void)
{
    if (is_home_area((void *)ADDR_home_lot, GAME_VAR(ADDR_current_area, u32) & 0xFF)) {
        phone_open_box();
        return;
    }
    make_list();
    RESULT0 = 1;
}

/* The city runs again: pick up, make the call or the invitation, hang up. */
void mod_on_tick(void)
{
    u8 *pl = return_spot.player;
    if (refacing && pl) {
        refacing--;
        pl[0x12] = start_facing;
    }
    if (anim_step != A_NONE) {
        pl = anim_pl;
        anim_ticks++;
        hold(pl);                                           /* (again if the answer box gave them back) */
        int mine = pl[0xC7] == ANIM(anim_step == A_HANG_UP, phone_kind, 2);   /* still ours (not walking away) */
        if (anim_step == A_PICK_UP && (!mine || (*(u32 *)(pl + 0xC) & ANIM_DONE) || anim_ticks > CALL_TICKS)) {
            int i = picked;
            picked = -1;
            anim_step = A_TALK, anim_ticks = 0;
            make_it(i);
        } else if (anim_step == A_TALK) {
            /* the city (and this tick) waits while the answer is on screen; once it is gone, still on the
               line a moment (the game's state 0x1F counts 30), then hang up */
            if (!mine) {                                    /* the player moved on: leave them be */
                phone_look(2);
                anim_step = A_NONE, release();
            } else if (TOP_STATE != TOP_DIALOG && anim_ticks > TALK_TICKS) {
                ui_sound(SND_HANG_UP);
                phone_look(1);
                entity_play_anim(pl, ANIM(1, phone_kind, 2));
                anim_step = A_HANG_UP, anim_ticks = 0;
                if (phone_kind != 0)                        /* home phones: putting it down is standing */
                    anim_step = A_NONE, phone_look(2), release();
            }
        } else if (anim_step == A_HANG_UP && (!mine || (*(u32 *)(pl + 0xC) & ANIM_DONE) || anim_ticks > 120)) {
            phone_look(2);                                  /* the phone as it was; the player stands */
            if (mine)
                entity_play_anim(pl, ANIM(2, phone_kind, 2));
            anim_step = A_NONE, release();
        }
        return;
    }
    if (picked < 0)
        return;
    hold(pl);                                               /* from the first tick back in the city */
    if (wait && --wait)
        return;
    if (!pl) {                                              /* no player to animate: just make it */
        int i = picked;
        picked = -1;
        make_it(i);
        return;
    }
    anim_pl = pl;
    pl[0x12] = start_facing;                                /* the animation takes the facing */
    ui_sound(SND_PICK_UP);
    phone_look(0);
    entity_play_anim(pl, ANIM(0, phone_kind, 2));
    anim_step = A_PICK_UP, anim_ticks = 0;
}
