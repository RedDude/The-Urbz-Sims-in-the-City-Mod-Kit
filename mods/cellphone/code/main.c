/* Cellphone: a button in the middle of Urb Info that uses the phone wherever the player is (no phone
   object needed): the Sim picks up and the phone's own options come up, as with a phone object (Phone
   mod 0.6, phone_api version 2; with 0.5 it goes straight to Call Friend's touch screen).

   Urb Info is menu 0 of the bottom-screen menus (docs/systems.md "Bottom-screen menus"): a header and 4
   buttons in a 2 x 2 grid. The mod core keeps the menus in its own array (the game's 4 literals point at
   it), so this mod copies Urb Info, adds a 5th button at the centre and puts the copy in that array. A
   touch on it returns our action; the game stores it in menu_ui+0xC and calls menu_dispatch, a call this
   mod takes over: it asks for the phone (or buzzes) and leaves Urb Info open either way.
   The phone itself is the Phone mod's: it publishes a small table (phone_api in mods/phone/code/main.c)
   that this mod finds by its magic word and its own address. */
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
    int (*open_menu)(void);            /* version 2: the phone's own options, as with a phone object */
} phone_api_t;
#define PHONE_API_MAGIC 0x454E4F50u     /* 'PONE' */
#define SCAN_BYTES      0x10000        /* around the Phone mod's event functions */

#define menu_ui   (*(menu_ui_t *volatile *)ADDR_menu_ui)
#define can_save  GAME_FN(ADDR_can_save, int (*)(void))
#define ui_sound  GAME_FN(ADDR_ui_sound, void (*)(int))

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

/* The Phone mod's table: its row in the mod table, then a scan near its code. */
static const phone_api_t *find_api(void)
{
    if (api)
        return api;
    mod_table_t *t = mod_table;
    for (int i = 0; i < t->count && phone_row < 0; i++)
        if (t->rows[i].hash == name_hash("phone"))
            phone_row = i;
    if (phone_row < 0)
        return 0;
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
        if (p->magic == PHONE_API_MAGIC && p->self == a && p->version >= 1 && p->open_anywhere) {
            api = p;
            break;
        }
    }
    return api;
}

static int phone_on(void)
{
    return find_api() && mod_table->rows[phone_row].on;
}

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
}

/* The touch handler's call of menu_dispatch (call menu_dispatch_call): the game's for every other action.
   Ours never calls it (it would close Urb Info, which then stays empty while the phone's box is up); the
   handler returns after it (the action isn't 0x22). */
#define menu_dispatch GAME_FN(ADDR_menu_dispatch, void (*)(void))
void cell_dispatch(void)
{
    menu_ui_t *m = menu_ui;
    if (!m || m->menu != MENU_URB_INFO || m->action != ACT_CELL) {
        menu_dispatch();
        return;
    }
    if (!phone_on() || !can_save()) {  /* no Phone mod, or busy (using something, walking to it, ...) */
        ui_sound(SND_REFUSED);         /* as the game's own refusal of Save Game (menu_hit_test returns -1) */
        return;
    }
    pending = 2;                       /* Urb Info stays open, as when using a phone object */
}

/* The city's tick (as the phone's own use opens its screens), once the menu has closed. */
void mod_on_tick(void)
{
    if (!pending || --pending)
        return;
    if (phone_on() && can_save()) {
        if (api->version >= 2)
            api->open_menu();          /* pick up, then Call Services / Call Friend / Invite (at home) */
        else
            api->open_anywhere();
    }
}
