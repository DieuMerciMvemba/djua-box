#!/usr/bin/env python3
"""
xt90i_gen.py - Generateur parametrique de boitiers imprimables XT90I (AMASS) male / femelle
+ pieces de test d'ajustement.  Dependances : numpy, scipy.

Usage :  python3 xt90i_gen.py [--cav-comp 0.10] [--plug-comp 0.10] [--outdir out]

Repere de la piece : x = largeur, y = hauteur (y=0 = face plate du bas),
z = axe d'emboitement (z=0 = face d'accouplement, le corps s'etend vers +z).
Les STL des boitiers sont exportes COUCHES sur la face plate (voir README).

SOURCE DES COTES
  [PLAN]  = lu sur les plans cotes AMASS XT90I fournis (valeur imprimee sur le plan)
  [EST]   = estime sur l'image du plan (non cote) -> a verifier avec la piece de test
  [PARAM] = depend de TES contacts metalliques -> a regler
"""
import argparse, math, os, struct
from collections import Counter
import numpy as np
from scipy.spatial import Delaunay

# ----------------------------------------------------------------------------
# PARAMETRES (mm)
# ----------------------------------------------------------------------------
P = dict(
    # --- interface d'emboitement ---
    CAV_W=8.70, CAV_H=19.60,          # [PLAN] cavite interne du male (largeur x hauteur)
    PLUG_W=8.40, PLUG_H=19.30,        # [PLAN] fiche de la femelle
    PITCH_M=11.00, PITCH_F=10.90,     # [PLAN] entraxe contacts de puissance (male / femelle)
    SIG_PITCH=4.00,                   # [PLAN] entraxe des 2 broches de signal (a mi-hauteur)
    OUT_H=22.56,                      # [PLAN] hauteur exterieure du corps
    MALE_LEN=30.70, FEM_LEN=29.70,    # [PLAN] longueurs totales
    PLUG_LEN=11.70,                   # [PLAN] longueur de la fiche femelle
    OUT_W=11.40,                      # [EST] largeur exterieure (non cotee)
    CAV_DEPTH=13.0,                   # [EST] profondeur de la cavite male
    CHX=2.30, CHY=2.60,               # [EST] chanfreins du detrompeur (coins du haut)
    CAV_RB=0.8, PLUG_RB=0.8,          # [EST] rayon des coins du bas
    OUT_RB=1.2, OUT_RT=5.2,           # [EST] rayons exterieurs bas / haut (voute)
    YC=11.03,                         # [EST] hauteur de l'axe des contacts depuis la face plate
    YC_CAV=11.03, YC_PLUG=11.03,
    SAFE=0.30,                        # marge de securite sur les chanfreins estimes
    # --- compensation d'impression (par cote) ---
    CAV_COMP=0.10, PLUG_COMP=0.10,
    LEADIN=0.30, LEADIN_LEN=0.5,      # entree en escalier pour faciliter l'emboitement
    # --- contacts metalliques [PARAM] ---
    M_POWER_D=4.60,                   # trou pour bullet male 4.5 mm (+0.1)
    F_POWER_D=5.00,                   # trou pour contact femelle (a regler)
    SIG_D=1.50,                       # trou broches de signal
    FLOOR_T=6.0,                      # epaisseur du plancher male (retient les contacts)
    # --- arriere (zone de soudure) ---
    REAR_WALL=1.4, REAR_WALL_T=1.4, REAR_WALL_B=1.3,
    SIGCH_HALF=2.2, PLATE_T=1.0,      # canal de signal et cloisons d'isolation
    SEG=0.15,                         # pas de discretisation des contours
)

# ----------------------------------------------------------------------------
# Profils 2D
# ----------------------------------------------------------------------------
def arc_pts(cx, cy, r, a0, a1, n=12):
    return [(cx + r*math.cos(math.radians(a)), cy + r*math.sin(math.radians(a)))
            for a in np.linspace(a0, a1, n)]

def rounded_profile(xl, xr, yb, yt, rb, rt, n=12):
    pts = []
    pts += arc_pts(xr-rb, yb+rb, rb, -90, 0, n) if rb > 0 else [(xr, yb)]
    pts += arc_pts(xr-rt, yt-rt, rt, 0, 90, n) if rt > 0 else [(xr, yt)]
    pts += arc_pts(xl+rt, yt-rt, rt, 90, 180, n) if rt > 0 else [(xl, yt)]
    pts += arc_pts(xl+rb, yb+rb, rb, 180, 270, n) if rb > 0 else [(xl, yb)]
    return np.array(pts)

