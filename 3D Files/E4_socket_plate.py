"""Socket plate for the EXISTING Sensor_Housing.STL.
Housing: footprint 6.893 (across) x 8.025 (along the beam), 16.941 tall.
  emitter  optic 12.743 above the seat, at the FRONT face
  detector optic  7.021 above the seat, 2.225 mm behind the front face
Both parts' leads exit the REAR face, 8.025 mm behind the front.
The housing drops through the plate and seats on the PCB, so its own height
sets the optics -- the plate only sets position and azimuth."""
import math, json, numpy as np, itertools, xml.etree.ElementTree as ET
CX,CY,R=38.5,61.5,38.5; wallL,wallR=CX-84,CX+84
HW,HD,HH = 6.893, 8.025, 16.941
EMIT_Y, DET_Y = 12.743, 7.021
DET_BACK = 2.225                      # detector face is this far behind the front
LEAD_E, LEAD_D = 26.5, 12.70
BEND = 2.6                            # 1.6 board + protrusion + bend loss
TARGET_Y = 112.0

brd=ET.parse('/mnt/user-data/uploads/E4 - Rebuild/documents/schematics/E4-blackpill-v2.brd').getroot().find('.//board')
els={e.get('name'):e for e in brd.find('elements').findall('element')}
def pads(n):
    e=els[n]; ex,ey=float(e.get('x')),float(e.get('y'))
    r=math.radians(float((e.get('rot') or 'R0').replace('R','')))
    return [(ex+lx*math.cos(r)-ly*math.sin(r), ey+lx*math.sin(r)+ly*math.cos(r)) for lx,ly in ((-1.27,-.5),(1.27,-.5))]
def mid(n):
    p=pads(n); return ((p[0][0]+p[1][0])/2,(p[0][1]+p[1][1])/2)
NM={'SL':'SIDELEFT','SR':'SIDERIGHT','FL':'FRONTLEFT','FR':'FRONTRIGHT'}
# components that would foul the housing seat
HT={'0603-RES':0.45,'SML0603':0.55,'1206':0.60,'SOT96P237X111-3N':1.10,'MA06-1':8.5,'BLACK_PILL':12.0}
OBST=[(float(e.get('x')),float(e.get('y'))) for e in brd.find('elements').findall('element')
      if e.get('package') in HT and float(e.get('y'))>52]

def corners(F,az):
    a=math.radians(az); fx,fy=-math.sin(a),math.cos(a); px,py=fy,-fx
    c=[]
    for u in (0.0,-HD):
        for v in (-HW/2,HW/2):
            c.append((F[0]+fx*u+px*v, F[1]+fy*u+py*v))
    return c
def inside_rim(F,az,front_overhang=3.0):
    """the seat may overhang the rim slightly at the FRONT (the plate carries it
    there); the rear half must be fully on the board"""
    a=math.radians(az); fx,fy=-math.sin(a),math.cos(a); px,py=fy,-fx
    for u,lim in ((0.0,R+front_overhang),(-HD/2,R-0.5),(-HD,R-0.5)):
        for v in (-HW/2,HW/2):
            if math.hypot(F[0]+fx*u+px*v-CX, F[1]+fy*u+py*v-CY) > lim: return False
    return True
def pads_clear(F,az,own_pads,clr=0.4):
    """no pad may sit under the housing footprint, and each pad must be at least
    `clr` behind the rear face so a lead can drop straight onto it"""
    a=math.radians(az); fx,fy=-math.sin(a),math.cos(a); px,py=fy,-fx
    for qx,qy in own_pads:
        dx,dy=qx-F[0],qy-F[1]
        u=dx*fx+dy*fy                       # +ve forward of the front face
        if u > -HD-clr: return False        # not far enough behind the rear face
    return True

def seat_clear(F,az,clr=1.3):
    a=math.radians(az); fx,fy=-math.sin(a),math.cos(a); px,py=fy,-fx
    for ox,oy in OBST:
        dx,dy=ox-F[0],oy-F[1]
        u=dx*fx+dy*fy; v=dx*px+dy*py            # along beam / across
        if -HD-clr<=u<=clr and abs(v)<=HW/2+clr: return False
    return True

