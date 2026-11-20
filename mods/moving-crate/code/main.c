/* Moving Crate: the crate you get when you rent a home stays, can be moved, and can hold your things.

   The original game (docs/systems.md "The moving crate"):
   - move_home puts the old home's placed things into item list 21 (the crate, 50 slots, game_state+0x158) and
     sets the "crate" flag (game_state+0x111). The crate is object 230, made from an area record in each home's
     main area while crate_here (0x02046948) says so: you are in that area and the flag is set.
   - Its menu: Open the Crate (0x0F) always, Return the Crate (0x10) only when the crate is empty. Open shows the
     shop screen (state 17) on list 21: Pockets on the left, the crate on the right; double-tap a crate item to
     take it. Return clears the flag and deletes the crate for good.

   This mod:
   - Action 0x10 is always offered and is now Move the Crate (text 3624): the crate goes into Pockets (it gets the
     Soap Box's icon there) and comes out like any furniture (double-tap it, walk, A). Placed that way it is a
     normal placed object (object 230, saved by the game) with the same menu.
   - The crate from the area record is made whenever you are in your home's main area and the crate is nowhere
     else (not in Pockets, not placed anywhere): so it can't be lost, and returning it is gone.
   - On the crate screen a double-tap on a Pockets item puts it in the crate (refused when the crate is full).
   - The crate never goes inside itself (move_home packs a placed crate into list 21 with the rest: it is taken
     out again before the new home's crate is made). */
#include "game.h"
#include "mod.h"

#define CRATE       230
#define CRATE_ICON  97                                  /* the Soap Box's icon (object 78) */
#define GS          ((u8 *)ADDR_game_state)
#define SLOTS(l)    (*(u16 **)((l) + 4))                /* item list {u8 count, u8 capacity, u16, slots *}, 6-byte slots */

#define list_add     GAME_FN(ADDR_list_add, int (*)(u8 *list, unsigned obj, unsigned a, unsigned variant))
#define list_find    GAME_FN(0x0203D08C, int (*)(u8 *list, unsigned obj))
#define list_remove  GAME_FN(0x0203CFB0, void (*)(u8 *list, int index, int shift))
#define shop_sell    GAME_FN(0x0209CC50, int (*)(void))
#define store_remove GAME_FN(0x0203C82C, void (*)(u8 *objects, int area, int index))
#define set_state    GAME_FN(ADDR_entity_set_state, void (*)(void *e, int state))
#define ui_sound     GAME_FN(ADDR_ui_sound, void (*)(int id))
#define message_box  GAME_FN(ADDR_message_box, void (*)(int icon, int string))

#define STR_POCKETS_FULL 393                            /* "Your Pockets are full!" */

struct { u32 magic, moved, put_in, refused, spawned, kept_away, unpacked; } moving_crate = { 0x54415243 };  /* 'CRAT' */

static u8 *pockets(void)    { return *(u8 **)(GS + 0x154); }
static u8 *crate_list(void) { return *(u8 **)(GS + 0x158); }
static u8 *store(void)      { return *(u8 **)ADDR_placed_store_ptr; }

/* Is the crate placed in any area? Other areas' placed things are in the placed-objects store (u16 total, then
   10-byte records {u16 object, ...}); the area on screen has its own in its object list (seen 2026-11-20: after
   a load at home the store was empty and the placed crate a node of object_list_head), so look at both. */
static int crate_placed(void)
{
    for (u8 *n = *(u8 **)ADDR_object_list_head; n; n = *(u8 **)n)
        if (*(u16 *)(n + 8) == CRATE)
            return 1;
    u8 *s = store();
    if (!s)
        return 0;
    int total = *(u16 *)s;
    for (int i = 0; i < total; i++)
        if (*(u16 *)(s + 2 + i * 10) == CRATE)
            return 1;
    return 0;
}

/* Menu: offer action 0x10 (Move the Crate) every time; the game asked "is the crate empty?". */
int crate_offer_move(u8 *list)
{
    (void)list;
    return 1;
}