def chamfer_profile(xl, xr, yb, yt, rb, chx, chy, n=10):
    pts = []
    pts += arc_pts(xr-rb, yb+rb, rb, -90, 0, n)
    pts += [(xr, yt-chy), (xr-chx, yt), (xl+chx, yt), (xl, yt-chy)]
    pts += arc_pts(xl+rb, yb+rb, rb, 180, 270, n)
    return np.array(pts)

def circle(cx, cy, r, n=48):
    a = np.linspace(0, 2*math.pi, n, endpoint=False)
    return np.stack([cx + r*np.cos(a), cy + r*np.sin(a)], 1)

def rect(xl, xr, yb, yt):
    return np.array([(xl, yb), (xr, yb), (xr, yt), (xl, yt)], float)

def outer_poly(P):
    return rounded_profile(-P['OUT_W']/2, P['OUT_W']/2, 0, P['OUT_H'], P['OUT_RB'], P['OUT_RT'])

def cavity_poly(P, c):                       # cavite du male imprime
    W = P['CAV_W'] + 2*c; H = P['CAV_H'] + 2*c; yc = P['YC_CAV']
    return chamfer_profile(-W/2, W/2, yc-H/2, yc+H/2, P['CAV_RB']+c,
                           P['CHX']-P['SAFE'], P['CHY']-P['SAFE'])

def plug_poly(P, c):                         # fiche de la femelle imprimee
    W = P['PLUG_W'] - 2*c; H = P['PLUG_H'] - 2*c; yc = P['YC_PLUG']
    return chamfer_profile(-W/2, W/2, yc-H/2, yc+H/2, max(P['PLUG_RB']-c, 0.2),
                           P['CHX']+P['SAFE'], P['CHY']+P['SAFE'])

def real_cavity(P):                          # cavite du vrai male (plan + chanfreins estimes)
    return chamfer_profile(-P['CAV_W']/2, P['CAV_W']/2, P['YC_CAV']-P['CAV_H']/2,
                           P['YC_CAV']+P['CAV_H']/2, P['CAV_RB'], P['CHX'], P['CHY'])

def real_plug(P):                            # fiche du vrai femelle
    return chamfer_profile(-P['PLUG_W']/2, P['PLUG_W']/2, P['YC_PLUG']-P['PLUG_H']/2,
                           P['YC_PLUG']+P['PLUG_H']/2, P['PLUG_RB'], P['CHX'], P['CHY'])

def holes(P, pitch, dpow):
    yc = P['YC']; h = {}
    h['hp1'] = circle(0, yc+pitch/2, dpow/2); h['hp2'] = circle(0, yc-pitch/2, dpow/2)
    h['hs1'] = circle(-P['SIG_PITCH']/2, yc, P['SIG_D']/2, 24)
    h['hs2'] = circle(+P['SIG_PITCH']/2, yc, P['SIG_D']/2, 24)
    return h

def channels(P):
    w = P['OUT_W']/2 - P['REAR_WALL']; yt = P['OUT_H'] - P['REAR_WALL_T']
    yb = P['REAR_WALL_B']; yc = P['YC']; g = P['SIGCH_HALF']; t = P['PLATE_T']
    return dict(
        ch_top=rounded_profile(-w, w, yc+g+t, yt, 0, max(P['OUT_RT']-P['REAR_WALL_T'], 0.5)),
        ch_mid=rect(-w, w, yc-g, yc+g),
        ch_bot=rounded_profile(-w, w, yb, yc-g-t, max(P['OUT_RB']-P['REAR_WALL_B'], 0.4), 0))

# ----------------------------------------------------------------------------
# Maillage : triangulation 2D conforme + cellules prismatiques (2.5D)
# ----------------------------------------------------------------------------
def densify(poly, h):
    out = []
    n = len(poly)
    for i in range(n):
        a = poly[i]; b = poly[(i+1) % n]
        k = max(1, int(math.ceil(np.hypot(*(b-a))/h)))
        for s in range(k):
            out.append(a + (b-a)*s/k)
    return np.array(out)