SOL={}
for ch in ('SL','SR','FL','FR'):
    dmid=mid('DET-'+NM[ch]); emid=mid('IR-'+NM[ch])
    tgt=(wallL if ch=='SL' else wallR, TARGET_Y) if ch in ('SL','SR') else None
    best=None
    for azs in np.arange(-88,89,0.5):
        az = azs if ch in ('SL','FL') else azs
        a=math.radians(az); fx,fy=-math.sin(a),math.cos(a)
        for rx in np.arange(dmid[0]-3.0,dmid[0]+3.01,0.25):
            for ry in np.arange(dmid[1]-3.0,dmid[1]+3.01,0.25):
                rear=(rx,ry)
                runD=math.hypot(rx-dmid[0],ry-dmid[1]); runE=math.hypot(rx-emid[0],ry-emid[1])
                if DET_Y+runD+BEND>LEAD_D-0.5: continue
                if EMIT_Y+runE+BEND>LEAD_E-0.5: continue
                F=(rx+fx*HD, ry+fy*HD)
                if not inside_rim(F,az) or not seat_clear(F,az): continue
                if not pads_clear(F,az,pads('DET-'+NM[ch])+pads('IR-'+NM[ch])): continue
                if tgt:
                    # where does the emitter beam actually land on the side wall?
                    if abs(fx)<1e-3: continue
                    t=(tgt[0]-F[0])/fx
                    if t<=0: continue
                    ly=F[1]+fy*t
                    if not (110.0 <= ly <= 117.0): continue     # flat face just inboard of the post (Ch7)
                    if abs(az)>70.0: continue                  # keep a real forward component
                    d=t; sc=d
                else:
                    if abs(az-(10.0 if ch=='FL' else -10.0))>0.8: continue
                    sc=runD+runE; d=None
                if best is None or sc<best[0]: best=(sc,F,az,rear,runD,runE,d)
    assert best, ch+": infeasible"
    sc,F,az,rear,runD,runE,d=best
    a=math.radians(az); fx,fy=-math.sin(a),math.cos(a)
    det=(F[0]-fx*DET_BACK, F[1]-fy*DET_BACK)
    SOL[ch]=dict(land_y=round(F[1]+math.cos(math.radians(az))*d,1) if d else None, front=[round(F[0],2),round(F[1],2)], az=round(az,2), rear=[round(rear[0],2),round(rear[1],2)],
                 emit_optic=[round(F[0],2),round(F[1],2),EMIT_Y], det_optic=[round(det[0],2),round(det[1],2),DET_Y],
                 leadE=round(EMIT_Y+runE+BEND,1), leadD=round(DET_Y+runD+BEND,1), dist=(round(d,1) if d else None))
    print("%-3s front (%6.2f,%6.2f) az %+6.1f | rear (%6.2f,%6.2f) | lead E %4.1f/26.5  D %4.1f/12.7 | %s"
          %(ch,F[0],F[1],az,rear[0],rear[1],EMIT_Y+runE+BEND,DET_Y+runD+BEND,
            ("reach %.1f mm, lands y=%.1f"%(d,F[1]+fy*d)) if d else "front pair"))
json.dump(SOL,open('sockets_v2.json','w'),indent=1)
print("\nSL emitter reach %.1f mm (board as routed: 81.1)  -> x%.2f on the emitter leg"
      %(SOL['SL']['dist'],(81.14/SOL['SL']['dist'])**2))
"""E4 sensor SOCKET PLATE -- holds four EXISTING Sensor_Housing parts.
The housing passes through the plate and seats on the PCB, so the housing's own
height sets the optics (detector 7.02, emitter 12.74 above the board) and the
plate only sets position and azimuth.  Epoxy in the pocket."""
import math, json, numpy as np, trimesh, xml.etree.ElementTree as ET
from shapely.geometry import Polygon, MultiPoint
from trimesh.creation import extrude_polygon, box, cylinder
S=json.load(open('sockets_v2.json'))
CX,CY,R=38.5,61.5,38.5; PCB_T=1.6
HW,HD=6.893,8.025; FIT=0.30                 # pocket clearance for print + epoxy
Z0,Z1=2.00,8.40   # plate underside / top.  The emitter shroud's underside is at
                  # 9.296 mm, so the plate can be 6.4 mm thick and still pass under
                  # it -- twice the section, and the pocket grips over 6.4 mm.
RIN,ROUT=31.6,40.5; TH0,TH1=1.5,178.5   # RIN clears the Black Pill (max radius 30.78)
def rot(a,ax=[0,0,1]): return trimesh.transformations.rotation_matrix(a,ax)
def sector(r0,r1,t0,t1,z0,z1,n=200):
    a=np.linspace(math.radians(t0),math.radians(t1),n)
    pts=[(CX+r1*math.cos(t),CY+r1*math.sin(t)) for t in a]+[(CX+r0*math.cos(t),CY+r0*math.sin(t)) for t in a[::-1]]
    m=extrude_polygon(Polygon(pts),z1-z0); m.apply_translation([0,0,z0]); return m
