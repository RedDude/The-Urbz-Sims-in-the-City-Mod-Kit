"""Which townspeople stand in a few public places after a city boot (friendly-topics probe, step 1)."""
import json, os, sys, tempfile
KIT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path[:0] = [os.path.join(KIT, 'tests'), os.path.join(KIT, 'verify'), os.path.join(KIT, 'kit')]
import proofs as P, urbz_verify as V
rom = P.build('friendly-topics', [os.path.join(KIT, 'mods', 'friendly-topics')])[0]
state = V.city_state(os.path.abspath(rom))
areas = json.load(open(V.AREAS_JSON))['areas']
for a in [int(x) for x in sys.argv[1:]]:
    pts = areas[a]['entry_points'] if a < len(areas) else []
    job = {'rom': os.path.abspath(rom), 'out': tempfile.mkdtemp(prefix='urbz-chat-'), 'tag': 'snap', 'poke': [],
           'state': state, 'goto': (a, pts[0]['id'] if pts else 0), 'script': [['wait', 120], ['shot', 'x']],
           'snap': [P.MEM_HI]}
    res = V.run_child(job, timeout=600)
    w = P._world_full(open(res['snaps'][0][1][0], 'rb').read())
    print(a, areas[a].get('name'), [(e[1], e[2], e[3], e[4]) for e in w if e[1] in (0, 7)], res['snaps'][0][0] if 0 else '')