def node_contours(contours):
    """Insert every crossing between different polygon contours as a shared vertex.

    The profiles can cross in XY while being used at different depths. Noding the
    crossings lets the common triangulation represent each profile exactly.
    """
    names = list(contours)
    additions = {name: [[] for _ in contours[name]] for name in names}
    endpoint_tol = 1e-9

    for i, name_a in enumerate(names):
        a = contours[name_a]
        a0 = a
        ar = np.roll(a, -1, axis=0) - a
        amin = np.minimum(a0, a0 + ar)
        amax = np.maximum(a0, a0 + ar)

        for name_b in names[i+1:]:
            b = contours[name_b]
            b0 = b
            br = np.roll(b, -1, axis=0) - b
            bmin = np.minimum(b0, b0 + br)
            bmax = np.maximum(b0, b0 + br)
            if np.any(amax.max(axis=0) < bmin.min(axis=0) - 1e-10) or \
               np.any(bmax.max(axis=0) < amin.min(axis=0) - 1e-10):
                continue

            # Densified edges are short, so these pairwise arrays stay small.
            box = np.all((amin[:, None, :] <= bmax[None, :, :] + 1e-10) &
                         (bmin[None, :, :] <= amax[:, None, :] + 1e-10), axis=2)
            den = ar[:, None, 0]*br[None, :, 1] - ar[:, None, 1]*br[None, :, 0]
            qmp = b0[None, :, :] - a0[:, None, :]
            safe = box & (np.abs(den) > 1e-12)
            t = np.full(den.shape, np.nan)
            u = np.full(den.shape, np.nan)
            np.divide(qmp[:, :, 0]*br[None, :, 1] - qmp[:, :, 1]*br[None, :, 0],
                      den, out=t, where=safe)
            np.divide(qmp[:, :, 0]*ar[:, None, 1] - qmp[:, :, 1]*ar[:, None, 0],
                      den, out=u, where=safe)
            crosses = safe & (t >= -endpoint_tol) & (t <= 1+endpoint_tol) & \
                      (u >= -endpoint_tol) & (u <= 1+endpoint_tol)

            for ia, ib in zip(*np.nonzero(crosses)):
                ta = min(1.0, max(0.0, float(t[ia, ib])))
                tb = min(1.0, max(0.0, float(u[ia, ib])))
                if tb <= endpoint_tol:
                    point = b0[ib]
                elif tb >= 1-endpoint_tol:
                    point = b0[(ib+1) % len(b0)]
                elif ta <= endpoint_tol:
                    point = a0[ia]
                elif ta >= 1-endpoint_tol:
                    point = a0[(ia+1) % len(a0)]
                else:
                    point = a0[ia] + ta*ar[ia]

                if endpoint_tol < ta < 1-endpoint_tol:
                    additions[name_a][ia].append((ta, point))
                if endpoint_tol < tb < 1-endpoint_tol:
                    additions[name_b][ib].append((tb, point))

    noded = {}
    for name, poly in contours.items():
        out = []
        for i, point in enumerate(poly):
            out.append(point)
            inserts = sorted(additions[name][i], key=lambda item: item[0])
            for _, crossing in inserts:
                if np.linalg.norm(np.asarray(crossing) - out[-1]) > 1e-8:
                    out.append(crossing)
        noded[name] = np.asarray(out)
    return noded

def eo_inside(pts, polys):
    """regle pair/impair : pts (N,2), polys liste de (M,2)"""
    cnt = np.zeros(len(pts), int)
    x = pts[:, 0][:, None]; y = pts[:, 1][:, None]
    for poly in polys:
        a = poly; b = np.roll(poly, -1, axis=0)
        x1 = a[:, 0][None, :]; y1 = a[:, 1][None, :]
        x2 = b[:, 0][None, :]; y2 = b[:, 1][None, :]
        cond = (y1 > y) != (y2 > y)
        with np.errstate(divide='ignore', invalid='ignore'):
            xi = x1 + (y - y1)*(x2 - x1)/(y2 - y1)
        cnt += np.sum(cond & (x < xi), axis=1)
    return (cnt % 2) == 1

