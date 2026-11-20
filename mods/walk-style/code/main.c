/* Walk Style: pick the player's walk on the mod's page (Options > Mods > Walk Style): tap a Rep group's
   picture, or R / L through every choice.

   The game has five player walks: 0x0A (the normal one) and one per Rep group (0x0B-0x0E). walk_pick
   (0x02037860) turns a request for 0x0A into your group's walk in some places (docs/systems.md "The player's
   walk"). We answer its two calls: "Game's own" leaves its answer alone, the others give the chosen walk
   wherever the game would walk (not when it has its own reason to keep the plain one: player_walk_busy). */
#include "game.h"
#include "mod.h"

#define walk_pick        GAME_FN(ADDR_walk_pick, int (*)(int anim))
#define player_walk_busy GAME_FN(ADDR_player_walk_busy, int (*)(int anim))
#define WALK 0x0A

static const struct { u8 anim; u16 icon; const char *name; } styles[] = {
    { 0,    0,   "Game's own" },   /* your Rep group's, where the game uses it */
    { 0x0A, 0,   "Normal" },
    { 0x0E, 373, "Streetie" },     /* icon: the group's picture (ui_sprite_table row, as on the S goals) */
    { 0x0C, 378, "Nerdie" },
    { 0x0D, 377, "Richie" },
    { 0x0B, 396, "Artsie" },
};
#define N_STYLES (sizeof styles / sizeof styles[0])

struct { u32 magic; u8 style, pad[3]; u32 picks, changed; } walk_style = { 0x4B4C4157, 0 };   /* 'WALK' (tests) */

int walk_style_pick(int anim)
{
    int r = walk_pick(anim);
    u8 want = styles[walk_style.style].anim;
    if (anim == WALK && want && !player_walk_busy(0)) {
        r = want;
        walk_style.changed++;
    }
    walk_style.picks++;
    return r;
}

static char *put(char *d, const char *s)
{
    while ((*d = *s++))
        d++;
    return d;
}

#define FIRST_GROUP 2            /* styles[2..5]: the four pictures, in this order */
#define ICON_Y      32
#define ICON_X0     40           /* "Streeties" stays right of x 16 (what the game clears) */
#define ICON_DX     46
#define GROUP_NAMES 829          /* the game's text: Streeties, Nerdies, Richies, Artsies */

void mod_on_page(mod_page_t *p)
{
    int s = walk_style.style;
    if (p->tapped >= 0)                  /* a group's picture: that walk; the chosen one again: the game's own */
        s = s == FIRST_GROUP + p->tapped ? 0 : FIRST_GROUP + p->tapped;
    if (p->keys_down & KEY_R)
        s = (s + 1) % N_STYLES;
    if (p->keys_down & KEY_L)
        s = (s + N_STYLES - 1) % N_STYLES;
    walk_style.style = s;
    char line[32];
    put(put(line, "Walk: "), styles[s].name);
    p->print(p, line);
    p->selected = s >= FIRST_GROUP ? s - FIRST_GROUP : -1;   /* the frame on the chosen group, none otherwise */
    for (int i = 0; i < 4; i++)
        p->icon(p, styles[FIRST_GROUP + i].icon, ICON_X0 + ICON_DX * i, ICON_Y, text_get(GROUP_NAMES + i));
}

int mod_on_save(u8 *buf, int max)
{
    if (max < 1)
        return -1;
    buf[0] = walk_style.style;
    return 1;
}

void mod_on_load(const u8 *buf, int len)
{
    walk_style.style = (buf && len >= 1 && buf[0] < N_STYLES) ? buf[0] : 0;
}
