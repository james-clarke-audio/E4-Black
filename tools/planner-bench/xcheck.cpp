// Does native-with-diagonals-off agree with the orthogonal planner?
// If it does, the firmware need only carry one of them.
#include "planner.h"
#include "diagonal.h"
#include "native.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using namespace plan;
struct MF { uint8_t c[16][16]={};
  bool load(const char*p){FILE*f=fopen(p,"rb");if(!f)return false;uint8_t b[256];
    if(fread(b,1,256,f)!=256){fclose(f);return false;}fclose(f);
    for(int x=0;x<16;x++)for(int y=0;y<16;y++)c[x][y]=b[x*16+y];return true;}
  static bool fn(const void*ctx,int x,int y,int h){const MF*m=(const MF*)ctx;
    if(x<0||x>15||y<0||y>15)return false;return (m->c[x][y]&(1<<h))==0;}
  WallReader rd()const{return WallReader{&MF::fn,this};} };
static Route A,B;
int main(int argc,char**argv){
  Robot r; DiagTurns off; off.enabled=false;
  int n=0,same=0,diff=0; double worst=0; std::string worstm;
  for(int i=1;i<argc;i++){
    MF m; if(!m.load(argv[i])) continue;
    plan_route (A, m.rd(), r,      QUICKEST, 0,0,NN, 7,7,2,2);
    plan_native(B, m.rd(), r, off, QUICKEST, 0,0,NN, 7,7,2,2);
    if(!A.ok||!B.ok) continue;
    ++n;
    const double d = B.seconds - A.seconds;
    if (A.cells==B.cells && A.turns==B.turns && A.spins==B.spins) ++same; else ++diff;
    if (d>worst){worst=d; worstm=argv[i];}
  }
  printf("%d mazes compared\n  identical shape : %d\n  differing shape : %d\n", n, same, diff);
  printf("  worst native-minus-orthogonal time: %+.3f s  %s\n", worst,
         worstm.empty()?"-":worstm.substr(worstm.find_last_of('/')+1).c_str());
  return 0;
}