def build_mesh(polys, slabs, seg):
    """polys: dict nom->(M,2). slabs: liste (z0,z1,[noms]) contigus. Retourne (V,F)."""
    vmap = {}; vlist = []
    def vid(p):
        k = (round(float(p[0]), 5), round(float(p[1]), 5))
        if k not in vmap:
            vmap[k] = len(vlist); vlist.append(k)
        return vmap[k]
    idx = {}
    contours = node_contours({name: densify(poly, seg) for name, poly in polys.items()})
    for name, d in contours.items():
        seq = []
        for p in d:
            i = vid(p)
            if not seq or seq[-1] != i:
                seq.append(i)
        if seq[0] == seq[-1]:
            seq.pop()
        idx[name] = seq
    for it in range(10):                      # raffinement jusqu'a conformite
        pts = np.array(vlist)
        tri = Delaunay(pts)
        edges = set()
        for s in tri.simplices:
            for a, b in ((s[0], s[1]), (s[1], s[2]), (s[2], s[0])):
                edges.add((min(a, b), max(a, b)))
        missing = 0
        for name, seq in idx.items():
            new = []
            for i in range(len(seq)):
                a = seq[i]; b = seq[(i+1) % len(seq)]
                new.append(a)
                if (min(a, b), max(a, b)) not in edges:
                    missing += 1
                    m = (np.array(vlist[a]) + np.array(vlist[b]))/2
                    new.append(vid(m))
            idx[name] = new
        if missing == 0:
            break
    else:
        raise RuntimeError("triangulation non conforme apres 10 raffinements")
    pts = np.array(vlist)
    tri = Delaunay(pts)
    T = tri.simplices.copy()
    ar = 0.5*((pts[T[:, 1], 0]-pts[T[:, 0], 0])*(pts[T[:, 2], 1]-pts[T[:, 0], 1]) -
              (pts[T[:, 2], 0]-pts[T[:, 0], 0])*(pts[T[:, 1], 1]-pts[T[:, 0], 1]))
    neg = ar < 0
    T[neg] = T[neg][:, [0, 2, 1]]
    ar = np.abs(ar)
    keep = ar > 1e-9
    T = T[keep]
    cent = pts[T].mean(axis=1)
    pc = {n: pts[idx[n]] for n in idx}
    zl = sorted({s[0] for s in slabs} | {s[1] for s in slabs})
    ninterval = len(zl)-1
    sol = np.zeros((ninterval, len(T)), bool)
    for (z0, z1, names) in slabs:
        j = zl.index(z0)
        assert zl[j+1] == z1, "dalles non contigues"
        sol[j] = eo_inside(cent, [pc[n] for n in names])
    nbr = {}
    for t, (a, b, c) in enumerate(T):
        nbr[(a, b)] = t; nbr[(b, c)] = t; nbr[(c, a)] = t
    vid3 = {}; V = []
    def V3(p, j):
        k = (p, j)
        if k not in vid3:
            vid3[k] = len(V); V.append((pts[p, 0], pts[p, 1], zl[j]))
        return vid3[k]
    F = []
    for j in range(ninterval):
        below = sol[j-1] if j > 0 else np.zeros(len(T), bool)
        above = sol[j+1] if j+1 < ninterval else np.zeros(len(T), bool)
        for t in np.nonzero(sol[j])[0]:
            a, b, c = T[t]
            if not below[t]:
                F.append((V3(a, j), V3(c, j), V3(b, j)))
            if not above[t]:
                F.append((V3(a, j+1), V3(b, j+1), V3(c, j+1)))
            for (u, v) in ((a, b), (b, c), (c, a)):
                n = nbr.get((v, u), -1)
                if n < 0 or not sol[j][n]:
                    u0 = V3(u, j); v0 = V3(v, j); v1 = V3(v, j+1); u1 = V3(u, j+1)
                    F.append((u0, v0, v1)); F.append((u0, v1, u1))
    return np.array(V, float), np.array(F, int)

