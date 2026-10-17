#pragma once
// Reused unchanged by host CTest and real OHOS UI/worker executions.
#include "../src/foundation/color.h"
#include "../src/foundation/keyboard.h"
#include "../src/foundation/event_request_dispatch.h"
#include "../src/foundation/handle_table.h"
#include "../src/positioning_strategy.h"
#include "../src/window_shape.h"
#include "../src/window_shadow.h"
#include "../src/capi/color_c.h"
#include "../src/capi/geometry_c.h"
#include "../src/capi/keyboard_c.h"
#include "../src/capi/placement_c.h"
#include "../src/capi/positioning_strategy_c.h"
#include "../src/capi/window_shape_c.h"
#include "../src/capi/window_shadow_c.h"
#include "../src/capi/event_request_c.h"
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace nativeapi::tests {
struct FoundationChecks {
  int passed = 0, failed = 0;
  std::function<void(const char*)> failure;
  void Check(bool ok, const char* label) {
    if (ok) ++passed;
    else { ++failed; if (failure) failure(label); }
  }
  static bool PointIs(Point p, double x, double y) { return p.x == x && p.y == y; }
  static bool RectIs(Rectangle r, double x, double y, double w, double h) {
    return r.x == x && r.y == y && r.width == w && r.height == h;
  }
  void Run() {
    // Explicit RGBA expectations exercise all four formats, hash/case and alpha.
    struct HexCase { const char* text; unsigned int rgba; };
    for (const auto& c : std::vector<HexCase>{{"#1aF",0x11aaffff}, {"1aF8",0x11aaff88},
      {"#123aBc",0x123abcff}, {"123aBc80",0x123abc80}, {"000",0x000000ff},
      {"#0000",0}, {"fff",0xffffffff}, {"#FFffFF00",0xffffff00}}) {
      Check(Color::FromHex(c.text).ToRGBA() == c.rgba, "C++ hex channels");
      Check(native_color_to_rgba(native_color_from_hex(c.text)) == c.rgba, "C hex channels");
    }
    for (const char* text : {static_cast<const char*>(nullptr), "", "#", "0", "00", "12345", "1234567",
      "123456789", "12345678901234567890", "#GGG", "#12z", "12 4", "#12+456", "##123", " 123", "123 ", "0x1234", "#1234567Z"}) {
      bool threw = false; Color value{};
      try { value = Color::FromHex(text); } catch (...) { threw = true; }
      Check(!threw && value.ToRGBA() == 0, "invalid C++ color returns transparent without throwing");
      Check(native_color_to_rgba(native_color_from_hex(text)) == 0, "invalid C color returns transparent");
    }
    Check(Color::FromRGBA(0x12,0x34,0x56,0x78).ToARGB() == 0x78123456 &&
      Color::FromRGBA(1,2,3).ToRGBA() == 0x010203ff, "RGBA constructor defaults and ARGB packing");
    Check(native_color_to_argb(native_color_from_rgba(0x12,0x34,0x56,0x78)) == 0x78123456, "C ARGB packing");
    const Color colors[]{Color::Transparent,Color::Black,Color::White,Color::Red,Color::Green,
      Color::Blue,Color::Yellow,Color::Cyan,Color::Magenta};
    const native_color_t ccolors[]{NATIVE_COLOR_TRANSPARENT,NATIVE_COLOR_BLACK,NATIVE_COLOR_WHITE,NATIVE_COLOR_RED,
      NATIVE_COLOR_GREEN,NATIVE_COLOR_BLUE,NATIVE_COLOR_YELLOW,NATIVE_COLOR_CYAN,NATIVE_COLOR_MAGENTA};
    const unsigned int packed[]{0,0x000000ff,0xffffffff,0xff0000ff,0x00ff00ff,0x0000ffff,0xffff00ff,0x00ffffff,0xff00ffff};
    for (size_t i=0;i<9;++i) Check(colors[i].ToRGBA()==packed[i] && native_color_to_rgba(ccolors[i])==packed[i], "named color constants");
    auto all = EdgeInsets::All(2.5); auto symmetric = native_edge_insets_symmetric(3.25,7.5);
    Check(all.top==2.5 && all.right==2.5 && all.bottom==2.5 && all.left==2.5, "equal insets");
    Check(symmetric.top==3.25 && symmetric.bottom==3.25 && symmetric.right==7.5 && symmetric.left==7.5, "C symmetric insets order");
    auto cs=to_c_edge_insets(EdgeInsets::Symmetric(3.25,7.5)); auto ca=native_edge_insets_all(-1);
    Check(cs.top==symmetric.top && cs.left==symmetric.left && ca.bottom==-1 && ca.right==-1, "C++ symmetric and C all");
    Check(PointIs(to_cpp_point(to_c_point({-10.25,30.5})),-10.25,30.5) &&
      RectIs(to_cpp_rectangle(to_c_rectangle({-1.25,2.5,300.75,200.5})),-1.25,2.5,300.75,200.5), "fractional signed coordinates survive ABI");
    auto size=to_cpp_size(to_c_size({120.25,80.5})); Check(size.width==120.25 && size.height==80.5, "size conversion");
    const Placement placements[]{Placement::Top,Placement::TopStart,Placement::TopEnd,Placement::Right,Placement::RightStart,
      Placement::RightEnd,Placement::Bottom,Placement::BottomStart,Placement::BottomEnd,Placement::Left,Placement::LeftStart,Placement::LeftEnd};
    for (int i=0;i<12;++i) Check(static_cast<int>(to_c_placement(placements[i]))==i &&
      to_cpp_placement(static_cast<native_placement_t>(i))==placements[i], "placement enum identity (not rendered placement)");
    auto flags=ModifierKey::Ctrl | ModifierKey::Alt; flags|=ModifierKey::Shift;
    Check((flags & ModifierKey::Ctrl)==ModifierKey::Ctrl && (flags & ModifierKey::Meta)==ModifierKey::None, "modifier OR AND assignment");
    bool all_flags=true;
    for (unsigned int i=0;i<256;++i) {
      KeyboardAccelerator a("F9", static_cast<ModifierKey>(i));
      auto c=to_c_keyboard_accelerator(a);
      all_flags = all_flags && static_cast<unsigned int>(c.modifiers)==i && to_cpp_keyboard_accelerator(c)==a;
      native_keyboard_accelerator_free(&c);
    }
    Check(all_flags, "all 256 modifier masks survive owned C accelerator roundtrip");
    KeyboardAccelerator a("F9",flags), empty;
    Check(!a.IsEmpty() && empty.IsEmpty() && empty.ToString().empty() && a.ToString()=="Ctrl+Alt+Shift+F9", "accelerator constructor empty/order");
    Check(a==KeyboardAccelerator("F9",flags) && a!=KeyboardAccelerator("F8",flags) &&
      a!=KeyboardAccelerator("F9",ModifierKey::Ctrl), "accelerator equality key and modifiers");
    auto ac=to_c_keyboard_accelerator(a); auto text=native_keyboard_accelerator_to_string(ac);
    Check(text && std::string(text)=="Ctrl+Alt+Shift+F9" && !native_keyboard_accelerator_is_empty(ac), "C accelerator rendering");
    free_c_str(text); native_keyboard_accelerator_free(&ac);
    native_keyboard_accelerator_t ec{}; Check(native_keyboard_accelerator_is_empty(ec), "null C accelerator key is empty");
    // Value-object builder validation, copy independence and hard limits.
    WindowShape shape;
    Check(shape.GetPointCount()==0 && PointIs(shape.GetPointAt(0),0,0), "empty polygon");
    Check(shape.AddPoint({0,0}) && shape.AddPoint({16384,16384}) && shape.AddPoint({1.25,2.5}), "polygon finite fractional bounds");
    const double nan=std::numeric_limits<double>::quiet_NaN(), inf=std::numeric_limits<double>::infinity();
    for (Point p : {Point{-1,0},Point{0,-1},Point{16384.1,0},Point{0,16384.1},Point{nan,0},Point{0,inf}})
      Check(!shape.AddPoint(p) && shape.GetPointCount()==3, "invalid vertex preserves polygon");
    WindowShape copied=shape; shape.Clear(); auto moved=std::move(copied);
    Check(shape.GetPointCount()==0 && moved.GetPointCount()==3 && PointIs(moved.GetPointAt(2),1.25,2.5) &&
      PointIs(moved.GetPointAt(99),0,0), "polygon copy move and missing point");
    bool capacity=true; for(int i=0;i<4096;++i) capacity=shape.AddPoint({1,2}) && capacity;
    Check(capacity && !shape.AddPoint({1,2}) && shape.GetPointCount()==4096, "polygon exact vertex capacity");
    auto sc=native_window_shape_create();
    Check(sc && native_window_shape_add_point(sc,{2.25,3.5}) && native_window_shape_get_point_count(sc)==1 &&
      PointIs(to_cpp_point(native_window_shape_get_point_at(sc,0)),2.25,3.5), "C polygon public methods");
    native_window_shape_clear(sc); Check(native_window_shape_get_point_count(sc)==0, "C polygon clear");
    native_window_shape_free(sc); Check(!native_window_shape_add_point(sc,{0,0}) && native_window_shape_get_point_count(sc)==0, "stale polygon handle");
    WindowShadow shadow;
    Check(shadow.GetColor().ToRGBA()==0x0000004d && shadow.GetBlurRadius()==18 && PointIs(shadow.GetOffset(),0,6), "default shadow configuration");
    shadow.SetColor({10,20,30,40});
    Check(shadow.SetBlurRadius(0) && shadow.SetBlurRadius(64) && shadow.SetOffset({-64,64}), "shadow inclusive bounds");
    for (double radius : {-1.0,64.01,nan,inf}) Check(!shadow.SetBlurRadius(radius) && shadow.GetBlurRadius()==64, "invalid radius preserves shadow");
    for (Point offset : {Point{-64.1,0},Point{0,64.1},Point{nan,0},Point{0,inf}})
      Check(!shadow.SetOffset(offset) && PointIs(shadow.GetOffset(),-64,64), "invalid shadow offset preserves value");
    auto shadow_copy=shadow; shadow.SetColor(Color::White); shadow.SetBlurRadius(1); shadow.SetOffset({1,1});
    Check(shadow_copy.GetColor().ToRGBA()==0x0a141e28 && shadow_copy.GetBlurRadius()==64 &&
      PointIs(shadow_copy.GetOffset(),-64,64), "shadow copy independent");
    auto sh=native_window_shadow_create(); native_window_shadow_set_color(sh,{10,20,30,40});
    Check(native_window_shadow_set_blur_radius(sh,2.5) && native_window_shadow_set_offset(sh,{-1.5,3.25}) &&
      native_color_to_rgba(native_window_shadow_get_color(sh))==0x0a141e28 && native_window_shadow_get_blur_radius(sh)==2.5 &&
      PointIs(to_cpp_point(native_window_shadow_get_offset(sh)),-1.5,3.25), "C shadow configuration methods");
    Check(!native_window_shape_add_point(sh,{1,2}) && native_window_shape_get_point_count(sh)==0, "cross-type shape handle refused");
    native_window_shadow_free(sh); Check(!native_window_shadow_set_blur_radius(sh,1), "stale shadow handle refused");
    auto absolute=PositioningStrategy::Absolute({-20.5,80.25}); auto cursor=PositioningStrategy::CursorPosition();
    Check(absolute.GetType()==PositioningStrategy::Type::Absolute && PointIs(absolute.GetAbsolutePosition(),-20.5,80.25) &&
      !absolute.GetRelativeWindow(), "absolute strategy");
    Check(cursor.GetType()==PositioningStrategy::Type::CursorPosition && PointIs(cursor.GetRelativeOffset(),0,0), "cursor strategy value (not cursor query)");
    auto relative=PositioningStrategy::Relative(Rectangle{1.25,2.5,100.75,80.5},{3.25,-4.5});
    auto relative_copy=relative; relative=absolute; auto relative_moved=std::move(relative_copy);
    Check(relative_moved.GetType()==PositioningStrategy::Type::Relative && RectIs(relative_moved.GetRelativeRectangle(),1.25,2.5,100.75,80.5) &&
      PointIs(relative_moved.GetRelativeOffset(),3.25,-4.5), "rectangle strategy copy and move");
    auto null_window=PositioningStrategy::Relative(std::shared_ptr<Window>{});
    Check(!null_window.GetRelativeWindow() && RectIs(null_window.GetRelativeRectangle(),0,0,0,0), "empty shared window strategy safe");
    auto ap=native_positioning_strategy_absolute({-20.5,80.25}); auto cp=native_positioning_strategy_cursor_position();
    auto rp=native_positioning_strategy_relative_with_rect_and_offset({1.25,2.5,100.75,80.5},{3.25,-4.5});
    auto np=native_positioning_strategy_relative_with_window_and_offset(0,{2,3});
    Check(ap && cp && rp && np && native_positioning_strategy_get_type(cp)==NATIVE_POSITIONING_STRATEGY_TYPE_CURSOR_POSITION &&
      PointIs(to_cpp_point(native_positioning_strategy_get_absolute_position(ap)),-20.5,80.25) &&
      RectIs(to_cpp_rectangle(native_positioning_strategy_get_relative_rectangle(rp)),1.25,2.5,100.75,80.5) &&
      PointIs(to_cpp_point(native_positioning_strategy_get_relative_offset(rp)),3.25,-4.5), "C strategy factories and getters");
    Check(!native_positioning_strategy_get_relative_window(np) &&
      RectIs(to_cpp_rectangle(native_positioning_strategy_get_relative_rectangle(np)),0,0,0,0), "null C window yields empty strategy");
    for(auto h:{ap,cp,rp,np}) native_positioning_strategy_free(h);
    Check(!native_positioning_strategy_get_relative_window(rp) &&
      RectIs(to_cpp_rectangle(native_positioning_strategy_get_relative_rectangle(rp)),0,0,0,0), "stale strategy handles safe");
    Events();
  }
  void Events() {
    using D=detail::EventRequestDispatch;
    auto& table=HandleTable::GetInstance();
    for (int mode=0;mode<7;++mode) {
      int calls=0; bool allowed=false;
      auto request=D::Create(mode!=1,[&](bool result){++calls;allowed=result;});
      const auto h=table.Insert(request);
      Check(request->IsPending() && native_event_request_is_pending(h) &&
        native_event_request_is_cancelable(h)==(mode!=1), "event request initial shared state");
      std::shared_ptr<EventDecision> vote; native_event_decision_t cv=0;
      if(mode==1) Check(!request->Cancel() && !request->Defer() && !native_event_request_cancel(h) && !native_event_request_defer(h), "required event cannot be delayed or canceled");
      if(mode>=2) { vote=request->Defer(); cv=native_event_request_defer(h);
        Check(vote && cv && vote->IsPending() && native_event_decision_is_pending(cv), "C++ and C deferred votes pending"); }
      if(mode==2) Check(request->Cancel() && native_event_request_is_cancelled(h) && !request->Cancel(), "synchronous veto visible to C");
      D::Finish(request);
      if(mode==3) { Check(calls==0 && vote->Accept() && calls==0 && native_event_decision_accept(cv), "deferred acceptance waits for every vote"); }
      if(mode==4) { Check(native_event_decision_cancel(cv) && !vote->Accept(), "C deferred veto wins"); }
      if(mode==5) { vote.reset(); Check(request->IsCancelled() && !native_event_decision_accept(cv), "dropped unresolved vote cancels"); }
      if(mode==6) { D::Invalidate(request); Check(!vote->Accept() && !native_event_decision_cancel(cv) && calls==0, "invalidated request rejects late votes without callback"); }
      Check(calls==(mode==6?0:1) && (mode>=2 || allowed) && (mode<2 || mode==3 || !allowed) && !request->IsPending(), "completion once with correct result");
      Check(!native_event_request_cancel(h) && !native_event_request_defer(h), "late request replies refused");
      native_event_decision_free(cv); vote.reset(); native_event_request_free(h);
      Check(!native_event_request_is_pending(h), "released C request safe");
    }
    std::atomic<int> calls{0}, accepts{0};
    auto request=D::Create(true,[&](bool allowed){if(allowed)++accepts;++calls;});
    std::vector<std::shared_ptr<EventDecision>> votes;
    for(int i=0;i<16;++i) votes.push_back(request->Defer());
    D::Finish(request); std::vector<std::thread> threads;
    for(auto vote:votes) threads.emplace_back([vote]{vote->Accept();});
    for(auto& t:threads)t.join();
    Check(calls==1 && accepts==1 && !request->IsPending(), "concurrent accepting voters complete once");
    bool reentered=false;
    std::shared_ptr<EventRequest> reentrant;
    reentrant=D::Create(true,[&](bool){reentered=!reentrant->IsPending() && !reentrant->Cancel();});
    D::Finish(reentrant); Check(reentered,"completion runs outside request mutex");
  }
};
} // namespace nativeapi::tests
