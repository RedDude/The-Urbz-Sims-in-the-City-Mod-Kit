/* Cellphone: a button in the middle of Urb Info that uses the phone wherever the player is (no phone
   object needed). The Sim picks up and the phone's own choices come up, as with a phone object: at home
   Call Services / Call Friend / Invite a Friend Over; away from home Call Friend is the only choice, so it
   opens straight away. Call Friend and Invite use the Phone mod's faces screen when that mod is in the ROM
   and switched on (phone_api version 3), and the game's own box of 4 names otherwise: this mod works alone.

   Urb Info is menu 0 of the bottom-screen menus (docs/systems.md "Bottom-screen menus"): a header and 4
   buttons in a 2 x 2 grid. The mod core keeps the menus in its own array (the game's 4 literals point at
   it), so this mod copies Urb Info, adds a 5th button at the centre and puts the copy in that array. A
   touch on it returns our action; the game stores it in menu_ui+0xC and calls menu_dispatch, a call this
   mod takes over: it starts the phone (or buzzes) and leaves Urb Info open either way.

   The menu runs as phone_activity does with a phone object (docs/systems.md "The telephone"): pick up
   (state 8), the question box from phone_open (0x1A), Call Services' box (0x1B), the order and 30 ticks on
   the line (0x1F), the hang-up sending the service (0x1E); Call Friend / Invite: the box of 4 names (0x1C /
   0x1D: 0 = next page, n = phone_call / phone_invite(page + n - 1)), the answer, 30 ticks, hang up. There
   is no use record (a zeroed one stands in) and nothing to pay. */
#include "game.h"
#include "mod.h"

#define MENU_URB_INFO 0              /* index 0 (0x020F5744; seen in RAM 2026-11-20), the city's own bottom menu */
#define ACT_CELL      0x50             /* unused by the game (its actions go up to 0x28) and by the core (0x60+) */
#define STR_CELL      8320             /* "Cellphone" (mods/cellphone/text/strings.tsv) */
#define CELL_X        128              /* icon centre: between the 4 buttons (61/198, 57/120) */
#define CELL_Y        88
#define CELL_FRAME    4                /* icon.png: frame 4 (and palette row 4) of Urb Info's sheet, added at
                                          build time (mod.json "urb_info_icon", kit/urbz_menuicons.py) */
#define SND_REFUSED   3
#define SND_PICK_UP   0x1C9
#define SND_HANG_UP   0x13B

typedef struct {
    u32 pos, action, sub, param, label;
} menu_button_t;

typedef struct {
    u32 gfx, layout, z0, z1, pal16, pal256;
    u16 count, pad;
    menu_button_t b[8];
} menu_t;

typedef struct {
    u32 unk0, open, menu, action;
    u16 selected;
} menu_ui_t;

typedef struct {                       /* the same as phone_api_t in mods/phone/code/main.c */
    u32 magic, self, version;
    int (*open_anywhere)(void);        /* version 1: straight to Call Friend */
    int (*open_menu)(void);            /* version 2 only */
    int (*open_faces)(int invite);     /* version 3: the faces screen, Call Friend (0) or Invite (1) */
} phone_api_t;
#define PHONE_API_MAGIC 0x454E4F50u     /* 'PONE' */
#define SCAN_BYTES      0x10000        /* around the Phone mod's event functions */

#define G(name, type) GAME_FN(ADDR_##name, type)
#define menu_ui   (*(menu_ui_t *volatile *)ADDR_menu_ui)
#define can_save  G(can_save, int (*)(void))
#define ui_sound  G(ui_sound, void (*)(int))
#define entity_play_anim G(entity_play_anim, void (*)(u8 *, int))
#define is_home_area     G(is_home_area, int (*)(void *, int))
#define people_known     G(people_known, int (*)(void *, s8 *, int, int, int))
#define phone_open_box   G(phone_open, void (*)(void))
#define names_box        G(phone_names_box, void (*)(void))
#define services_box     G(phone_services_box, void (*)(void))
#define service_order    G(service_order, void (*)(int, void *))
#define service_call     G(service_call, void (*)(int))
#define phone_call       G(phone_call, void (*)(int, void *))
#define phone_invite     G(phone_invite, void (*)(int, void *))
#define phone_list   ((volatile s8 *)ADDR_phone_list)
#define phone_count  GAME_VAR(ADDR_phone_count, u8)
#define phone_page   GAME_VAR(ADDR_phone_page, u8)
#define RESULT0      (*(volatile u32 *)(ADDR_screen_block + 0xD4))   /* the top screen's dialog_result */
#define TOP_STATE    (*(volatile u32 *)(ADDR_screen_block + 0x70))
#define TOP_DIALOG   2                 /* the top screen's state while a box is up (the city waits) */
#define ANIM_DONE    0x400000          /* entity +0xC: the animation has ended */
#define ANIM(row)    (((const u8 *)ADDR_phone_anims)[12 * (row) + 3 * 1 + 2])   /* the home phone's (kind 1):
                                          0 pick up 0x51, 1 hang up, 2 stand */