# ----------------------------------------------------------------------------
# Verification / export
# ----------------------------------------------------------------------------
def verify(V, F):
    rep = {}
    ed = Counter()
    for a, b, c in F:
        ed[(a, b)] += 1; ed[(b, c)] += 1; ed[(c, a)] += 1
    bad = [e for e, n in ed.items() if n != 1 or ed.get((e[1], e[0]), 0) != 1]
    rep['aretes_non_manifold'] = len(bad)
    v0 = V[F[:, 0]]; v1 = V[F[:, 1]]; v2 = V[F[:, 2]]
    cr = np.cross(v1-v0, v2-v0)
    rep['triangles'] = len(F)
    rep['triangles_degeneres'] = int(np.sum(np.linalg.norm(cr, axis=1) < 1e-10))
    rep['volume_mm3'] = float(np.sum(np.einsum('ij,ij->i', v0, np.cross(v1, v2)))/6)
    # coques (composantes connexes)
    par = list(range(len(V)))
    def f(x):
        while par[x] != x:
            par[x] = par[par[x]]; x = par[x]
        return x
    for a, b, c in F:
        par[f(a)] = f(b); par[f(b)] = f(c)
    used = {f(i) for i in np.unique(F)}
    rep['coques'] = len(used)
    rep['V_E_F'] = (len(np.unique(F)), len(ed)//2, len(F))
    rep['bbox'] = (V.min(0).round(3).tolist(), V.max(0).round(3).tolist())
    rep['ok'] = rep['aretes_non_manifold'] == 0 and rep['triangles_degeneres'] == 0 and rep['volume_mm3'] > 0
    return rep

def write_stl(path, V, F, name="xt90i"):
    v0 = V[F[:, 0]]; v1 = V[F[:, 1]]; v2 = V[F[:, 2]]
    n = np.cross(v1-v0, v2-v0)
    l = np.linalg.norm(n, axis=1, keepdims=True); l[l == 0] = 1
    n = n/l
    with open(path, 'wb') as fh:
        fh.write(name.encode()[:80].ljust(80, b' '))
        fh.write(struct.pack('<I', len(F)))
        data = np.zeros(len(F), dtype=[('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')])
        data['n'] = n; data['v'] = np.stack([v0, v1, v2], 1); data['a'] = 0
        fh.write(data.tobytes())

def to_print_orientation(V, length, width):
    """rotation propre (x,y,z)->(x, L-z, y) : couche sur la face plate, face d'accouplement vers +y"""
    W = np.stack([V[:, 0] + width/2, length - V[:, 2], V[:, 1]], 1)
    return W

# ----------------------------------------------------------------------------
# Pieces
# ----------------------------------------------------------------------------
def male_part(P):
    polys = {'outer': outer_poly(P), 'cav': cavity_poly(P, P['CAV_COMP']),
             'cav_in': cavity_poly(P, P['CAV_COMP']+P['LEADIN'])}
    h = holes(P, P['PITCH_M'], P['M_POWER_D']); polys.update(h); polys.update(channels(P))
    hn = list(h.keys()); d = P['CAV_DEPTH']; f = d + P['FLOOR_T']
    slabs = [(0, P['LEADIN_LEN'], ['outer', 'cav_in']),
             (P['LEADIN_LEN'], d, ['outer', 'cav']),
             (d, f, ['outer'] + hn),
             (f, P['MALE_LEN'], ['outer', 'ch_top', 'ch_mid', 'ch_bot'])]
    return polys, slabs

def female_part(P):
    polys = {'outer': outer_poly(P), 'plug': plug_poly(P, P['PLUG_COMP']),
             'plug_in': plug_poly(P, P['PLUG_COMP']+P['LEADIN'])}
    h = holes(P, P['PITCH_F'], P['F_POWER_D']); polys.update(h); polys.update(channels(P))
    hn = list(h.keys()); pl = P['PLUG_LEN']
    slabs = [(0, P['LEADIN_LEN'], ['plug_in'] + hn),
             (P['LEADIN_LEN'], pl, ['plug'] + hn),
             (pl, 18.0, ['outer'] + hn),
             (18.0, P['FEM_LEN'], ['outer', 'ch_top', 'ch_mid', 'ch_bot'])]
    return polys, slabs

COMPS = [0.00, 0.05, 0.10, 0.15, 0.20]

def shift(poly, dx):
    return poly + np.array([dx, 0.0])

def coupon_cavities(P):
    """5 tranches de cavite male (compensation croissante). Ouverture vers le haut."""
    step = P['OUT_W'] + 2.5
    polys = {}; names_out = []; names_cav = []
    for i, c in enumerate(COMPS):
        dx = i*step
        polys[f'o{i}'] = shift(outer_poly(P), dx); polys[f'c{i}'] = shift(cavity_poly(P, c), dx)
        names_out.append(f'o{i}'); names_cav.append(f'c{i}')
    x0 = -P['OUT_W']/2 - 2.0; x1 = (len(COMPS)-1)*step + P['OUT_W']/2 + 2.0
    polys['plate'] = rect(x0, x1, -2.0, P['OUT_H']+2.0)
    slabs = [(0, 2.0, ['plate']), (2.0, 8.0, names_out + names_cav)]
    return polys, slabs

def coupon_plugs(P):
    """5 tranches de fiche femelle (compensation croissante) avec trous de contacts. Fiche vers le haut."""
    step = P['OUT_W'] + 2.5
    polys = {}; names_out = []; names_plug = []; hn = []
    for i, c in enumerate(COMPS):
        dx = i*step
        polys[f'o{i}'] = shift(outer_poly(P), dx); polys[f'p{i}'] = shift(plug_poly(P, c), dx)
        names_out.append(f'o{i}'); names_plug.append(f'p{i}')
        for k, h in holes(P, P['PITCH_F'], P['F_POWER_D']).items():
            polys[f'{k}_{i}'] = shift(h, dx); hn.append(f'{k}_{i}')
    x0 = -P['OUT_W']/2 - 2.0; x1 = (len(COMPS)-1)*step + P['OUT_W']/2 + 2.0
    polys['plate'] = rect(x0, x1, -2.0, P['OUT_H']+2.0)
    slabs = [(0, 0.6, ['plate'] + hn), (0.6, 2.0, names_out + hn), (2.0, 8.0, names_plug + hn)]
    return polys, slabs

# ----------------------------------------------------------------------------
# Controles geometriques
# ----------------------------------------------------------------------------
def signed_clearance(inner, outer, h=0.05):
    """distance minimale du contour 'inner' au contour 'outer' (negatif si 'inner' sort de 'outer')"""
    di = densify(inner, h); do = densify(outer, h)
    inside = eo_inside(di, [do])
    a = do; b = np.roll(do, -1, axis=0)
    ab = b - a; L2 = np.sum(ab*ab, 1); L2[L2 == 0] = 1
    dmin = np.full(len(di), 1e9)
    for k in range(0, len(di), 400):
        pk = di[k:k+400][:, None, :]
        t = np.clip(np.sum((pk - a[None])*ab[None], 2)/L2[None], 0, 1)
        proj = a[None] + t[..., None]*ab[None]
        dmin[k:k+400] = np.min(np.linalg.norm(pk - proj, axis=2), axis=1)
    return float(np.min(np.where(inside, dmin, -dmin)))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cav-comp', type=float, default=P['CAV_COMP'])
    ap.add_argument('--plug-comp', type=float, default=P['PLUG_COMP'])
    ap.add_argument('--outdir', default='out')
    a = ap.parse_args()
    P['CAV_COMP'] = a.cav_comp; P['PLUG_COMP'] = a.plug_comp
    os.makedirs(a.outdir, exist_ok=True)
    report = []
    jobs = [('xt90i_male_boitier', male_part, True, P['MALE_LEN']),
            ('xt90i_femelle_boitier', female_part, True, P['FEM_LEN']),
            ('xt90i_test_cavites_male', coupon_cavities, False, None),
            ('xt90i_test_fiches_femelle', coupon_plugs, False, None)]
    for name, fn, lying, L in jobs:
        polys, slabs = fn(P)
        V, F = build_mesh(polys, slabs, P['SEG'])
        np.save(os.path.join(a.outdir, name + '_model.npy'), V) if False else None
        Vm = V.copy()
        if lying:
            V = to_print_orientation(V, L, P['OUT_W'])
        rep = verify(V, F)
        path = os.path.join(a.outdir, name + '.stl')
        write_stl(path, V, F, name)
        rep['fichier'] = name + '.stl'; rep['taille_Mo'] = round(os.path.getsize(path)/1e6, 2)
        report.append(rep)
        print(name, rep)
        if lying:
            np.savez_compressed(os.path.join(a.outdir, name + '_model.npz'), V=Vm, F=F)

    # controles d'ajustement (sur les profils)
    cc, pc = P['CAV_COMP'], P['PLUG_COMP']
    print('--- controles d\'ajustement (profils, en mm) ---')
    q1 = signed_clearance(real_plug(P), cavity_poly(P, cc))
    q2 = signed_clearance(plug_poly(P, pc), real_cavity(P))
    q_pair = signed_clearance(plug_poly(P, pc), cavity_poly(P, cc))
    q3 = signed_clearance(cavity_poly(P, cc), outer_poly(P))
    q4 = signed_clearance(real_plug(P), real_cavity(P))
    print(f'vraie fiche dans cavite imprimee  : jeu mini = {q1:.3f}')
    print(f'fiche imprimee dans vraie cavite  : jeu mini = {q2:.3f}')
    print(f'fiche imprimee dans cavite imprimee: jeu mini = {q_pair:.3f}')
    print(f'paroi mini cavite imprimee/exterieur : {q3:.3f}')
    print(f'(reference vraie fiche / vraie cavite : {q4:.3f})')
    return report

if __name__ == '__main__':
    main()
