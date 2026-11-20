/* The in-game Mods page (Phase 5, A4).

   The bottom-screen menus (Urb Info, Options, ...) are data: menu_table_ptrs holds one
   menu per index, and a menu is a header plus up to 8 buttons {x, y, icon frame; action;
   sub-menu; param; label string}. Action 0 opens the sub-menu named by the button, and the
   game's own code draws icons and labels and tests touches. So the core:
     - copies the 5 menu pointers into a longer array (index 5 = Mods page, 6 = a mod's
       info page) and points the game's 4 literals at it;
     - gives Options a 4th button, "Mods", that opens menu 5 (the same 2 x 2 layout as
       Urb Info: Settings, Save Game / Mods, Quit Game);
     - answers taps on the Mods page itself (menu_hit_call): switch the mod, redraw, stay.
   Labels are string ids; ids from 0xF000 are the core's own text (menu_label_call). */
#include "game.h"
#include "mod.h"
#include "core.h"

typedef struct {
    u32 pos;                  /* x | y << 8 | icon frame << 16 (x, y = icon centre) */
    u32 action;               /* 0 = open sub-menu `sub`; else returned to the game */
    u32 sub, param, label;
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

#define menu_ui      (*(menu_ui_t **)ADDR_menu_ui)
#define menu_hit_test  GAME_FN(ADDR_menu_hit_test, int (*)(void))
#define menu_redraw    GAME_FN(ADDR_menu_redraw, void (*)(void))
#define menu_draw_text GAME_FN(ADDR_menu_draw_text, void (*)(void))
#define text_draw      GAME_FN(ADDR_text_draw, void (*)(int, int, int, const char *, int))
#define text_font      GAME_FN(ADDR_text_font, void (*)(int, int, int))
#define text_width     GAME_FN(ADDR_text_width, int (*)(const char *))
#define LABEL_MAX_W    150                          /* a label's 40 text tiles hold about 160 px */
#define ui_sound       GAME_FN(ADDR_ui_sound, void (*)(int))
#define set_palette_row GAME_FN(ADDR_set_palette_row, void (*)(void *, int))
#define menu_sprites   ((void **)0x02144D10)        /* 8 button sprites of the open menu */

enum { MENU_OPTIONS = 2, MENU_MODS = 5, MENU_INFO = 6, N_MENUS = 7 };
enum { LABEL_BASE = 0xF000, LABEL_NONE = 0xF00D, LABEL_BACK = 0xF00E, LABEL_MODS = 0xF00F };
enum { ACT_ITEM = 0x60 };                         /* 0x60 + button: an item on the Mods page */
enum { ACT_ICON = 0x70 };                         /* 0x70 + i: picture i on an info page */
#define MAX_ICONS 4
#define SEL_SPRITE 14                                 /* ui_sprite_table: the goals' selection frame, 8 frames shining */
#define SEL_ROW    12
#define entity_records   GAME_FN(ADDR_entity_records, void (*)(void *, const void *))
#define entity_set_frame GAME_FN(ADDR_entity_set_frame, void (*)(void *, int))
#define ui_sprites (*(const u8 *const *)0x0204ED20)  /* ui_sprite_table (moved when a mod adds objects) */
#define INFO_LINES 5        /* + title + Back = the 6 labels a menu can safely hold (like the Catalog) */
enum { IT_TOGGLE, IT_INFO, IT_NEXT };
#define PER_PAGE 6

static const menu_t *menus[N_MENUS];
static menu_t options4, mods_menu, info_menu;
static char labels[8][28];
static char lines[INFO_LINES][32];
static int n_lines;
static struct { u8 kind, mod; } items[8];
static int page, info_mod;

static const char *mod_name(int i, char *out)
{
    const char *n = mod_table->rows[i].name;
    int k = 0;
    while (k < 15 && n[k]) {
        out[k] = n[k];
        k++;
    }
    out[k] = 0;
    return out;
}

/* Shorten text (in place) until it fits a label; the game corrupts the screen otherwise.
   Only call this while a menu is being drawn: it measures with the game's fonts, which are not
   loaded yet at boot (measuring then hung the game in melonDS: a white screen). */
static void fit_w(char *s, int w)
{
    int n = 0;
    while (s[n])
        n++;
    while (n > 0 && text_width(s) > w)
        s[--n] = 0;
}

static void fit(char *s)
{
    fit_w(s, LABEL_MAX_W);
}

static void cat(char *d, const char *s)
{
    while (*d)
        d++;
    while ((*d++ = *s++))
        ;
}

static menu_button_t button(int x, int y, int frame, int action, int sub, int label)
{
    menu_button_t b = { x | y << 8 | frame << 16, action, sub, 0, label };
    return b;
}

/* (Re)build the Mods page from the mod table: 2 columns x 3 rows. */
static void build_mods_page(void)
{
    mod_table_t *t = mod_table;
    int n = 0, skip = page * (PER_PAGE - 1), more = 0, k = 0;
    /* the list: each mod's switch, then "info" for mods with an info page */
    for (int i = 0; i < t->count; i++) {
        if (t->rows[i].flags & MODF_HIDDEN)
            continue;                               /* kit parts (e.g. new-assets) */
        for (int kind = IT_TOGGLE; kind <= IT_INFO; kind++) {
            if (kind == IT_INFO && !t->rows[i].ev[EV_PAGE])
                continue;
            if (kind == IT_TOGGLE && !(t->rows[i].flags & MODF_TOGGLE) && t->rows[i].ev[EV_PAGE])
                continue;                           /* always on, with a page: just the page */
            if (k++ < skip)
                continue;
            if (n == PER_PAGE) {
                more = 1;
                continue;
            }
            items[n].kind = kind;
            items[n].mod = i;
            n++;
        }
    }
    if (more) {                                     /* last slot: next page */
        n = PER_PAGE - 1;
        items[n].kind = IT_NEXT;
        n++;
    } else if (page && n < PER_PAGE) {             /* last page: back to the first */
        items[n].kind = IT_NEXT;
        n++;
    }
    mods_menu = options4;                            /* same icons and colours */
    mods_menu.count = n;
    for (int s = 0; s < n; s++) {
        int x = s & 1 ? 192 : 64, y = 28 + 46 * (s >> 1), frame;
        char nm[16];
        labels[s][0] = 0;
        if (items[s].kind == IT_NEXT) {
            cat(labels[s], more ? "More mods" : "First page");
            frame = 0;
        } else {
            mod_row_t *r = &t->rows[items[s].mod];
            cat(labels[s], mod_name(items[s].mod, nm));
            if (items[s].kind == IT_INFO) {
                if (r->flags & MODF_TOGGLE)
                    cat(labels[s], ": info");
                frame = 0;
            } else if (!(r->flags & MODF_TOGGLE)) {
                cat(labels[s], ": always on");
                frame = 1;
            } else {
                cat(labels[s], r->on ? ": ON" : ": OFF");
                if (r->runtime & 1)
                    cat(labels[s], " (save full)");
                frame = r->on ? 1 : 2;               /* bright arrow = on, grey = off */
            }
        }
        mods_menu.b[s] = button(x, y, frame, ACT_ITEM + s, 0, LABEL_BASE + s);
    }
}

void core_page_init(void)
{
    const menu_t *const *orig = (const menu_t *const *)ADDR_menu_table_ptrs;
    const menu_t *opt = (const menu_t *)ADDR_options_menu;
    for (int i = 0; i < 5; i++)
        menus[i] = orig[i];
    /* Options: Settings, Save Game / Mods, Quit Game (the Urb Info layout) */
    options4 = *opt;
    options4.count = 4;
    options4.b[0] = opt->b[0];
    options4.b[1] = opt->b[1];
    options4.b[2] = button(61, 120, 1, 0, MENU_MODS, LABEL_MODS);
    options4.b[3] = opt->b[2];
    options4.b[3].pos = 198 | 120 << 8 | (opt->b[2].pos & 0xFF0000);
    menus[MENU_OPTIONS] = &options4;
    build_mods_page();
    menus[MENU_MODS] = &mods_menu;
    info_menu = options4;
    info_menu.count = 1;
    info_menu.b[0] = button(226, 120, 3, 0, MENU_MODS, LABEL_BACK);
    menus[MENU_INFO] = &info_menu;
    static const u32 lits[4] = { ADDR_menu_ptr_lit_1, ADDR_menu_ptr_lit_2, ADDR_menu_ptr_lit_3,
                                 ADDR_menu_ptr_lit_4 };
    for (int i = 0; i < 4; i++) {
        *(volatile u32 *)lits[i] = (u32)menus;
        DC_FlushRange((void *)lits[i], 4);
    }
    DC_WaitWriteBufferEmpty();
    core.page_ready = 1;
}

/* Button labels: the core's own text for ids from 0xF000. */
const char *core_label(int id)
{
    if (id == LABEL_MODS)
        return "Mods";
    if (id == LABEL_NONE)
        return "";
    if (id == LABEL_BACK)
        return "Back";
    if (id >= LABEL_BASE && id < LABEL_BASE + 8) {
        fit(labels[id - LABEL_BASE]);                /* drawn now: the fonts are loaded */
        return labels[id - LABEL_BASE];
    }
    return text_get(id);
}

/* An info page line (mod_on_page's print). The core draws the lines into the text tiles the
   game gives labels 2-6, inside the rectangle the game clears before drawing any menu
   (x 16-248, y 8-152), so nothing is left behind on other menus. */
static void page_print(mod_page_t *p, const char *s)
{
    int k = p->line++ - p->scroll;
    if (k < 0 || k >= INFO_LINES)
        return;
    int n = 0;
    while (n < 31 && s[n]) {
        lines[k][n] = s[n];
        n++;
    }
    lines[k][n] = 0;
    if (k >= n_lines)
        n_lines = k + 1;
}

static int info_scroll, info_total;
static u32 info_keys;                                /* keys for the page being built (mod_page_t.keys_down) */
static int info_tapped = -1;                         /* the picture tapped, for the page being built */
static struct { u16 sprite; u8 x, y; char label[12]; } icons[MAX_ICONS];
static int n_icons, info_sel = -1;                   /* the picture with the selection frame (-1: none) */
static u32 sel_ticks;

/* A picture on an info page (mod_page_t.icon): a touchable button drawn from a ui_sprite_table row. */
static void page_icon(mod_page_t *p, int sprite, int x, int y, const char *label)
{
    (void)p;
    if (n_icons >= MAX_ICONS)
        return;
    icons[n_icons].sprite = sprite;
    icons[n_icons].x = x;
    icons[n_icons].y = y;
    int k = 0;
    while (label && k < 11 && label[k]) {
        icons[n_icons].label[k] = label[k];
        k++;
    }
    icons[n_icons].label[k] = 0;
    n_icons++;
}

static void build_info_page(int mod)
{
    mod_row_t *r = &mod_table->rows[mod];
    mod_page_t p = { page_print, 0, info_keys, info_scroll, 0, page_icon, info_tapped, -1 };
    n_lines = 0;
    n_icons = 0;
    if (r->ev[EV_PAGE])
        ((void (*)(mod_page_t *))r->ev[EV_PAGE])(&p);
    info_total = p.line;
    if (info_scroll + INFO_LINES < info_total) {     /* more below: the last line says so */
        const char *more = "(R: more, L: back)";
        int n = 0;
        while (more[n]) {
            lines[INFO_LINES - 1][n] = more[n];
            n++;
        }
        lines[INFO_LINES - 1][n] = 0;
        n_lines = INFO_LINES;
    }
    if (n_icons && n_lines > INFO_LINES - 1)         /* the 5th line's text tiles hold the pictures' labels */
        n_lines = INFO_LINES - 1;
    info_menu.count = 1 + n_icons;                   /* Back and the pictures; the lines are drawn below */
    for (int i = 0; i < n_icons; i++)
        info_menu.b[1 + i] = button(icons[i].x, icons[i].y, 0, ACT_ICON + i, 0, LABEL_NONE);
    info_sel = p.selected >= 0 && p.selected < n_icons ? p.selected : -1;
    if (info_sel >= 0)                               /* the frame: one more button on the same spot */
        info_menu.b[info_menu.count++] = button(icons[info_sel].x, icons[info_sel].y, 0, ACT_ICON + info_sel, 0,
                                                LABEL_NONE);
}

/* Every tick: on an info page longer than 5 lines, R / L scroll it (2026-10-09: it never did; the D-pad walks
   you about while a menu is open). */
static void redraw_info(void)
{
    /* the world tick runs with the top screen current: draw the menu as its own (bottom) screen, as a tap would */
    volatile u32 *scr = (volatile u32 *)ADDR_screen_index;
    u32 was = *scr;
    *scr = 1;
    build_info_page(info_mod);
    menu_redraw();
    ui_sound(4);
    *scr = was;
}

/* A page that fits gets R / L itself (2026-11-19, for settings like Walk Style's): the mod sees them in
   keys_down, and the page is drawn again only if its text changed. */
static void page_keys(u16 keys)
{
    char was[INFO_LINES][32];
    int n_was = n_lines;
    for (int k = 0; k < INFO_LINES; k++)
        for (int c = 0; c < 32; c++)
            was[k][c] = lines[k][c];
    info_keys = keys & (KEY_R | KEY_L);
    build_info_page(info_mod);
    info_keys = 0;
    int same = n_lines == n_was;
    for (int k = 0; same && k < n_lines; k++)
        for (int c = 0; c < 32 && (was[k][c] || lines[k][c]); c++)
            if (was[k][c] != lines[k][c]) {
                same = 0;
                break;
            }
    if (!same)
        redraw_info();
}

void core_page_tick(void)
{
    menu_ui_t *m = menu_ui;
    if (!m || m->menu != MENU_INFO)
        return;
    u16 keys = *(volatile u16 *)(ADDR_input_state + 10);
    if (info_sel >= 0 && menu_sprites[1 + n_icons] && !(++sel_ticks & 3)) {   /* the frame shines, as on the goals */
        volatile u32 *scr = (volatile u32 *)ADDR_screen_index;
        u32 was = *scr;
        *scr = 1;
        entity_set_frame(menu_sprites[1 + n_icons], (sel_ticks >> 2) & 7);
        *scr = was;
    }
    if (info_total <= INFO_LINES) {
        if (keys & (KEY_R | KEY_L))
            page_keys(keys);
        return;
    }
    int s = info_scroll;
    if (keys & 0x100)
        s++;
    if (keys & 0x200)
        s--;
    if (s > info_total - INFO_LINES)
        s = info_total - INFO_LINES;
    if (s < 0)
        s = 0;
    if (s == info_scroll)
        return;
    info_scroll = s;
    redraw_info();
}

/* After the game drew a menu's text: titles and info lines for our menus, icon colours. */
void core_menu_text(void)
{
    menu_draw_text();
    menu_ui_t *m = menu_ui;
    int idx = m ? (int)m->menu : -1;
    if (idx == MENU_OPTIONS || idx == MENU_MODS || idx == MENU_INFO) {
        const menu_t *mn = menus[idx];
        for (int i = 0; i < mn->count; i++)         /* icon frame k uses palette row k */
            if (menu_sprites[i])
                set_palette_row(menu_sprites[i], (mn->b[i].pos >> 16) & 0xFF);
    }
    if (idx == MENU_MODS) {
        text_font(3, 0, -1);
        text_draw(1, 128, 8, "Mods", 1);          /* where the game draws menu titles */
        text_font(1, 0, -1);
    } else if (idx == MENU_INFO) {
        char nm[16];
        text_font(3, 0, -1);
        text_draw(1, 128, 8, mod_name(info_mod, nm), 1);
        text_font(1, 0, -1);
        for (int k = 0; k < n_lines; k++) {
            fit(lines[k]);
            text_draw(0x51 + 0x28 * k, 20, 28 + 16 * k, lines[k], 0);
        }
        for (int i = 0; i < n_icons; i++) {          /* the pictures: their own records instead of the menu's */
            if (menu_sprites[1 + i]) {
                set_palette_row(menu_sprites[1 + i], 8 + i);   /* first: the record's palette loads into this row */
                entity_records(menu_sprites[1 + i], ui_sprites + 16 * icons[i].sprite);
                entity_set_frame(menu_sprites[1 + i], 0);
            }
            if (icons[i].label[0]) {                 /* 20 text tiles each, from the 5th line's (pages with
                                                        pictures have 4 lines); about 80 px */
                fit_w(icons[i].label, 44);
                text_draw(0x51 + 0x28 * 4 + 20 * i, icons[i].x, icons[i].y + 18, icons[i].label, 1);
            }
        }
        if (info_sel >= 0 && menu_sprites[1 + n_icons]) {   /* the selection frame, over its picture */
            set_palette_row(menu_sprites[1 + n_icons], SEL_ROW);
            entity_records(menu_sprites[1 + n_icons], ui_sprites + 16 * SEL_SPRITE);
            entity_set_frame(menu_sprites[1 + n_icons], 0);
        }
    }
}

/* A touch on the open menu: the Mods page's buttons are handled here. */
int core_menu_hit(void)
{
    int r = menu_hit_test();
    menu_ui_t *m = menu_ui;
    if (m && m->menu == MENU_INFO && r >= ACT_ICON && r < ACT_ICON + MAX_ICONS) {
        info_tapped = r - ACT_ICON;                  /* the page sees it in mod_page_t.tapped */
        build_info_page(info_mod);
        info_tapped = -1;
        menu_redraw();
        ui_sound(1);
        return -1;
    }
    if (!m || m->menu != MENU_MODS || r < ACT_ITEM || r >= ACT_ITEM + 8)
        return r;
    int s = r - ACT_ITEM;
    core.page_taps++;
    if (items[s].kind == IT_NEXT) {
        mod_table_t *t = mod_table;
        int total = 0;
        for (int i = 0; i < t->count; i++)
            if (!(t->rows[i].flags & MODF_HIDDEN))
                total += (t->rows[i].ev[EV_PAGE] && !(t->rows[i].flags & MODF_TOGGLE)) ? 1 :
                         1 + (t->rows[i].ev[EV_PAGE] != 0);
        page = (page + 1) * (PER_PAGE - 1) < total ? page + 1 : 0;
    } else if (items[s].kind == IT_INFO) {
        info_mod = items[s].mod;
        info_scroll = 0;
        build_info_page(info_mod);
        m->menu = MENU_INFO;
        menu_redraw();
        ui_sound(4);
        return -1;
    } else {
        mod_row_t *row = &mod_table->rows[items[s].mod];
        if (row->flags & MODF_TOGGLE)
            core_set(items[s].mod, !row->on);
        else
            ui_sound(3);
    }
    build_mods_page();
    menu_redraw();
    return -1;
}
