/* Phone: Call Friend and Invite a Friend Over open a touch screen like the Mixer's, with the faces of the
   people you know in its 8 squares (docs/systems.md "Object touch screens" and "The telephone").

   The game's object touch screen is state 31 on the bottom screen and state 37 (the help text) on the top;
   its mode picks the object (0/1 mixers, 2 oven; 3-7 are unused copies of the mixer's row). The phone uses
   mode 7, so the game draws the mixer's screen and its close button. This code adds, the way the mixer
   does for ingredients: a face in each square (UI sprite slots 0xB0-0xB7, kept in button slots 0-7),
   the selection frame on the face touched, its name and the Rep Group's relationship bar in the panel, and the round
   button (the mixer's "mix", button slot 0xF) to make the call. Like the mixer, the phone use ends while
   the screen is open (the city waits behind it) and the player goes back to where they stood. The call
   or invitation itself is the game's own (phone_call / phone_invite: the answer, the face, +1
   relationship, the visit), made once the city runs again, with the player picking up the phone, holding
   it while the answer shows and hanging up (the game's own animations; see mod_on_tick). */
#include "game.h"
#include "mod.h"

#define PHONE_MODE   7
#define PER_PAGE     8
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
#define B_NEXT        9                /* ours: the next 8 people (the mixer keeps blender squares in 9-11) */
#define B_PREV        10               /* ours: the 8 before */
#define K_LEFT        0                /* round button kinds: 0 left, 1 right, 2 up, 3 down, 4 back, 5 tick */
#define K_RIGHT       1
#define NEXT_X        130              /* where the green button was (it stays hidden) */
#define PREV_X        16               /* the same on the panel's left */
#define ARROW_Y       101
#define SLOT_SQUARE   0xB0             /* UI sprite slots of the 8 squares */

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
#define clear_panel()   map_fill(1, 1, 12, 17, 10, 0)  /* the panel's text rows, to its right edge (the number) */
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
#define NAME_Y       96                /* the panel: the name, then the bar and its number */
#define BAR_X        3
#define BAR_Y        114
#define NUM_X        139
/* The red Beads (the Collectibles screen's, ui_sprite_table row 20) under the bar: this person gave you theirs */
#define BEAD_SPRITE  20
#define BEAD_PAL_ROW 0xC
#define BEAD_X       70                /* drawn about 8 px left of and 3 px above this: centred in the space */
#define BEAD_Y       138               /* between the bar's edge (y 128) and the panel's bottom (155) */
#define ui_sprites   (*(const u8 *const *)0x0204ED20)   /* ui_sprite_table (moved when a mod adds objects) */
#define phone_call      G(phone_call, void (*)(int, void *))
#define phone_invite    G(phone_invite, void (*)(int, void *))

/* The phone picture instead of the mixer's blender (mods/phone/art/make_art.py writes phone_art.inc).
   The bottom screen: BG3 = the right-hand panel with the blender in it (plain purple tile 0x6002, columns
   21-31), BG2 = the frames, with the blender's 3 squares at columns 24-28, rows 4-15 (blank tile 0x011B);
   both use tiles from char base 0, and the game's own go up to 297. The picture goes on BG2 (so its
   see-through pixels show the purple below) in tiles 0x200 onwards, colours in BG palette row 15.
   The picture is not in the repo (phone_art.inc is made locally from your own PNG): without it, the
   blender stays. */
#if __has_include("phone_art.inc")
#include "phone_art.inc"
#define HAVE_ART 1
#else
#define HAVE_ART 0
#define ART_W 1
#define ART_H 1
static const u16 art_pal[16];
static const u32 art_tiles[8];
#endif
#define ART_TILE     0x200
#define ART_ROW      15
#define ART_X        24                /* tiles: the picture's top-left on the screen */
#define ART_Y        7
#define BG_PURPLE    0x6002
#define BG2_BLANK    0x011B
#define BG_CHAR(n)   (0x06200000 + ((GAME_VAR(0x04001008 + 2 * (n), u16) >> 2) & 15) * 0x4000)
#define SUB_BG_PAL   0x05000400
static u8 art_pending;                 /* frames left to (re)draw it: the game may load the screen late */

