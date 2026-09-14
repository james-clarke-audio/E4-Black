"""E4 sensor carrier v2.  Emitter and detector are solved INDEPENDENTLY and aimed
at a common target point, because their leads differ by 2:1.
  TSAL6100 : barrel 5.00, body 8.70, leads 26.5, half-angle +/-10 deg
  OP505A   : barrel 3.05, body 4.83, leads 12.7, viewing angle 25 deg
"""
import math, json, numpy as np, trimesh, xml.etree.ElementTree as ET
from shapely.geometry import Polygon
from trimesh.creation import extrude_polygon, box, cylinder

CX,CY,R=38.5,61.5,38.5; PCB_T=1.6; wallL,wallR=CX-84,CX+84
EM=dict(d=5.00, body=8.70, lead=26.5, h=12.0)
DT=dict(d=3.05, body=4.83, lead=12.70, h=6.0)
# lead budget: plan run + height + (1.6 board + ~0.8 protrusion + ~0.8 lost in two bends)
BEND=3.2; RIMM=2.0
STANDOFF,PLATE_T=2.00,1.80; Z0,Z1=STANDOFF,STANDOFF+PLATE_T
WALL=1.60; RIN,ROUT=26.0,37.2; TH0,TH1=1.5,178.5

brd=ET.parse('/mnt/user-data/uploads/E4 - Rebuild/documents/schematics/E4-blackpill-v2.brd').getroot().find('.//board')
els={e.get('name'):e for e in brd.find('elements').findall('element')}
def pads(n):
    e=els[n]; ex,ey=float(e.get('x')),float(e.get('y'))
    r=math.radians(float((e.get('rot') or 'R0').replace('R','')))
    return [(ex+lx*math.cos(r)-ly*math.sin(r), ey+lx*math.sin(r)+ly*math.cos(r)) for lx,ly in ((-1.27,-.5),(1.27,-.5))]

def solve(part, pad, target, xlo, xhi, ylo, yhi, FRONT=False):
    budget = part['lead'] - part['h'] - BEND - 2.0
    best=None
    for ox in np.arange(xlo,xhi,0.25):
        for oy in np.arange(ylo,yhi,0.25):
            if math.hypot(ox-CX,oy-CY) > R-RIMM-part['d']/2: continue
            az=math.degrees(math.atan2(ox-target[0], target[1]-oy))
            a=math.radians(az); dx,dy=-math.sin(a),math.cos(a)
            ex,ey=ox-dx*part['body'], oy-dy*part['body']
            if min(math.hypot(ex-p[0],ey-p[1]) for p in pad) > budget: continue
            run=min(math.hypot(ex-p[0],ey-p[1]) for p in pad)
            d=math.hypot(ox-target[0],oy-target[1])
            score = run if FRONT else d
            if best is None or score<best[0]: best=(score,ox,oy,az,run,d)
    return best

TGT={'SL':(wallL,112.0),'SR':(wallR,112.0),'FL':None,'FR':None}
NAME={'SL':'SIDELEFT','SR':'SIDERIGHT','FL':'FRONTLEFT','FR':'FRONTRIGHT'}
SOL={}
print("%-4s %-9s %-16s %7s %8s %9s %9s"%("ch","part","optic (x,y,z)","azim","elev","to target","lead used"))
print("-"*78)
for ch in ('SL','SR','FL','FR'):
    tgt=TGT[ch]
    if tgt is None:   # front pair: aim +/-10 deg, target far up the corridor
        az0=10.0 if ch=='FL' else -10.0
        ref=(5.0 if ch=='FL' else 72.0, 70.0)
        a=math.radians(az0); tgt=(ref[0]-math.sin(a)*400, ref[1]+math.cos(a)*400)
    for tag,part,pd in (('emitter',EM,pads('IR-'+NAME[ch])),('detector',DT,pads('DET-'+NAME[ch]))):
        xlo,xhi = (1.0,40.0) if ch in ('SL','FL') else (37.0,76.0)
        b=solve(part,pd,tgt,xlo,xhi,60.0,97.0,FRONT=(ch in ('FL','FR')))
        if b is None: print("   %s %s: NO SOLUTION"%(ch,tag)); continue
        _,ox,oy,az,run,d=b
        SOL.setdefault(ch,{})[tag]=dict(x=round(ox,2),y=round(oy,2),z=part['h'],az=round(az,2),
                                        dist=round(d,1),lead=round(run+part['h']+BEND,1),dia=part['d'],body=part['body'])
    e,t=SOL[ch]['emitter'],SOL[ch]['detector']
    # detector elevation: look up at the spot the emitter lights
    elev=math.degrees(math.atan2(e['z']-t['z'], t['dist']))
    SOL[ch]['detector']['elev']=round(elev,2); SOL[ch]['emitter']['elev']=0.0
    for tag in ('emitter','detector'):
        s=SOL[ch][tag]
        print("%-4s %-9s (%5.2f,%5.2f,%4.1f) %+7.1f %+8.2f %9.1f %8.1f/%.1f"
              %(ch,tag,s['x'],s['y'],s['z'],s['az'],s['elev'],s['dist'],s['lead'],(EM if tag=='emitter' else DT)['lead']))
json.dump(SOL,open('solved_v2.json','w'),indent=1)
print("\nSL emitter %.1f mm and detector %.1f mm to the target (was 81.1 / 81.1)"
      %(SOL['SL']['emitter']['dist'],SOL['SL']['detector']['dist']))
print("round trip  (81.1*81.1)/(%.1f*%.1f) = x%.2f"
      %(SOL['SL']['emitter']['dist'],SOL['SL']['detector']['dist'],
        (81.14*81.14)/(SOL['SL']['emitter']['dist']*SOL['SL']['detector']['dist'])))
