"""E4 sensor socket plate, rev B.
Nose plate unchanged.  Edge grippers DELETED (they sat in the wheel cutouts).
Rear fixing: 2 mm pads on the chassis board at the forward motor-mount studs
(17,51) and (60,51), reached by arms running the clear margin between the board
edge and the Black Pill."""
import math, json, numpy as np, trimesh, xml.etree.ElementTree as ET
from shapely.geometry import Polygon, MultiPoint
from trimesh.creation import extrude_polygon, box, cylinder
S=json.load(open('sockets_v2.json'))
CX,CY,R=38.5,61.5,38.5; PCB_T=1.6
HW,HD=6.893,8.025; FIT=0.50   # MJF: +-0.3 mm, guideline asks >=0.6 between mating parts
Z0,Z1=2.00,8.40
PAD_Z=2.00                      # rear pads: 2 mm on the board, under the daughterboard
RIN,ROUT=31.6,40.5; TH0,TH1=1.5,178.5
def rot(a,ax=[0,0,1]): return trimesh.transformations.rotation_matrix(a,ax)
def sector(r0,r1,t0,t1,z0,z1,n=200):
    a=np.linspace(math.radians(t0),math.radians(t1),n)
    pts=[(CX+r1*math.cos(t),CY+r1*math.sin(t)) for t in a]+[(CX+r0*math.cos(t),CY+r0*math.sin(t)) for t in a[::-1]]
    m=extrude_polygon(Polygon(pts),z1-z0); m.apply_translation([0,0,z0]); return m
def place(m,x,y,z,az):
    m=m.copy(); m.apply_transform(rot(math.radians(az))); m.apply_translation([x,y,z]); return m

# ---- the REAL board outline, parsed not assumed ---------------------------
def edge_left(y):
    if y<=10.04: return 0.0
    if y<=14.5:  return 0.0+(y-10.04)/4.46*11.46
    if y<=54.5:  return 11.46
    if y<=61.5:  return 11.46-(y-54.5)/7.0*4.46
    d=R*R-(y-CY)**2
    return CX-math.sqrt(d) if d>0 else CX
BP_X0,BP_Y0,BP_Y1 = 12.7, 57.2, 77.5     # Black Pill, real library footprint

add=[sector(RIN,ROUT,TH0,TH1,Z0,Z1)]; sub=[]

# ---- feet + arms, both sides ----------------------------------------------
# The foot IS the motor-mount rail on the silkscreen: 6 mm wide (x 14..20),
# 40 mm long (y 14.5..54.5), 2 mm thick, with BOTH studs through it.  It
# replaces the single M2 nut that currently sits under each mount, so the
# mount slots straight down on top of it.
FOOT_X0,FOOT_X1 = 14.0, 20.0
FOOT_Y0,FOOT_Y1 = 14.5, 54.8   # mount is 40 long x 6 wide x 15 high, standing up
FOOT_Z          = 1.60         # = one M2 nut.  The wheels are the datum, so a
                               # thicker foot would push the BOARD down, not the
                               # sensors up -- 1.6 keeps every height as built.
TAB_Y1          = 56.8         # tab forward of the mount, where it turns up
ARM_Y0          = 55.0
for side in (0,1):                       # 0 = left, 1 mirrors to the right
    def mx(x): return (77.0-x) if side else x
    foot=Polygon([(mx(FOOT_X0),FOOT_Y0),(mx(FOOT_X1),FOOT_Y0),
                  (mx(FOOT_X1),FOOT_Y1),(mx(FOOT_X0),FOOT_Y1)])
    add.append(extrude_polygon(foot,FOOT_Z))
    # forward of the mount the tab steps up to the plate underside
    tab=Polygon([(mx(FOOT_X0),FOOT_Y1),(mx(FOOT_X1),FOOT_Y1),
                 (mx(FOOT_X1),TAB_Y1),(mx(FOOT_X0),TAB_Y1)])
    add.append(extrude_polygon(tab,Z0))
    # arm: corridor between the board edge and the Black Pill, 2.0 -> 8.4 mm
    L=[];Rr=[]
    for y in np.arange(ARM_Y0,79.01,0.25):
        # forward of the wheels (y>=57) the arm may hang 0.8 mm past the board
        # edge -- nothing is under it there, and it buys width at the pinch
        if   y<56.5: off= 0.30
        elif y<57.0: off= 0.30-(y-56.5)/0.5*1.10
        else:        off=-0.80
        lo=edge_left(y)+off
        if   y<BP_Y0: hi=21.0
        elif y<=BP_Y1: hi=BP_X0-0.20
        else: hi=21.0
        hi=max(hi,lo+1.6)
        L.append((mx(lo),y)); Rr.append((mx(hi),y))
    armpoly=Polygon(L+Rr[::-1])
    am=extrude_polygon(armpoly,Z1-Z0); am.apply_translation([0,0,Z0]); add.append(am)
    # M2 clearance at BOTH studs on this side
    for sy in (17.0,51.0):
        h=cylinder(radius=1.25,height=30); h.apply_translation([mx(17.0),sy,0]); sub.append(h)

