/* Quick Start: at power-on, no logos and no title menu: the game goes straight into a saved game.

   How the game starts (docs/systems.md "Power-on: logos to the city"): both screens enter state 3 (the
   logos); its update runs on screen 0 and, after the logos, puts 0x21 on screen 0 and the title (5) on
   screen 1; 0x21's enter resets the game (game_reset). Load-an-Urb (state 7, screen 1) then reads the slot
   and calls set_state_other(1, 5, 0) (the city, load mode, on screen 0) and set_state_here(0x22) (the
   city's menus). We replace state 3's update (hooks.txt) and, once the logos' fade-in is over, do either of
   those straight away, the same way the game does.

   The choice (Options > Mods > Quick Start, R / L) is kept in the save, like any mod data. It has to be
   known at power-on, before a game is loaded, so mod_on_boot reads both slots from the cartridge and takes
   the choice from the most recent save (each save gets a number one higher than any seen). */
#include "game.h"
#include "mod.h"

#define set_state_here    GAME_FN(ADDR_set_state_here, void (*)(int state, int mode, int param))
#define set_state_other   GAME_FN(ADDR_set_state_other, void (*)(int state, int mode, int param))
#define screen_fade       GAME_FN(ADDR_screen_fade, void (*)(int mask, int on))
#define screen_level      GAME_FN(ADDR_screen_level, void (*)(int mask, int level))
#define fade_busy         GAME_FN(ADDR_fade_busy, int (*)(void))
#define game_reset        GAME_FN(ADDR_game_reset, void (*)(void))
#define sound_card_pause  GAME_FN(ADDR_sound_card_pause, void (*)(void))
#define sound_card_resume GAME_FN(ADDR_sound_card_resume, void (*)(void))
#define save_read_slot    GAME_FN(ADDR_save_read_slot, void (*)(int slot))
#define save_slot_valid   GAME_FN(ADDR_save_slot_valid, int (*)(int slot))
#define cur_screen        GAME_VAR(ADDR_cur_screen, u32)
#define save_card_busy    GAME_VAR(ADDR_save_card_busy, u8)
#define load_transition   GAME_VAR(ADDR_load_transition, u8)
#define save_slot_number  GAME_VAR(ADDR_save_ctx + 0x80, u32)   /* the slot game_start loads */
#define REG_KEYINPUT      GAME_VAR(0x04000130, u16)              /* 0 bits = held */
#define KEYIN_SELECT      4

#define ST_CITY 1
#define ST_TITLE 5
#define ST_TITLE_TOP 0x21
#define ST_CITY_MENU 0x22
#define MODE_LOAD 5
#define SLOT_ADDR(s) (0x20 + (s) * 0xFE0)
#define SLOT_SIZE 0xFE0

enum { LAST, SLOT1, SLOT2, TITLE, N_CHOICES };
static const char *const choice_names[N_CHOICES] = { "Last saved", "Slot 1", "Slot 2", "Title screen" };

/* 'QSTA' then state (tests find it with the magic). did: 0 not yet, 1 loaded a slot, 2 title (chosen or
   nothing to load), 3 title (SELECT held), 4 title (the slot didn't read back). */
struct {
    u32 magic;
    u8 choice, from_save, did, slot;
    u16 stamp[2];                /* the number of each slot's last save (0 = none of ours) */
    u16 next;                    /* the number for the next save */
    u16 scans;
    u8 valid[2];                 /* the slot holds a game (its checksum is right), from scan() */
} quick_start = { 0x41545351, LAST, 0, 0, 0xFF };

static u8 slot_buf[SLOT_SIZE];

static u32 get16(const u8 *p) { return p[0] | p[1] << 8; }
static u32 get32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }

int mod_on_save(u8 *buf, int max);

static u32 my_hash(void)                                 /* our row: the one whose save event is ours */
{                                                        /* (the row's name is the Mods page title) */
    mod_table_t *t = mod_table;
    for (int i = 0; i < t->count; i++)
        if (t->rows[i].ev[EV_SAVE] == (void *)mod_on_save)
            return t->rows[i].hash;
    return 0;
}

/* Our data in a slot's mod block ('MODS', 1, 0, then {u32 hash, u16 length, data}..., u32 0), or 0. */
static const u8 *find_ours(const u8 *d, u32 hash, int *len)
{
    for (int i = 0; i + 10 <= SAVE_SLOT_DATA; i++) {
        if (get32(d + i) != 0x53444F4DU || d[i + 4] != 1 || d[i + 5] != 0)
            continue;
        const u8 *hit = 0;
        int q = i + 6, n;
        while (q + 4 <= SAVE_SLOT_DATA) {
            u32 h = get32(d + q);
            if (!h)
                return hit;                              /* a whole block: this is it */
            if (q + 6 > SAVE_SLOT_DATA || q + 6 + (n = get16(d + q + 4)) > SAVE_SLOT_DATA)
                break;
            if (h == hash) {
                hit = d + q + 6;
                *len = n;
            }
            q += 6 + n;
        }
    }
    return 0;
}

static int slot_sum_ok(const u8 *d)
{
    u32 sum = 0, any = 0;
    for (int i = 0; i < SLOT_SIZE; i += 2) {
        sum += get16(d + i);
        any |= i < SAVE_SLOT_DATA ? d[i] | d[i + 1] : 0;
    }
    return (sum & 0xFFFF) == 0 && any;
}

