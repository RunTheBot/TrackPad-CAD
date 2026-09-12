#include "gesture.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace tpc;
void require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
Sample pair(double ms,double x=0,double y=0,bool shift=false) {return {ms,{{1,30+x,30+y},{2,50+x,30+y}},shift};}
int main() try {
    Engine e;
    e.feed(pair(0));require(e.tick(0).zero(),"touch down must not jump");
    e.feed(pair(8,1,2));auto f=e.tick(8);
    require(f.axes[3]==32 && f.axes[4]==-16 && f.axes[0]==0,"orbit mapping");
    require(e.tick(16).zero(),"delta must not latch");
    e.feed(pair(24,2,3,true));require(e.tick(24).zero(),"modifier transition must rebase");
    e.feed(pair(32,3,5,true));f=e.tick(32);
    require(f.axes[0]==14 && f.axes[1]==28 && f.axes[3]==0,"pan mapping");
    e.feed({40,{},false});require(e.tick(40).zero(),"lift releases");
    e.cancel();e.feed(pair(0));e.feed({8,{{1,29,30},{2,51,30}},false});f=e.tick(8);
    require(f.axes[2]<0 && f.axes[3]==0 && f.axes[4]==0,"symmetric pinch only zooms");
    e.cancel();e.feed(pair(0));e.feed({8,{{1,40,20},{2,40,40}},false});f=e.tick(8);
    require(f.axes[5]==350 && f.axes[2]==0,"twist rolls and clamps");
    e.cancel();e.feed(pair(0));e.feed(pair(8,1000,1000));f=e.tick(8);
    require(f.axes[3]==350 && f.axes[4]==-350,"axes saturate");
    e.feed(pair(16,1001,1001));require(e.tick(100).zero(),"stale motion discarded");
    e.cancel();e.feed(pair(0));e.feed({8,{{2,51,30},{1,31,30}},false});require(e.tick(8).axes[4]==-16,"contact order independent");
    e.feed({16,{{3,40,40},{4,60,40}},false});require(e.tick(16).zero(),"new identities rebase");
    e.cancel();e.feed({0,{{1,10,10},{2,20,10},{3,30,10}},false});e.feed({32,{},false});
    auto a=e.take_actions();require(a.size()==1 && a[0]==Action::frame_selection,"three finger tap");
    e.feed({40,{{1,10,10}},false});e.feed({64,{},false});e.feed({96,{{1,10,10}},false});e.feed({120,{},false});
    a=e.take_actions();require(a.size()==1 && a[0]==Action::pivot_at_cursor,"double tap");
    e.cancel();e.feed({0,{{1,10,10}},false});e.feed({8,{{1,20,10}},false});e.feed({16,{},false});
    e.feed({24,{{1,20,10}},false});e.feed({32,{},false});require(e.take_actions().empty(),"drag isn't a tap");
    e.cancel();e.feed(pair(0));e.feed(pair(8,1,1));e.cancel();require(e.tick(8).zero(),"focus cancellation releases");
    e.feed(pair(0));e.feed(pair(8,std::numeric_limits<double>::quiet_NaN()));require(e.tick(8).zero(),"invalid input releases");
    e.feed(pair(0));e.feed({8,{{1,30,30},{1,50,30}},false});require(e.tick(8).zero(),"duplicate IDs rejected");
    f={};f.axes[0]=-350;f.axes[5]=350;const auto r=motion_report(f);
    require(r[0]==1 && r[1]==0xa2 && r[2]==0xfe && r[11]==0x5e && r[12]==1,"HID little endian encoding");
    std::cout<<"Passed gesture, transition, timeout, tap, validation and HID encoding checks.\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