print("emitter floor margin %.1f deg;  detector tilted UP %.2f deg (away from the floor)"
      %(math.degrees(math.atan2(EM['h'],SOL['SL']['emitter']['dist'])),SOL['SL']['detector']['elev']))
import math, json, numpy as np, trimesh, xml.etree.ElementTree as ET
from shapely.geometry import Polygon
from trimesh.creation import extrude_polygon, box, cylinder
S=json.load(open('solved_v2.json'))
CX,CY,R=38.5,61.5,38.5; PCB_T=1.6
STANDOFF,PLATE_T=2.00,1.80; Z0,Z1=STANDOFF,STANDOFF+PLATE_T
WALL=1.60; RIN,ROUT=26.0,37.2; TH0,TH1=1.5,178.5
def rot(a,ax=[0,0,1]): return trimesh.transformations.rotation_matrix(a,ax)
def vcut(length,d,extra=8.0):
    apex=-d/math.sqrt(2.0); w=extra
    p=Polygon([(0,apex),(w,apex+w),(w,apex+w+3),(-w,apex+w+3),(-w,apex+w)])
    m=extrude_polygon(p,length); m.apply_transform(rot(math.pi/2,[1,0,0])); return m
def sector(r0,r1,t0,t1,z0,z1,n=190):
    a=np.linspace(math.radians(t0),math.radians(t1),n)
    pts=[(CX+r1*math.cos(t),CY+r1*math.sin(t)) for t in a]+[(CX+r0*math.cos(t),CY+r0*math.sin(t)) for t in a[::-1]]
    m=extrude_polygon(Polygon(pts),z1-z0); m.apply_translation([0,0,z0]); return m
def place(m,x,y,z,az):
    m=m.copy(); m.apply_transform(rot(math.radians(az))); m.apply_translation([x,y,z]); return m

add=[sector(RIN,ROUT,TH0,TH1,Z0,Z1)]; sub=[]
for sx in (0.8,63.0):
    p=box(extents=[13.2,17.0,PLATE_T]); p.apply_translation([sx+6.6,63.0,(Z0+Z1)/2]); add.append(p)

for ch in ('SL','SR','FL','FR'):
    for tag in ('emitter','detector'):
        s=S[ch][tag]; d=s['dia']; body=s['body']; h=s['z']; az=s['az']; elev=s['elev']
        bw=d+2*WALL; bl=body+3.5; btop=h+d/2+0.8
        b=box(extents=[bw,bl,btop-Z0]); b.apply_translation([0,-(body/2+1.2),(btop+Z0)/2])
        add.append(place(b,s['x'],s['y'],0,az))
        v=vcut(body+4.0,d); v.apply_transform(rot(math.radians(elev),[1,0,0]))
        v.apply_translation([0,0,h]); sub.append(place(v,s['x'],s['y'],0,az))
        # epoxy well: widen the trough for the 3 mm behind the part's rear flange, so
        # the fillet keys to the block instead of being squeezed into the V.  Rear of
        # the part, well clear of the optics -- IR-transparent epoxy does not matter here.
        w=box(extents=[d+2.0,3.0,d+2.0]); w.apply_translation([0,-(body+1.5),h])
        sub.append(place(w,s['x'],s['y'],0,az))

FOOT_W,FOOT_D=9.0,9.0; FOOT_R=R-1.0; HOOK_Z=-(PCB_T+2.2); ARM_Z=2.2
for th in (30.0,150.0):
    a=math.radians(th); ax,ay=CX+FOOT_R*math.cos(a),CY+FOOT_R*math.sin(a)
    add.append(place(box(extents=[FOOT_W,FOOT_D,ARM_Z-HOOK_Z]),ax,ay,(ARM_Z+HOOK_Z)/2,th-90))
    sl_len=(R-(FOOT_R-FOOT_D/2))+1.5
    sl=box(extents=[FOOT_W+0.6,sl_len,PCB_T+0.20]); sl.apply_translation([0,(R-FOOT_R)-sl_len/2,-(PCB_T+0.20)/2+0.10])
    sub.append(place(sl,ax,ay,0,th-90))
    rel=box(extents=[1.6,sl_len,0.60]); rel.apply_translation([0,(R-FOOT_R)-sl_len/2,-0.20])
    sub.append(place(rel,ax,ay,0,th-90))
bo=cylinder(radius=3.4,height=Z1); bo.apply_translation([38.5,98.0,Z1/2]); add.append(bo)
hl=cylinder(radius=1.15,height=40); hl.apply_translation([38.5,98.0,0]); sub.append(hl)

body=trimesh.boolean.union(add,engine='manifold')
body=trimesh.boolean.difference([body]+sub,engine='manifold')
print("watertight:",body.is_watertight," bodies:",body.body_count," %.0f mm3 (~%.0f g PLA)"%(body.volume,body.volume*1.24e-3))
b=body.bounds; print("bbox X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f"%(b[0][0],b[1][0],b[0][1],b[1][1],b[0][2],b[1][2]))
n=body.face_normals; ar=body.area_faces; dn=n[:,2]<-1e-6
ang=np.degrees(np.arccos(np.clip(-n[dn][:,2],0,1)))   # 0 = flat ceiling, 90 = vertical
cb=body.triangles_center[dn]; bad=ang<45.0
bed=bad&(np.abs(cb[:,2]-Z0)<0.05); feet=bad&(cb[:,2]<1.0); other=bad&~bed&~feet
print("unsupported: baseplate underside (-> bed contact) %.0f mm2 | feet %.0f mm2 | GENUINE OVERHANG %.1f mm2"
      %(ar[dn][bed].sum(),ar[dn][feet].sum(),ar[dn][other].sum()))
body.export('E4_sensor_carrier_v3.stl'); print("exported E4_sensor_carrier_v3.stl")
