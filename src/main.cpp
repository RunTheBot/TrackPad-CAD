#include "gesture.hpp"
#include "windows.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static void print(double ms,const tpc::Frame& f) {
    std::cout<<ms; for(auto a:f.axes) std::cout<<","<<a;
    std::cout<<","<<f.buttons<<"\n";
}
int capture_main(int argc,char** argv) try {
    if(argc==3 && std::string(argv[1])=="replay") {
        std::ifstream in(argv[2]); if(!in) throw std::runtime_error("Cannot open replay file");
        tpc::Engine engine; std::string line; double last=-1, next=0;
        std::cout<<"ms,Tx,Ty,Tz,Rx,Ry,Rz,buttons\n";
        while(std::getline(in,line)) {
            if(line.empty() || line[0]=='#') continue;
            std::istringstream row(line); tpc::Sample s; int shift; size_t n;
            if(!(row>>s.ms>>shift>>n) || !std::isfinite(s.ms) || s.ms<0 || s.ms<last || n>10 || (shift!=0 && shift!=1)) throw std::runtime_error("Invalid replay header");
            s.shift=shift!=0;
            for(size_t i=0;i<n;++i) {tpc::Contact c; if(!(row>>c.id>>c.x>>c.y) || !std::isfinite(c.x) || !std::isfinite(c.y)) throw std::runtime_error("Invalid contact"); s.contacts.push_back(c);}
            std::string extra; if(row>>extra) throw std::runtime_error("Unexpected replay field");
            while(next<s.ms) {print(next,engine.tick(next)); next+=8;}
            engine.feed(s);
            for(auto a:engine.take_actions()) std::cerr<<s.ms<<": "<<(a==tpc::Action::frame_selection ? "frame_selection (bind button 1 in CAD profile)" : "pivot_at_cursor (requires CAD adapter)")<<"\n";
            last=s.ms;
        }
        for(;next<=last+88;next+=8) print(next,engine.tick(next));
        return 0;
    }
#ifdef _WIN32
    if(argc==2 && std::string(argv[1])=="devices") return tpc::devices();
    if(argc>=3 && argc<=4 && std::string(argv[1])=="live") {
        const std::string option=argc==4 ? argv[3] : "";
        if(!option.empty() && option!="--driver" && option!="--stream") throw std::runtime_error("Unknown live option");
        std::string s=argv[2]; return tpc::live(std::wstring(s.begin(),s.end()),option=="--driver",option=="--stream");
    }
    if(argc==3 && std::string(argv[1])=="rotation-test") {
        std::string s=argv[2]; return tpc::rotation_test(std::wstring(s.begin(),s.end()));
    }
#endif
    std::cout<<"TrackPad CAD experimental prototype\n"
      "  trackpad-cad devices\n"
      "  trackpad-cad replay examples/gestures.txt\n"
      "  trackpad-cad live target.exe [--driver]\n"
      "  trackpad-cad rotation-test target.exe\n"
      "Live: hold F8 while the named CAD process is foreground. Ctrl+C quits.\n"
      "Default live output is a diagnostic trace. --driver requires the test VHF driver.\n";
    return argc==1 ? 0 : 2;
} catch(const std::exception& e) {std::cerr<<"Error: "<<e.what()<<"\n"; return 1;}

#ifndef TPC_CAPTURE_LIBRARY
int main(int argc,char** argv){return capture_main(argc,argv);}
#endif