def place(m,x,y,z,az):
    m=m.copy(); m.apply_transform(rot(math.radians(az))); m.apply_translation([x,y,z]); return m

add=[sector(RIN,ROUT,TH0,TH1,Z0,Z1)]; sub=[]
for x0,x1 in ((0.8,12.0),(65.6,76.4)):
    p=box(extents=[x1-x0,22.0,Z1-Z0]); p.apply_translation([(x0+x1)/2,65.0,(Z0+Z1)/2]); add.append(p)

for ch in ('SL','SR','FL','FR'):
    s=S[ch]; F=s['front']; az=s['az']
    a=math.radians(az); fx,fy=-math.sin(a),math.cos(a)
    # pocket: centred HD/2 behind the front face
    col=box(extents=[HW+FIT+6.0, HD+FIT+6.0, Z1-Z0])
    col.apply_translation([0,-HD/2,(Z0+Z1)/2]); add.append(place(col,F[0],F[1],0,az))
    pk=box(extents=[HW+FIT, HD+FIT, (Z1-Z0)+4.0])
    pk.apply_translation([0,-HD/2,(Z0+Z1)/2])
    sub.append(place(pk,F[0],F[1],0,az))
    # beam relief: nothing in front of the housing, full plate height
    # (the hand-placed relief box is gone -- the cone subtraction below does it
    #  properly, and the box was removing material the plate needed)

# Feet hook the board's STRAIGHT side edges at y = 58, well clear of the
# housings (the curved nose rim is fully occupied by them).  With the apex bolt
# that is a 77 x 40 mm triangle.
# Feet hook the board's STRAIGHT side edges at y = 58, clear of the housings
# (the curved nose rim is fully occupied).  With the apex bolt: a 77 x 40 triangle.

# --- lead slots ------------------------------------------------------------
# Sixteen leads drop from the housings' rear faces onto the board; the plate has
# to be open at every pad.  (Their absence is what made the first socket plate
# unbuildable -- all 16 lead paths were blocked.)
from shapely.geometry import MultiPoint as _MP
_brd=ET.parse('/mnt/user-data/uploads/E4 - Rebuild/documents/schematics/E4-blackpill-v2.brd').getroot().find('.//board')
_els={e.get('name'):e for e in _brd.find('elements').findall('element')}
def _pads(n):
    e=_els[n]; ex,ey=float(e.get('x')),float(e.get('y'))
    r=math.radians(float((e.get('rot') or 'R0').replace('R','')))
    return [(ex+lx*math.cos(r)-ly*math.sin(r), ey+lx*math.sin(r)+ly*math.cos(r)) for lx,ly in ((-1.27,-.5),(1.27,-.5))]
_NM={'SL':'SIDELEFT','SR':'SIDERIGHT','FL':'FRONTLEFT','FR':'FRONTRIGHT'}
for _ch in ('SL','SR','FL','FR'):
    for _pre in ('IR-','DET-'):
        _poly=_MP(_pads(_pre+_NM[_ch])).convex_hull.buffer(1.8,resolution=12)
        _sl=extrude_polygon(_poly,(Z1-Z0)+4.0); _sl.apply_translation([0,0,Z0-2.0])
        sub.append(_sl)


# --- component keep-outs ----------------------------------------------------
# Subtract the real library footprints (not a guessed box -- that mistake had the
# plate sitting through the Black Pill in every earlier version).
def _wbox(nm,h,margin=0.6):
    e=_els[nm]; p={q.get('name'):q for q in _brd.find('libraries').iter('package')}[e.get('package')]
    xs=[];ys=[]
    for t in list(p.findall('pad'))+list(p.findall('smd')):
        xs.append(float(t.get('x'))); ys.append(float(t.get('y')))
    for w in p.findall('wire'):
        if w.get('layer') in ('21','51','20'):
            xs+=[float(w.get('x1')),float(w.get('x2'))]; ys+=[float(w.get('y1')),float(w.get('y2'))]
    r=math.radians(float((e.get('rot') or 'R0').replace('R','')))
    ex,ey=float(e.get('x')),float(e.get('y')); cs=[]
    for lx in (min(xs),max(xs)):
        for ly in (min(ys),max(ys)):
            cs.append((ex+lx*math.cos(r)-ly*math.sin(r), ey+lx*math.sin(r)+ly*math.cos(r)))
    X=[c[0] for c in cs]; Y=[c[1] for c in cs]
    bx=box(extents=[max(X)-min(X)+2*margin, max(Y)-min(Y)+2*margin, h+2*margin])
    bx.apply_translation([(min(X)+max(X))/2,(min(Y)+max(Y))/2,h/2]); return bx