/* Move the Crate (replaces crate_return). node = the crate's object node, entity at +0x24. */
int crate_move(u8 *node)
{
    u8 *p = pockets();
    GS[0xA6E] = 0xFF;                                   /* as crate_return does */
    if (p[0] >= p[1]) {
        moving_crate.refused++;
        message_box(CRATE_ICON, STR_POCKETS_FULL);
        return 2;
    }
    list_add(p, CRATE, 0, 0);
    /* A placed crate also has a record in the placed-objects store: take it out (the area record's crate has
       none). Records of an area: first index at +0x642 + 4*area, count at +0x643 + 4*area. */
    u8 *s = store();
    int area = current_area;
    if (s && area < 82) {
        int first = s[0x642 + 4 * area], n = s[0x643 + 4 * area];
        for (int k = 0; k < n; k++)
            if (*(u16 *)(s + 2 + (first + k) * 10) == CRATE) {
                store_remove(GS + 0x138, area, k);
                break;
            }
    }
    set_state(*(void **)(node + 0x24), 1);
    moving_crate.moved++;
    return 2;
}

/* Should the area record's crate be made? (replaces crate_here: "in the home's main area and the flag set") */
int crate_spawn_check(void)
{
    u8 *c = crate_list();
    int i;
    while ((i = list_find(c, CRATE)) >= 0) {           /* packed by move_home: never a crate in the crate */
        list_remove(c, i, 1);
        moving_crate.unpacked++;
    }
    if (current_area != *(u8 *)(ADDR_home_lots + GS[0x110] * 8))
        return 0;
    if (list_find(pockets(), CRATE) >= 0 || crate_placed()) {
        moving_crate.kept_away++;
        return 0;
    }
    GS[0x111] = 1;                                      /* the game's own flag, for anything else that reads it */
    moving_crate.spawned++;
    return 1;
}

/* The crate screen's double-tap on a Pockets item (state 17 update, shop_sell's call). For list 21 shop_sell
   moves the item into the crate with no price, but it takes it out of Pockets even when the crate is full. */
int crate_put_in(void)
{
    u8 *ui = (u8 *)0x02148F30;                          /* shop_ui: +0 Pockets slot chosen, +4 the list shown */
    if (ui[4] != 21)
        return shop_sell();
    u8 *p = pockets();
    int idx = ui[0] + *(int *)(0x027C0078 + *(int *)0x027C0004 * 0x8C);   /* as shop_sell finds it */
    u16 obj = *(u16 *)((u8 *)SLOTS(p) + idx * 6);
    u8 *c = crate_list();
    if (obj == 0x184)
        return 0;                                       /* an empty slot: nothing happens, as in the game */
    if (obj == CRATE || c[0] >= c[1]) {
        ui_sound(3);
        moving_crate.refused++;
        return 0;
    }
    moving_crate.put_in++;
    return shop_sell();
}

/* place_object_check at 0x0203F804 (r6 = object): < 0xE0 goes on to the free-spot checks (0x0203F814), and so
   do 0xE1 (the Chicken, as before) and the crate; anything else 0xE0+ is refused (0x0203F830). */
__attribute__((naked)) void crate_place_stub(void)
{
    __asm__ volatile(
        "cmp r6, #0xE0\n"
        "ldrlt pc, =0x0203F814\n"
        "cmp r6, #0xE1\n"
        "cmpne r6, #0xE6\n"
        "ldreq pc, =0x0203F814\n"
        "ldr pc, =0x0203F830\n"
        ".ltorg\n");
}

/* Pockets and the crate screen show an object by its model's icon; the crate's is 632 (none). The text table
   can be moved by the objects kit: go through the literal that points at it. */
void mod_on_boot(void)
{
    u8 *table = *(u8 **)ADDR_obj_text_lit;
    *(u32 *)(table + CRATE * 20) = CRATE_ICON;
}