# ---- housings, beams, trenches (unchanged) --------------------------------
brd=ET.parse('/mnt/user-data/uploads/E4 - Rebuild/documents/schematics/E4-blackpill-v2.brd').getroot().find('.//board')
els={e.get('name'):e for e in brd.find('elements').findall('element')}
def pads_of(n):
    e=els[n]; ex,ey=float(e.get('x')),float(e.get('y'))
    r=math.radians(float((e.get('rot') or 'R0').replace('R','')))
    return [(ex+lx*math.cos(r)-ly*math.sin(r), ey+lx*math.sin(r)+ly*math.cos(r)) for lx,ly in ((-1.27,-.5),(1.27,-.5))]
NM={'SL':'SIDELEFT','SR':'SIDERIGHT','FL':'FRONTLEFT','FR':'FRONTRIGHT'}
for ch in ('SL','SR','FL','FR'):
    s=S[ch]; F=s['front']; az=s['az']
    col=box(extents=[HW+FIT+6.0,HD+FIT+6.0,Z1-Z0]); col.apply_translation([0,-HD/2,(Z0+Z1)/2])
    add.append(place(col,F[0],F[1],0,az))
    pk=box(extents=[HW+FIT,HD+FIT,(Z1-Z0)+4.0]); pk.apply_translation([0,-HD/2,(Z0+Z1)/2])
    sub.append(place(pk,F[0],F[1],0,az))
for ch in ('SL','SR','FL','FR'):
    for pre in ('IR-','DET-'):
        poly=MultiPoint(pads_of(pre+NM[ch])).convex_hull.buffer(1.8,resolution=12)
        sl=extrude_polygon(poly,(Z1-Z0)+4.0); sl.apply_translation([0,0,Z0-2.0]); sub.append(sl)
EMIT_Y,DET_Y,DET_BACK=12.743,7.021,2.225
def cone(ox,oy,oz,az,half,L=50.0):
    r=L*math.tan(math.radians(half)); c=trimesh.creation.cone(radius=r,height=L,sections=48)
    c.apply_translation([0,0,-L]); a=math.radians(az); f=np.array([-math.sin(a),math.cos(a),0.0])
    c.apply_transform(trimesh.geometry.align_vectors(np.array([0,0,-1.0]),f)); c.apply_translation([ox,oy,oz]); return c
for ch in ('SL','SR','FL','FR'):
    s=S[ch]; F=s['front']; a=math.radians(s['az']); fx,fy=-math.sin(a),math.cos(a)
    sub.append(cone(F[0],F[1],EMIT_Y,s['az'],15.0))
    tr=box(extents=[17.0,34.0,(Z1-4.0)+6.0]); tr.apply_translation([0,16.5,4.0+((Z1-4.0)+6.0)/2])
    sub.append(place(tr,F[0]-fx*DET_BACK,F[1]-fy*DET_BACK,0,s['az']))
for nm,h in (('U$7',12.0),('BLUETOOTH',8.5)):
    e=els[nm]; p={q.get('name'):q for q in brd.find('libraries').iter('package')}[e.get('package')]
    xs=[];ys=[]
    for t in list(p.findall('pad'))+list(p.findall('smd')): xs.append(float(t.get('x'))); ys.append(float(t.get('y')))
    for w in p.findall('wire'):
        if w.get('layer') in ('21','51','20'): xs+=[float(w.get('x1')),float(w.get('x2'))]; ys+=[float(w.get('y1')),float(w.get('y2'))]
    r=math.radians(float((e.get('rot') or 'R0').replace('R',''))); ex,ey=float(e.get('x')),float(e.get('y')); cs=[]
    for lx in (min(xs),max(xs)):
        for ly in (min(ys),max(ys)): cs.append((ex+lx*math.cos(r)-ly*math.sin(r), ey+lx*math.sin(r)+ly*math.cos(r)))
    X=[c[0] for c in cs]; Y=[c[1] for c in cs]
    b=box(extents=[max(X)-min(X)+1.2,max(Y)-min(Y)+1.2,h+1.2]); b.apply_translation([(min(X)+max(X))/2,(min(Y)+max(Y))/2,h/2]); sub.append(b)
# apex boss: with the grippers gone this is the third foot, so it runs all the
# way down to the board -- same 2 mm seat as the rear pads, no air under the bolt
bo=cylinder(radius=3.4,height=Z1); bo.apply_translation([38.5,98.0,Z1/2]); add.append(bo)
hl=cylinder(radius=1.15,height=40); hl.apply_translation([38.5,98.0,0]); sub.append(hl)

body=trimesh.boolean.union(add,engine='manifold')
body=trimesh.boolean.difference([body]+sub,engine='manifold')
print("watertight %s  bodies %d  %.0f mm3 (~%.0f g PLA)"%(body.is_watertight,body.body_count,body.volume,body.volume*1.24e-3))
b=body.bounds; print("bbox X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f"%(b[0][0],b[1][0],b[0][1],b[1][1],b[0][2],b[1][2]))
body.export('E4_socket_plate_revD.stl'); print("exported")