for _nm,_h in (('U$7',12.0),('BLUETOOTH',8.5)):
    sub.append(_wbox(_nm,_h))


# --- beam cones -------------------------------------------------------------
# With a 6.4 mm plate the only thing that must stay clear is the light itself.
# Subtract each optic's cone (half-angle + 5 deg of margin) rather than trusting
# a hand-placed relief box.
EMIT_Y,DET_Y,DET_BACK=12.743,7.021,2.225
def _cone(ox,oy,oz,az,half,L=50.0):
    r=L*math.tan(math.radians(half))
    c=trimesh.creation.cone(radius=r,height=L,sections=48)
    c.apply_translation([0,0,-L])                       # apex at origin, opening -z
    a=math.radians(az); f=np.array([-math.sin(a),math.cos(a),0.0])
    M=trimesh.geometry.align_vectors(np.array([0,0,-1.0]),f)
    c.apply_transform(M); c.apply_translation([ox,oy,oz]); return c
# The EMITTER is at 12.743, well above the plate, so it looks out over the top --
# a cone with margin is all it needs.  The DETECTOR is at 7.021, BELOW the 8.4 mm
# plate top, so a cone would leave it peering down a conical tunnel with a
# restricted field of view.  It gets an open trench instead: everything above
# z = 4.0 forward of it is removed, so it looks out over open sky.
DET_TRENCH_Z = 4.0
for _ch in ('SL','SR','FL','FR'):
    _s=S[_ch]; _F=_s['front']; _a=math.radians(_s['az'])
    _fx,_fy=-math.sin(_a),math.cos(_a)
    sub.append(_cone(_F[0],_F[1],EMIT_Y,_s['az'],10.0+5.0))
    _dx,_dy=_F[0]-_fx*DET_BACK, _F[1]-_fy*DET_BACK
    _L=34.0
    _tr=box(extents=[17.0,_L,(Z1-DET_TRENCH_Z)+6.0])
    _tr.apply_translation([0,_L/2-0.5,DET_TRENCH_Z+((Z1-DET_TRENCH_Z)+6.0)/2])
    sub.append(place(_tr,_dx,_dy,0,_s['az']))

HOOK_Z=-(PCB_T+2.2); ARM_Z=Z1; FY=58.0
for ex,out in ((0.0,-1.0),(77.0,+1.0)):
    cx_=ex-out*1.0                                   # foot centre, 1 mm inboard
    add.append(box(extents=[9.0,9.0,ARM_Z-HOOK_Z]).apply_translation([cx_,FY,(ARM_Z+HOOK_Z)/2]) or None) if False else None
    f=box(extents=[9.0,9.0,ARM_Z-HOOK_Z]); f.apply_translation([cx_,FY,(ARM_Z+HOOK_Z)/2]); add.append(f)
    # slot: from 0.5 mm outboard of the edge, 6 mm inboard -- leaves a 3.5 mm web
    sl=box(extents=[6.5,9.6,PCB_T+0.20]); sl.apply_translation([ex-out*(-0.5)+out*0.0,FY,-(PCB_T+0.20)/2+0.10])
    sl.apply_translation([(ex + (-out)*3.25) - (ex-out*(-0.5)), 0, 0])
    sub.append(sl)
    rel=box(extents=[6.5,1.6,0.60]); rel.apply_translation([ex+(-out)*3.25,FY,-0.30]); sub.append(rel)
    tie=box(extents=[9.0,10.0,Z1-Z0]); tie.apply_translation([cx_,FY+6.5,(Z0+Z1)/2]); add.append(tie)
bo=cylinder(radius=3.4,height=Z1-Z0); bo.apply_translation([38.5,98.0,(Z0+Z1)/2]); add.append(bo)
hl=cylinder(radius=1.15,height=40); hl.apply_translation([38.5,98.0,0]); sub.append(hl)