/* Read both slots from the cartridge: the newest save's choice, and each slot's save number. */
static void scan(void)
{
    u32 hash = my_hash(), best = 0;
    quick_start.scans++;
    for (int s = 0; s < 2; s++) {
        quick_start.stamp[s] = 0;
        quick_start.valid[s] = 0;
        if (!eeprom_read(SLOT_ADDR(s), SLOT_SIZE, slot_buf) || !slot_sum_ok(slot_buf))
            continue;
        quick_start.valid[s] = 1;
        if (!hash)
            continue;
        int len = 0;
        const u8 *d = find_ours(slot_buf, hash, &len);
        if (!d || len < 4)
            continue;
        u32 st = get16(d + 2);
        quick_start.stamp[s] = st;
        if (st > best) {
            best = st;
            if (d[0] < N_CHOICES) {
                quick_start.choice = d[0];
                quick_start.from_save = 1;
            }
        }
    }
    if (best + 1 > quick_start.next)
        quick_start.next = best + 1;
}

void mod_on_boot(void) { scan(); }

void mod_on_enable(void)                                 /* switched on in the city: the stream plays from the card */
{
    sound_card_pause();
    scan();
    sound_card_resume();
}

int mod_on_save(u8 *buf, int max)
{
    if (max < 4)
        return -1;
    u32 st = quick_start.next ? quick_start.next : 1;
    buf[0] = quick_start.choice;
    buf[1] = 0;
    buf[2] = st;
    buf[3] = st >> 8;
    quick_start.next = st + 1;
    return 4;
}

void mod_on_load(const u8 *buf, int len)
{
    if (buf && len >= 4 && get16(buf + 2) + 1 > quick_start.next)
        quick_start.next = get16(buf + 2) + 1;           /* the choice stays: it comes from the newest save */
}

/* (save_slot_info, which Load-an-Urb checks, is only filled once the title runs: 0 at this point.) */
static int slot_has_game(int s) { return quick_start.valid[s]; }

static int pick_slot(void)
{
    switch (quick_start.choice) {
    case SLOT1: return 0;
    case SLOT2: return 1;
    case TITLE: return -1;
    }
    if (quick_start.stamp[0] != quick_start.stamp[1])   /* last saved: the higher save number */
        return quick_start.stamp[1] > quick_start.stamp[0];
    if (slot_has_game(0) != slot_has_game(1))           /* none of ours: the only game there is */
        return slot_has_game(1);
    return -1;                                           /* both or neither: let the player choose */
}

/* What Load-an-Urb's confirm does (load_confirm 0x0204781C). Load-an-Urb runs on screen 1, so this runs in
   screen 1's turn too, and makes the same calls: the city (load mode) on screen 0, its menus on screen 1. */
static int load_slot(int s)
{
    game_reset();                                        /* the title does this when it starts (state 0x21's enter): */
                                                         /* without it the save's object lists have no buffer yet */
    save_card_busy = 1;                                  /* (Load-an-Urb also pauses the music stream around the */
    save_slot_number = s;                                /* read; nothing streams yet at the logos, and its resume */
    save_read_slot(s);                                   /* would clear 8 KB at a pointer that is still 0) */
    save_card_busy = 0;
    if (!save_slot_valid(s))
        return 0;
    for (int i = 0; i < 2; i++) {                        /* the logos left both screens dark (level 16); */
        screen_fade(0x3F, 0);                            /* Load-an-Urb runs with level 0 */
        screen_level(0x3F, 0);
        cur_screen ^= 1;
    }
    load_transition = 1;
    set_state_other(ST_CITY, MODE_LOAD, 0);
    set_state_here(ST_CITY_MENU, 0, 0);
    return 1;
}

/* What the logos do when they end (stage 0xD of state_logos_update), in screen 0's turn. */
static void to_title(void)
{
    screen_fade(0x3F, 0);
    set_state_here(ST_TITLE_TOP, 0, 0);
    cur_screen ^= 1;
    screen_fade(0x3F, 0);
    set_state_here(ST_TITLE, 0, 0);
    cur_screen ^= 1;
}

/* State 3's update (hooks.txt points the state table here); it runs for each screen every frame. */
void quick_start_update(void)
{
    static int calls;
    static int want = -1;
    if (quick_start.did)                                 /* done: the new states start next frame */
        return;
    if (cur_screen == 1) {
        if (want < 0)
            return;
        if (load_slot(want)) {
            quick_start.did = 1;
            quick_start.slot = want;
        } else {
            quick_start.did = 4;
            cur_screen ^= 1;
            to_title();
            cur_screen ^= 1;
        }
        return;
    }
    if (want >= 0 || ++calls < 3 || fade_busy())         /* both screens' logos enter started a fade-in: let it */
        return;                                          /* end, as the logos do (a state change mid-fade hangs) */
    if (!(REG_KEYINPUT & KEYIN_SELECT)) {
        quick_start.did = 3;
        to_title();
        return;
    }
    int s = pick_slot();
    if (s >= 0 && slot_has_game(s)) {
        want = s;                                        /* loaded in screen 1's turn */
        return;
    }
    quick_start.did = 2;
    to_title();
}

static char *put(char *d, const char *s)
{
    while ((*d = *s++))
        d++;
    return d;
}

void mod_on_page(mod_page_t *p)
{
    int c = quick_start.choice;
    if (p->keys_down & KEY_R)
        c = (c + 1) % N_CHOICES;
    if (p->keys_down & KEY_L)
        c = (c + N_CHOICES - 1) % N_CHOICES;
    quick_start.choice = c;
    char line[32];
    put(put(line, "Start in: "), choice_names[c]);
    p->print(p, line);
    p->print(p, "R / L to change.");
    p->print(p, "Kept when you save the game.");
    p->print(p, "Hold SELECT at power-on");
    p->print(p, "for the title screen.");
}