static void draw_art(void)
{
    if (!HAVE_ART)
        return;
    volatile u32 *t = (volatile u32 *)(BG_CHAR(2) + ART_TILE * 32);
    for (int k = 0; k < ART_W * ART_H * 8; k++)
        t[k] = art_tiles[k];
    volatile u16 *p = (volatile u16 *)(SUB_BG_PAL + ART_ROW * 32);
    for (int k = 0; k < 16; k++)
        p[k] = art_pal[k];
    map_fill(3, 21, 0, 11, 24, BG_PURPLE);         /* the panel without the blender */
    map_fill(2, 24, 4, 5, 12, BG2_BLANK);          /* without its 3 squares */
    for (int y = 0; y < ART_H; y++)
        for (int x = 0; x < ART_W; x++)
            map_fill(2, ART_X + x, ART_Y + y, 1, 1, ART_ROW << 12 | (ART_TILE + y * ART_W + x));
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

/* For other mods (the cellphone, 2026-11-20): Call Friend from anywhere, with no phone object. They find
   this table by its magic and its own address (mods/cellphone/code/main.c). */
typedef struct {
    u32 magic, self, version;
    int (*open_anywhere)(void);        /* version 1: straight to Call Friend */
    int (*open_menu)(void);            /* version 2: the phone's own question box first (see phone_menu_tick) */
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

static int phone_open_menu(void);

__attribute__((used, retain)) const phone_api_t phone_api = {
    PHONE_API_MAGIC, (u32)&phone_api, 2, phone_open_anywhere, phone_open_menu
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
        obj_screen_buttons();
        return;
    }
    /* obj_screen_buttons changes the flags (+0xC) of buttons 12 and 13, which only the mixers' modes make
       (their blender squares). In mode 7 they are empty: the game wrote to address 0x0C, which DeSmuME
       lets pass and melonDS (and the DS) stops on (a data abort: the screen went black and stayed so). */
    static u32 spare[2][4];
    buttons[B_SPARE1] = (u8 *)spare[0];
    buttons[B_SPARE2] = (u8 *)spare[1];
    obj_screen_buttons();
    buttons[B_SPARE1] = 0;
    buttons[B_SPARE2] = 0;
    int n = count();
    for (int i = 0; i < PER_PAGE; i++) {
        buttons[i] = 0;
        if (page + i < n)                        /* its own palette row, as the mixer's ingredients */
            buttons[i] = ui_sprite_new(SLOT_SQUARE + i, face_image(phone_list[page + i]), i, 2);
    }
    sel = -1;                                    /* the last person called, if on this page */
    armed = -1;
    if (want_sel >= 0 && page + want_sel < n && buttons[want_sel]) {   /* the D-pad went to another page */
        sel = want_sel;
        if (buttons[B_CURSOR])
            move_cursor(sel);
    }
    int keep = want_sel >= 0;
    want_sel = -1;
    for (int i = 0; !keep && i < PER_PAGE && page + i < n; i++)
        if (last_id && phone_list[page + i] == last_id && buttons[i]) {
            sel = (s8)i;
            if (buttons[B_CURSOR])
                move_cursor(i);
        }
    if (sel < 0 && buttons[0]) {                 /* nobody called yet (or not on this page): the first face,
                                                    where the game draws the frame anyway */
        sel = 0;
        if (buttons[B_CURSOR])
            move_cursor(0);
    }
    buttons[B_NEXT] = n > PER_PAGE ? button_new(K_RIGHT, 10, 0, 1, NEXT_X << 16, ARROW_Y << 16) : 0;
    buttons[B_PREV] = n > PER_PAGE ? button_new(K_LEFT, 10, 0, 1, PREV_X << 16, ARROW_Y << 16) : 0;
}

/* The panel under the squares: the selected person's name and where they are. */
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
    text_draw(0x1F, 72, NAME_Y, text_get(STR_NAME + id), 1);
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
    bead_make();
}

/* State 31 enter: the title (instead of the mixer's name). */
void phone_screen_title(int part)
{
    if (MODE(screen) != PHONE_MODE) {
        obj_screen_draw(part);
        return;
    }
    text_font(3, 0, -1);
    text_draw(1, 72, 6, text_get(kind == S_INVITE ? STR_INVITE : STR_CALL), 1);
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
    draw_art();
    art_pending = 2;
}