body=trimesh.boolean.union(add,engine='manifold')
body=trimesh.boolean.difference([body]+sub,engine='manifold')
print("plate: watertight %s  bodies %d  %.0f mm3 (~%.0f g PLA)"%(body.is_watertight,body.body_count,body.volume,body.volume*1.24e-3))
b=body.bounds; print("bbox X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f"%(b[0][0],b[1][0],b[0][1],b[1][1],b[0][2],b[1][2]))
body.export('E4_socket_plate_v8.stl'); print("exported E4_socket_plate_v6.stl")
import math, json, numpy as np, trimesh, xml.etree.ElementTree as ET
S=json.load(open('sockets_v2.json')); P=trimesh.load('E4_socket_plate_v8.stl')
H=trimesh.load('/mnt/user-data/uploads/E4 - Rebuild/3D Files/Sensor_Housing.STL')
b=H.bounds; H.apply_translation([-(b[0][0]+b[1][0])/2,-b[0][1],-b[0][2]])
EMIT_Y,DET_Y,DET_BACK,HD=12.743,7.021,2.225,8.025
brd=ET.parse('/mnt/user-data/uploads/E4 - Rebuild/documents/schematics/E4-blackpill-v2.brd').getroot().find('.//board')
els={e.get('name'):e for e in brd.find('elements').findall('element')}
def pads(n):
    e=els[n]; ex,ey=float(e.get('x')),float(e.get('y'))
    r=math.radians(float((e.get('rot') or 'R0').replace('R','')))
    return [(ex+lx*math.cos(r)-ly*math.sin(r), ey+lx*math.sin(r)+ly*math.cos(r)) for lx,ly in ((-1.27,-.5),(1.27,-.5))]
NM={'SL':'SIDELEFT','SR':'SIDERIGHT','FL':'FRONTLEFT','FR':'FRONTRIGHT'}
def pose(az,F):
    a=math.radians(az); fx,fy=-math.sin(a),math.cos(a); px,py=fy,-fx
    M=np.eye(4); M[:3,0]=[px,py,0]; M[:3,1]=[0,0,1]; M[:3,2]=[-fx,-fy,0]; M[:3,3]=[F[0],F[1],0]; return M
hs={c:H.copy() for c in S}
for c in S: hs[c].apply_transform(pose(S[c]['az'],S[c]['front']))
ALL=trimesh.boolean.union([P]+list(hs.values()),engine='manifold')
print("1. BEAM CONES (plate + all four housings)")
tot=0;blk=0
for ch in ('SL','SR','FL','FR'):
    s=S[ch]; F=s['front']; a=math.radians(s['az']); fx,fy=-math.sin(a),math.cos(a)
    for tag,ox,oy,oz,half in (('emitter',F[0],F[1],EMIT_Y,10.0),
                              ('detector',F[0]-fx*DET_BACK,F[1]-fy*DET_BACK,DET_Y,12.5)):
        f=np.array([fx,fy,0.0]); u=np.array([math.cos(a),math.sin(a),0.0]); v=np.cross(f,u)
        n=0;t_=0
        for ang in (0.0,half*0.6,half):
            for k in range(16 if ang>0 else 1):
                th=2*math.pi*k/16
                d=f*math.cos(math.radians(ang))+(u*math.cos(th)+v*math.sin(th))*math.sin(math.radians(ang))
                t=np.arange(0.4,45.0,0.15); t_+=1
                if ALL.contains(np.column_stack([ox+d[0]*t,oy+d[1]*t,oz+d[2]*t])).any(): n+=1
        tot+=t_; blk+=n
        print("   %-3s %-8s %2d of %d blocked"%(ch,tag,n,t_))
print("   TOTAL %d of %d blocked"%(blk,tot))
print("\n2. LEAD PATHS (pad must be clear of plate AND housing, from board to lead height)")
bad=0
for ch in ('SL','SR','FL','FR'):
    for tag,pre,hgt in (('emitter','IR-',EMIT_Y),('detector','DET-',DET_Y)):
        for i,(qx,qy) in enumerate(pads(pre+NM[ch])):
            col=np.column_stack([np.full(26,qx),np.full(26,qy),np.linspace(0.2,hgt,26)])
            if ALL.contains(col).any(): bad+=1; print("   BLOCKED %s %s pad%d"%(ch,tag,i))
print("   %d of 16 lead paths blocked"%bad)
print("\n3. MESH")
n=P.face_normals; ar=P.area_faces; dn=n[:,2]<-1e-6
ang=np.degrees(np.arccos(np.clip(-n[dn][:,2],0,1))); cb=P.triangles_center[dn]; bd=ang<45.0
bed=bd&(np.abs(cb[:,2]-2.0)<0.05); feet=bd&(cb[:,2]<1.0); other=bd&~bed&~feet
bb=P.bounds
print("   watertight %s, %d body, %.0f mm3 (~%.0f g), %.0f x %.0f x %.0f mm"%(P.is_watertight,P.body_count,P.volume,P.volume*1.24e-3,bb[1][0]-bb[0][0],bb[1][1]-bb[0][1],bb[1][2]-bb[0][2]))
print("   unsupported: bed %.0f | feet %.0f | GENUINE OVERHANG %.1f mm2"%(ar[dn][bed].sum(),ar[dn][feet].sum(),ar[dn][other].sum()))