#define STR_NAME     481               /* + person id: the name */
#define CANCEL       0x7FFFFFFF
#define NO_RESULT    0x7FFFFFFE
#define PICKUP_TICKS 30                /* at most (0x51 may not set the end bit when we play it) */
#define LINE_TICKS   30                /* on the line after the answer (state 0x1F counts 30) */

static menu_t urb5;                    /* Urb Info with the Cellphone button */
static const menu_t *urb4;             /* the game's own */
static const phone_api_t *api;
static int phone_row = -1;
static u8 pending;                     /* ticks until the phone opens (the menu closes first) */

/* A mod's name hash (mod_row_t.hash: FNV-1a of its folder name, kit/urbz_code.py name_hash). The row's
   name is its title ("Phone"), which may change. */
static u32 name_hash(const char *s)
{
    u32 h = 0x811C9DC5u;
    while (*s)
        h = (h ^ (u8)*s++) * 0x01000193u;
    return h ? h : 1;
}

/* The Phone mod's table: its row in the mod table, then a scan near its code. None: the game's boxes. */
static const phone_api_t *find_api(void)
{
    if (api || phone_row == -2)
        return api;
    mod_table_t *t = mod_table;
    for (int i = 0; i < t->count && phone_row < 0; i++)
        if (t->rows[i].hash == name_hash("phone"))
            phone_row = i;
    if (phone_row < 0) {
        phone_row = -2;                /* not in this ROM: don't look again */
        return 0;
    }
    u32 lo = 0xFFFFFFFFu, hi = 0;
    for (int e = 0; e < MOD_EVENTS; e++) {
        u32 a = (u32)t->rows[phone_row].ev[e];
        if (a) {
            lo = a < lo ? a : lo;
            hi = a > hi ? a : hi;
        }
    }
    if (!hi)
        return 0;
    lo = (lo > (u32)t + SCAN_BYTES ? lo - SCAN_BYTES : (u32)t) & ~3u;
    hi = (hi + SCAN_BYTES) & ~3u;
    for (u32 a = lo; a < hi; a += 4) {
        const phone_api_t *p = (const phone_api_t *)a;
        if (p->magic == PHONE_API_MAGIC && p->self == a && p->version >= 3 && p->open_faces) {
            api = p;
            break;
        }
    }
    return api;
}

/* The faces screen: the Phone mod is in the ROM and switched on. */
static int faces_on(void)
{
    return find_api() && mod_table->rows[phone_row].on;
}

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

/* ---- the phone's menu ---- */
enum { M_NONE, M_PICK_UP, M_CHOICE, M_SERVICES, M_NAMES, M_LINE };
static u8 step, ticks, home, invite, service;
static u8 *pl;
static u8 rec[12];                     /* stands in for the use record: +6 = the use's state (0x1F on the line) */

static void hang_up(void)
{
    ui_sound(SND_HANG_UP);
    if (pl[0xC7] == ANIM(0))
        entity_play_anim(pl, ANIM(1));
    step = M_NONE;
}

static void ask(void (*box)(void), int next)
{
    RESULT0 = NO_RESULT;
    box();
    step = (u8)next;
}

/* Call Friend (0) or Invite (1) chosen: the faces screen (the Phone mod picks up again for the call) or the
   game's box of 4 names. */
static void friends(int inv)
{
    if (faces_on()) {
        step = M_NONE;
        entity_play_anim(pl, ANIM(2));
        api->open_faces(inv);
        return;
    }
    invite = (u8)inv;
    phone_page = 0;
    ask(names_box, M_NAMES);
}

static void start(void)
{
    pl = player_entity();
    if (!pl)
        return;
    home = (u8)is_home_area((void *)ADDR_home_lot, GAME_VAR(ADDR_current_area, u32) & 0xFF);
    if (!home) {                       /* away the only choice is Call Friend: no question box */
        make_list();
        if (phone_list[0] < 0) {       /* nobody to call */
            ui_sound(SND_REFUSED);
            return;
        }
        if (faces_on()) {              /* straight to the faces (they pick up for the call itself) */
            api->open_faces(0);
            return;
        }
    }
    ui_sound(SND_PICK_UP);
    entity_play_anim(pl, ANIM(0));
    step = M_PICK_UP, ticks = 0;
}

