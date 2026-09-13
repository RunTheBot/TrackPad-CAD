#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace tpc {
struct Contact { unsigned id; double x, y; }; // millimetres
struct Sample { double ms; std::vector<Contact> contacts; bool shift = false; };
enum class Action { frame_selection, pivot_at_cursor };
struct Frame {
    std::array<int16_t, 6> axes{};
    uint32_t buttons = 0;
    bool zero() const { return buttons == 0 && std::all_of(axes.begin(), axes.end(), [](auto a){return a == 0;}); }
};
struct Settings {
    double orbit = 16, pan = 14, zoom = 600, roll = 400;
    double tap_slop_mm = 2, tap_ms = 220, double_tap_ms = 320;
    double stale_ms = 80;
};
class Engine {
    Settings cfg;
    std::optional<Sample> prev;
    std::vector<Contact> starts;
    std::array<double,6> pending{};
    std::vector<Action> actions;
    double down = 0, last = -1, last_tap = -10000;
    std::optional<Contact> tap_position;
    size_t peak = 0;
    bool tap_ok = false;
    static double distance(const Contact& a, const Contact& b) { return std::hypot(a.x-b.x,a.y-b.y); }
    void clear_motion() { pending.fill(0); }
public:
    explicit Engine(Settings s = {}) : cfg(s) {}
    void cancel() {
        prev.reset(); starts.clear(); peak = 0; tap_ok = false; last = -1;
        last_tap = -10000; tap_position.reset(); clear_motion(); actions.clear();
    }
    void feed(Sample s) {
        if (!std::isfinite(s.ms) || s.ms < 0 || s.contacts.size() > 10) { cancel(); return; }
        for (auto c : s.contacts) if (!std::isfinite(c.x) || !std::isfinite(c.y)) { cancel(); return; }
        std::sort(s.contacts.begin(), s.contacts.end(), [](auto a,auto b){return a.id < b.id;});
        for(size_t i=1;i<s.contacts.size();++i) if(s.contacts[i].id==s.contacts[i-1].id) {cancel();return;}
        if (last >= 0 && (s.ms < last || s.ms-last > cfg.stale_ms)) cancel();
        last = s.ms;
        const auto n = s.contacts.size();
        if (n && (!prev || prev->contacts.empty())) {
            down = s.ms; starts = s.contacts; peak = n; tap_ok = true;
        }
        peak = std::max(peak,n);
        for(auto c : s.contacts) {
            auto it = std::find_if(starts.begin(),starts.end(),[&](auto a){return a.id==c.id;});
            if (it==starts.end()) starts.push_back(c);
            else if(distance(*it,c)>cfg.tap_slop_mm) tap_ok=false;
        }
        if(n==0 && prev && !prev->contacts.empty()) {
            if(tap_ok && s.ms-down<=cfg.tap_ms) {
                if(peak==3) {actions.push_back(Action::frame_selection); last_tap=-10000;}
                else if(peak==1) {
                    const auto p=starts.front();
                    if(s.ms-last_tap<=cfg.double_tap_ms && tap_position && distance(p,*tap_position)<=cfg.tap_slop_mm*2) {
                        actions.push_back(Action::pivot_at_cursor); last_tap=-10000; tap_position.reset();
                    } else {last_tap=s.ms; tap_position=p;}
                } else last_tap=-10000;
            } else last_tap=-10000;
            clear_motion(); starts.clear(); peak=0;
        }
        bool same = prev && n==2 && prev->contacts.size()==2 && s.shift==prev->shift;
        if(same) for(size_t i=0;i<2;++i) same= same && s.contacts[i].id==prev->contacts[i].id;
        if(same) {
            const auto a=prev->contacts[0], b=prev->contacts[1], c=s.contacts[0], d=s.contacts[1];
            const double dx=(c.x+d.x-a.x-b.x)/2, dy=(c.y+d.y-a.y-b.y)/2;
            // Axis mapping (Frame.axes indices):
            // [0]=Tx, [1]=Ty, [2]=Tz(zoom), [3]=Rx(tilt up/down), [4]=Ry(tilt left/right), [5]=Rz(roll)
            if(s.shift) {
                // Shift + drag: Pan (Tx, Ty)
                pending[0]+=dx*cfg.pan;      // Tx: horizontal -> pan X
                pending[2]+=dy*cfg.pan;      // Ty: vertical   -> pan Y (inverted)
            } else {
                // No shift + drag: Orbit (Rx, Ry) - currently maps vertical to tilt up/down
                pending[3]+=dy*cfg.orbit;    // Rx: vertical   -> tilt up/down
                pending[5]-=dx*cfg.orbit;    // Ry: horizontal -> tilt left/right
                // TO REMAP: swap pending[3] and pending[4] assignments, or change dy/dx sources
                // e.g., for vertical -> tilt left/right: pending[4]+=dy*cfg.orbit;
            }
            const double old_span=distance(a,b), new_span=distance(c,d);
            if(old_span>2 && new_span>2) {
                pending[1]+=std::log(new_span/old_span)*cfg.zoom;  // Tz: pinch -> zoom
                pending[4]-=std::remainder(std::atan2(d.y-c.y,d.x-c.x)-std::atan2(b.y-a.y,b.x-a.x), 2*3.141592653589793)*cfg.roll; // Rz: rotate -> roll
            }
        } else clear_motion(); // never jump on contact or modifier transitions
        prev=std::move(s);
    }
    Frame tick(double now) {
        Frame f;
        if(last<0 || !std::isfinite(now) || now<last || now-last>cfg.stale_ms) {cancel(); return f;}
        for(size_t i=0;i<6;++i) f.axes[i]=static_cast<int16_t>(std::lround(std::clamp(pending[i],-350.0,350.0)));
        clear_motion(); // one scheduler interval per delta; next tick releases
        return f;
    }
    std::vector<Action> take_actions() {auto a=std::move(actions); actions.clear(); return a;}
};
inline std::array<uint8_t,13> motion_report(const Frame& f) {
    std::array<uint8_t,13> r{}; r[0]=1;
    for(size_t i=0;i<6;++i) {const auto a=static_cast<uint16_t>(f.axes[i]); r[1+2*i]=a&255; r[2+2*i]=a>>8;}
    return r;
}
}