static void close_screens(void)
{
    bar_free();
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
    if (art_pending) {
        art_pending--;
        draw_art();
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
            else if (buttons[0])
                select(0);
        } else if (keys & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN | KEY_L | KEY_R)) {
            /* the 8 squares are 4 across, 2 down; left / right go on to the next or last page */
            int at = page + (sel < 0 ? 0 : sel), to = at;
            if (sel >= 0 && keys & KEY_RIGHT) to = at + 1 < n ? at + 1 : 0;
            if (sel >= 0 && keys & KEY_LEFT) to = at > 0 ? at - 1 : n - 1;
            if (sel >= 0 && keys & KEY_DOWN && sel < 4 && at + 4 < n) to = at + 4;
            if (sel >= 0 && keys & KEY_UP && sel >= 4) to = at - 4;
            if (keys & KEY_R) to = page + PER_PAGE < n ? page + PER_PAGE : 0;
            if (keys & KEY_L) to = page >= PER_PAGE ? page - PER_PAGE : (n - 1) - (n - 1) % PER_PAGE;
            if (to - to % PER_PAGE != page) {    /* another page: draw it, then select there */
                ui_sound(1);
                page = (u8)(to - to % PER_PAGE);
                want_sel = (s8)(to % PER_PAGE);
                bar_free(), set_state_here(0x1F, PHONE_MODE, 0);
            } else if (buttons[to - page]) {
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
    if (next || prev) {                          /* the next / last 8 people (round from the end) */
        ui_sound(1);
        if (next)
            page = page + PER_PAGE < n ? page + PER_PAGE : 0;
        else
            page = page >= PER_PAGE ? page - PER_PAGE : (n - 1) - (n - 1) % PER_PAGE;
        bar_free(), set_state_here(0x1F, PHONE_MODE, 0);     /* the screen again, with them */
        return;
    }
    for (int i = 0; i < PER_PAGE; i++)
        if (buttons[i] && button_hit(i)) {
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

/* The phone's own menu from anywhere (the cellphone, phone_api version 2), as phone_activity runs it with
   a phone object: pick up (state 8), the question box from phone_open (0x1A: Call Services / Call Friend /
   Invite a Friend Over at home, only Call Friend away), then Call Services' box (0x1B), the order, 30 ticks
   on the line (0x1F) and hang up, sending the service (0x1E). Call Friend and Invite open this mod's touch
   screen, as with a phone object. There is no use record and nothing to pay (the pay phone's 1 Simoleon). */
#define phone_open_box  G(phone_open, void (*)(void))
#define services_box    G(phone_services_box, void (*)(void))
#define service_order   G(service_order, void (*)(int, void *))
#define service_call    G(service_call, void (*)(int))
#define is_home_area    G(is_home_area, int (*)(void *, int))
#define CANCEL       0x7FFFFFFF
#define NO_RESULT    0x7FFFFFFE
#define PICKUP_TICKS 30                /* at most (0x51 may not set the end bit when we play it) */
#define LINE_TICKS   30                /* on the line after ordering (state 0x1F counts 30) */
enum { M_NONE, M_PICK_UP, M_CHOICE, M_SERVICES, M_LINE };
static u8 menu_step, menu_ticks, menu_home;
static u8 *menu_pl;

static void menu_hang_up(void)
{
    ui_sound(SND_HANG_UP);
    if (menu_pl[0xC7] == ANIM(0, 1, 2))
        entity_play_anim(menu_pl, ANIM(1, 1, 2));
    menu_step = M_NONE;
}

static int phone_open_menu(void)
{
    u8 *pl = player_entity();
    if (!pl || menu_step != M_NONE || anim_step != A_NONE || picked >= 0)
        return 0;
    menu_pl = pl;
    menu_home = (u8)is_home_area((void *)ADDR_home_lot, GAME_VAR(ADDR_current_area, u32) & 0xFF);
    ui_sound(SND_PICK_UP);
    entity_play_anim(pl, ANIM(0, 1, 2));
    menu_step = M_PICK_UP, menu_ticks = 0;
    return 1;
}

/* Every city tick (the city waits while a question box is up, so a result is read once it has closed). */
static int phone_menu_tick(void)
{
    if (menu_step == M_NONE)
        return 0;
    u8 *pl = menu_pl;
    int mine = pl[0xC7] == ANIM(0, 1, 2);        /* still holding the phone (not walking away) */
    menu_ticks++;
    if (TOP_STATE == TOP_DIALOG)
        return 1;
    int r = (int)RESULT0;
    switch (menu_step) {
    case M_PICK_UP:
        if (!mine) {                               /* walked off: no menu */
            menu_step = M_NONE;
        } else if ((*(u32 *)(pl + 0xC) & ANIM_DONE) || menu_ticks > PICKUP_TICKS) {
            RESULT0 = NO_RESULT;
            phone_open_box();                      /* phone_list and the question box */
            menu_step = M_CHOICE;
        }
        break;
    case M_CHOICE:
        if (!menu_home && r == 0)                  /* away there is only Call Friend */
            r = 1;
        if (r == 0) {
            RESULT0 = NO_RESULT;
            services_box();
            menu_step = M_SERVICES;
        } else if ((r == 1 && phone_list[0] >= 0) || r == 2) {
            menu_step = M_NONE;                    /* the touch screen; the call picks up again after */
            entity_play_anim(pl, ANIM(2, 1, 2));
            open_anywhere_as(r == 1 ? S_CALL : S_INVITE);
        } else {
            menu_hang_up();
        }
        break;
    case M_SERVICES:
        if (r >= 0 && r <= 2) {
            GAME_VAR(ADDR_phone_service, u8) = (u8)r;
            *(u16 *)(dummy_rec + 6) = 0;
            service_order(r == 0 ? 0 : r + 1, dummy_rec);   /* the answer (or "not enough money") */
            menu_ticks = 0;
            menu_step = M_LINE;
        } else {
            menu_hang_up();
        }
        break;
    case M_LINE:
        if (*(u16 *)(dummy_rec + 6) == 0x1F && menu_ticks <= LINE_TICKS)
            break;
        r = GAME_VAR(ADDR_phone_service, u8);
        menu_hang_up();
        if (*(u16 *)(dummy_rec + 6) == 0x1F && r <= 2)
            service_call(r == 0 ? 0 : r + 1);
        break;
    }
    return 1;
}

/* The city runs again: pick up, make the call or the invitation, hang up. */
void mod_on_tick(void)
{
    if (phone_menu_tick())
        return;
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