/* Every city tick (the city waits while a box is up, so a result is read once it has closed). */
static void menu_tick(void)
{
    if (step == M_NONE)
        return;
    int mine = pl[0xC7] == ANIM(0);    /* still holding the phone (not walking away) */
    ticks++;
    if (TOP_STATE == TOP_DIALOG)
        return;
    int r = (int)RESULT0;
    switch (step) {
    case M_PICK_UP:
        if (!mine)                     /* walked off: no menu */
            step = M_NONE;
        else if ((*(u32 *)(pl + 0xC) & ANIM_DONE) || ticks > PICKUP_TICKS) {
            if (home)
                ask(phone_open_box, M_CHOICE);   /* phone_list and the question box */
            else
                friends(0);            /* (the list is made) */
        }
        break;
    case M_CHOICE:
        if (r == 0)
            ask(services_box, M_SERVICES);
        else if ((r == 1 && phone_list[0] >= 0) || r == 2)
            friends(r == 2);
        else
            hang_up();
        break;
    case M_SERVICES:
        if (r >= 0 && r <= 2) {
            service = (u8)(r == 0 ? 0 : r + 1);
            GAME_VAR(ADDR_phone_service, u8) = (u8)r;
            *(u16 *)(rec + 6) = 0;
            service_order(service, rec);         /* the answer (or "not enough money") */
            ticks = 0, step = M_LINE;
        } else {
            hang_up();
        }
        break;
    case M_NAMES:
        if (r == 0) {                            /* the next 4 */
            phone_page = (u8)(phone_page + 4 >= phone_count ? 0 : phone_page + 4);
            ask(names_box, M_NAMES);
        } else if (r > 0 && r <= 4 && phone_page + r - 1 < phone_count) {
            *(u16 *)(rec + 6) = 0;
            service = 0xFF;                      /* (no service to send) */
            if (invite)
                phone_invite(phone_page + r - 1, rec);
            else
                phone_call(phone_page + r - 1, rec);
            ticks = 0, step = M_LINE;
        } else {
            hang_up();
        }
        break;
    case M_LINE:
        if (*(u16 *)(rec + 6) == 0x1F && ticks <= LINE_TICKS)
            break;
        hang_up();
        if (*(u16 *)(rec + 6) == 0x1F && service != 0xFF)
            service_call(service);
        break;
    }
}

/* ---- the button ---- */

/* Put Urb Info with (on) or without the Cellphone button into the menu array. */
static void install(int on)
{
    const menu_t **menus = *(const menu_t ***)ADDR_menu_ptr_lit_1;
    if (!urb4) {
        urb4 = menus[MENU_URB_INFO];
        urb5 = *urb4;                  /* (copies past the 4 buttons too: overwritten or unused) */
        int n = urb4->count < 7 ? urb4->count : 7;
        urb5.count = (u16)(n + 1);
        menu_button_t b = { CELL_X | CELL_Y << 8 | CELL_FRAME << 16, ACT_CELL, 0, 0, STR_CELL };
        urb5.b[n] = b;
    }
    menus[MENU_URB_INFO] = on ? &urb5 : urb4;
}

void mod_on_boot(void)
{
    install(1);
    find_api();
}

void mod_on_enable(void)
{
    install(1);
}

void mod_on_disable(void)
{
    install(0);
    pending = 0;
    step = M_NONE;
}

/* The touch handler's call of menu_dispatch (call menu_dispatch_call): the game's for every other action.
   Ours never calls it (it would close Urb Info, which then stays empty while the phone's box is up); the
   handler returns after it (the action isn't 0x22). */
#define menu_dispatch G(menu_dispatch, void (*)(void))
void cell_dispatch(void)
{
    menu_ui_t *m = menu_ui;
    if (!m || m->menu != MENU_URB_INFO || m->action != ACT_CELL) {
        menu_dispatch();
        return;
    }
    if (step != M_NONE || !can_save()) {  /* busy (using something, walking to it, on the phone, ...) */
        ui_sound(SND_REFUSED);         /* as the game's own refusal of Save Game (menu_hit_test returns -1) */
        return;
    }
    pending = 2;                       /* Urb Info stays open, as when using a phone object */
}

/* The city's tick (as the phone's own use opens its screens), once the menu has closed. */
void mod_on_tick(void)
{
    menu_tick();
    if (!pending || --pending)
        return;
    if (can_save())
        start();
}
