/* Bug Fixes: bugs of the original game, fixed. Each fix says what goes wrong, how it was found and which proof
   in tests/proofs.py shows it (the bug in the original ROM, and gone with this mod). List: ../README.md.
   Switched off on the Mods page, the game behaves as the original again. */
#include "game.h"
#include "mod.h"

#define E8(e, o)  (*(u8 *)((e) + (o)))
#define E16(e, o) (*(u16 *)((e) + (o)))
#define E32(e, o) (*(u32 *)((e) + (o)))
#define path_cancel GAME_FN(ADDR_path_cancel, void (*)(void *e))
#define walked_out  GAME_FN(ADDR_npc_walked_out, void (*)(void *e))

struct { u32 magic, ticks, let_out, trash_back, trash_credit; } bug_fixes = { 0x58494642, 0, 0, 0, 0 };   /* 'BFIX' (tests read this) */

static u8 *world_first(void) { return *(u8 **)ADDR_entity_lists; }

/* ---- Fix 1: people who can't get out (proof fix-stuck-leaving) ----------------------------------------------
   The game sends someone out of the area by walking them to a way point (a door or lift; npc_walk_out sets entity
   +0x107 bit 1 and the target at +0x38). When that walk fails while they're on screen (you, or someone, standing
   on the spot), the game puts them back to wandering and tries again at its next relocation, every 5 s, for ever
   (off screen it just lets them go). In Waters' Gym, Sue Pirnova paced by the lift from 6:00 to past 8:30.
   Here: after 20 s of trying, someone within 64 px of their way point goes through it as if they had arrived;
   after 60 s, wherever they are. Only people wandering on their own (+0x104 = 0x23 with action 2, 7 or 0x19):
   not someone talking to you, in a story scene, or doing something for another mod. */
#define PEOPLE     49                           /* ids 31..79 */
#define LEAVE_NEAR (64 * 64)
static u32 leave_since[PEOPLE], leave_seen[PEOPLE];

static void let_stuck_people_out(u32 now)
{
    for (u8 *e = world_first(); e; e = *(u8 **)e) {
        int i = E16(e, 10) - 31;
        if (E16(e, 8) != 7 || i < 0 || i >= PEOPLE)
            continue;
        if (E8(e, 0x107) & 2) {                       /* the bit goes off after each failed walk: count from the first */
            if (!leave_since[i] || now - leave_seen[i] > 600)
                leave_since[i] = now;
            leave_seen[i] = now;
        } else if (!leave_since[i] || now - leave_seen[i] > 600) {
            leave_since[i] = 0;                       /* not asked again for 20 s: they stay after all */
            continue;
        }
        u32 t = now - leave_since[i];
        int a = E8(e, 0x105);
        if (t < 600 || E8(e, 0x104) != 0x23 || (a != 2 && a != 7 && a != 0x19) || !E32(e, 0x38))
            continue;
        int dx = ((s32)E32(e, 0x18) - (s32)E32(e, 0x38)) >> 16, dy = ((s32)E32(e, 0x1C) - (s32)E32(e, 0x3C)) >> 16;
        if (dx * dx + dy * dy > LEAVE_NEAR && t < 1800)
            continue;
        path_cancel(e);                               /* what the game does when they reach the way point */
        entity_set_state(e, 1);
        walked_out(e);
        leave_since[i] = 0;
        bug_fixes.let_out++;
    }
}

void mod_on_area_enter(int area)
{
    (void)area;
    for (int i = 0; i < PEOPLE; i++)                  /* new area, new people */
        leave_since[i] = 0;
}

/* ---- Fix 2: trash that stops coming back (proofs fix-trash, fix-trash-sold) -------------------------------------
   Piles of trash (collectible kind 0) appear from area records of type 16. Every pile you pick up adds 1 to
   trash_picked (saved with the game), and once it is over 100 the game never makes trash again. But the Artsies'
   "Recycle 100 piles of trash" only counts trash sold in a shop *after* they give you that challenge
   (artsie_trash_sold, in shop_input). Trash picked up before then uses up the town's 101 piles, so the goal can't
   be finished: players see the trash vanish for good, sooner the more of it they pick up.
   Two ways to fix it, chosen with tune_trash (settings.json "trash", the mod manager's Tune window):
   1 "count-sold" (default): the goal also counts trash sold before the challenge. The game doesn't keep that number, but it
     keeps trash_picked and how many you hold, so sold = picked - held (trash that left your Pockets any other way
     counts too). While the challenge is on, the goal's count is raised to that; at 100 the sub-goal is done, the
     way a sale would do it. Trash still stops after 101 piles, as in the original.
   0 "keep-coming": until that sub-goal (mission 5, goal 2, sub-goal 2) is done, trash_picked stays at 100 at most,
     so trash keeps coming; afterwards the game is as it was.
   Either way a save that already has the bug is mended. */
const volatile u8 tune_trash = 1;                     /* the build writes the chosen setting here (urbz_tuning.py) */

#define TRASH_GOAL       goal_row(5, 2)
#define TRASH_GOAL_SHOWN (TRASH_GOAL[2 + 2 * 2])
#define TRASH_GOAL_DONE  (TRASH_GOAL[3 + 2 * 2])
#define subgoal_set_done GAME_FN(ADDR_subgoal_set_done, void (*)(int m, int g, int s))

static void keep_trash_coming(void)
{
    if (trash_picked > 100 && !TRASH_GOAL_DONE) {
        trash_picked = 100;
        bug_fixes.trash_back++;
    }
}

static void count_trash_sold_before(void)
{
    if (!TRASH_GOAL_SHOWN || TRASH_GOAL_DONE)          /* the Artsies haven't asked yet, or it's done */
        return;
    int sold = (int)trash_picked - (int)collectible_count[0];
    if (sold <= (int)artsie_trash_sold)
        return;
    artsie_trash_sold = sold > 255 ? 255 : sold;
    bug_fixes.trash_credit++;
    if (sold >= 100)
        subgoal_set_done(5, 2, 2);
}

/* The Goals screen draws each sub-goal line from text_get(goal's title string + s + 1) (the BL at 0x02036860,
   goals_line_text in hooks.txt). For the Artsies' trash line (string 4092) we add the game's own count while the
   challenge is on and under 100: "Clean up the city! Recycle 100 piles of trash (42)". */
#define TRASH_GOAL_TEXT 4092
static char trash_line[160];

static char *put_num(char *p, unsigned n)
{
    char d[4];
    int k = 0;
    do d[k++] = '0' + n % 10; while ((n /= 10) && k < 4);
    while (k)
        *p++ = d[--k];
    return p;
}

const char *goal_line_text(int id)
{
    const char *s = text_get(id);
    unsigned n = artsie_trash_sold;
    if (id != TRASH_GOAL_TEXT || !TRASH_GOAL_SHOWN || TRASH_GOAL_DONE || n >= 100)
        return s;
    char *p = trash_line, *end = trash_line + sizeof trash_line - 12;
    while (*s && p < end)
        *p++ = *s++;
    *p++ = ' ';
    *p++ = '(';
    p = put_num(p, n);
    *p++ = ')';
    *p = 0;
    return trash_line;
}

void mod_on_tick(void)
{
    u32 now = ++bug_fixes.ticks;
    if (tune_trash == 1)
        count_trash_sold_before();
    else
        keep_trash_coming();                          /* every tick: a place loaded right after a pickup still has trash */
    if (now & 15)
        return;
    let_stuck_people_out(now);
}
