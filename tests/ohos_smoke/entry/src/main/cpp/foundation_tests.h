#pragma once
#include "../../../../../foundation_value_checks.h"
#include "../../../../../../src/application.h"
#include "../../../../../../src/platform/ohos/ohos_napi.h"
#include <optional>

namespace nativeapi::tests {
struct FoundationWork {
  napi_async_work work = nullptr; napi_deferred deferred = nullptr;
  FoundationChecks checks;
  bool worker = false;
  void Run() {
    checks.failure=[](const char* text){ OH_LOG_Print(LOG_APP,LOG_ERROR,1,"nativeapi-foundation","FAIL %{public}s",text); };
    checks.Check(IsMainThread()!=worker,"actual UI or independent worker thread");
    checks.Run();
  }
  std::string Reply() const { return "{\"passed\":"+std::to_string(checks.passed)+",\"failed\":"+std::to_string(checks.failed)+"}"; }
};
inline napi_value FoundationValues(napi_env env, napi_callback_info info) {
  size_t count=1; napi_value arg=nullptr; napi_get_cb_info(env,info,&count,&arg,nullptr,nullptr);
  auto job=std::make_unique<FoundationWork>();
  if(count!=1 || napi_get_value_bool(env,arg,&job->worker)!=napi_ok) return nullptr;
  napi_value promise=nullptr; napi_create_promise(env,&job->deferred,&promise);
  if(!job->worker) {job->Run();napi_resolve_deferred(env,job->deferred,ohos::String(env,job->Reply()));return promise;}
  auto name=ohos::String(env,"nativeapi.foundation");
  if(napi_create_async_work(env,nullptr,name,
    [](napi_env,void* data){static_cast<FoundationWork*>(data)->Run();},
    [](napi_env env,napi_status status,void* data){
      std::unique_ptr<FoundationWork> job(static_cast<FoundationWork*>(data));
      if(status==napi_ok)napi_resolve_deferred(env,job->deferred,ohos::String(env,job->Reply()));
      else napi_reject_deferred(env,job->deferred,ohos::String(env,"foundation worker failed"));
      napi_delete_async_work(env,job->work);
    },job.get(),&job->work)!=napi_ok || napi_queue_async_work(env,job->work)!=napi_ok) {
      if(job->work)napi_delete_async_work(env,job->work);
      napi_reject_deferred(env,job->deferred,ohos::String(env,"foundation worker submission failed"));return promise;
    }
  job.release();return promise;
}
struct FoundationWindowJournal {
  std::optional<PositioningStrategy> strategy, copied;
  native_positioning_strategy_t c=0;
  std::weak_ptr<Window> weak;
  WindowId id=0;
  FoundationChecks checks;
  void Setup() {
    checks.failure=[](const char* text){ OH_LOG_Print(LOG_APP,LOG_ERROR,1,"nativeapi-foundation","FAIL %{public}s",text); };
    ohos::CreateSubWindow("NativeAPI Foundation Target","pages/ControlsTarget",[this](auto window){
      checks.Check(window!=nullptr,"real SDK child created"); if(!window)return;
      id=window->GetId();weak=window;
      strategy=PositioningStrategy::Relative(window,{3.25,-4.5});
      auto copy=*strategy; copied=std::move(copy);
      auto h=native_window_manager_get(id);
      c=native_positioning_strategy_relative_with_window_and_offset(h,{3.25,-4.5});
      native_window_free(h);
      checks.Check(strategy->GetRelativeWindow()==window && copied->GetRelativeWindow()==window,"copied and moved strategies preserve wrapper identity");
      window->SetBounds({120,180,240,160});window->Show();
    });
  }
  static void Rect(std::ostringstream& out,Rectangle r) {out<<'['<<r.x<<','<<r.y<<','<<r.width<<','<<r.height<<']';}
  std::string State(const std::string& action) {
    if(action=="setup")Setup();
    if(action=="release" && strategy && copied && c) {
      auto owner=native_positioning_strategy_get_relative_window(c);
      checks.Check(owner && native_window_get_id(owner)==id,"C relative getter returns owning canonical window handle");
      auto window=strategy->GetRelativeWindow();
      checks.Check(window && !window->GetNativeObject() && !WindowManager::GetInstance().Get(id),"actual SDK destruction invalidates native wrapper and registry");
      checks.Check(FoundationChecks::RectIs(strategy->GetRelativeRectangle(),0,0,0,0) &&
        FoundationChecks::RectIs(to_cpp_rectangle(native_positioning_strategy_get_relative_rectangle(c)),0,0,0,0),"closed target returns empty C++ and C geometry");
      window.reset(); strategy.reset();copied.reset();native_positioning_strategy_free(c);c=0;
      checks.Check(!weak.expired() && native_window_get_id(owner)==id,"owning C getter survives release of all strategies");
      const auto references=weak.use_count();
      native_window_free(owner);
      checks.Check(weak.use_count()==references-1,"C getter releases exactly its own reference; SDK callbacks may retain others");
      // This wrapper has no native registry or SDK promise captures, so its final
      // release is deterministic independently of the JS garbage collector.
      auto isolated=std::make_shared<Window>();
      std::weak_ptr<Window> lifetime=isolated;
      std::optional<PositioningStrategy> value=PositioningStrategy::Relative(isolated);
      auto h=HandleTable::GetInstance().Insert(isolated);
      auto config=native_positioning_strategy_relative_with_window_and_offset(h,{0,0});
      native_window_free(h);isolated.reset();
      checks.Check(!lifetime.expired() && value->GetRelativeWindow()!=nullptr,"strategies retain released original shared wrapper");
      auto held=native_positioning_strategy_get_relative_window(config);
      value.reset();native_positioning_strategy_free(config);
      checks.Check(!lifetime.expired() && HandleTable::GetInstance().Resolve<Window>(held)==lifetime.lock(),"owning C getter alone retains isolated wrapper");
      native_window_free(held);
      checks.Check(lifetime.expired(),"isolated wrapper destroyed after final owning getter release");
    }
    std::ostringstream out;out<<"{\"ready\":"<<(strategy&&copied&&c?"true":"false")<<",\"passed\":"<<checks.passed<<",\"failed\":"<<checks.failed;
    if(strategy && copied && c) {
      auto window=strategy->GetRelativeWindow();auto owner=native_positioning_strategy_get_relative_window(c);
      checks.Check(window && native_window_get_id(owner)==id,"live C and C++ getter identities");native_window_free(owner);
      out<<",\"cpp\":";Rect(out,strategy->GetRelativeRectangle());out<<",\"copy\":";Rect(out,copied->GetRelativeRectangle());
      out<<",\"c\":";Rect(out,to_cpp_rectangle(native_positioning_strategy_get_relative_rectangle(c)));
      auto offset=strategy->GetRelativeOffset();auto co=native_positioning_strategy_get_relative_offset(c);
      out<<",\"offset\":["<<offset.x<<','<<offset.y<<','<<co.x<<','<<co.y<<']';
    }
    out<<'}';return out.str();
  }
};
inline FoundationWindowJournal foundation_window;
inline napi_value FoundationWindow(napi_env env,napi_callback_info info) {
  size_t count=1;napi_value arg=nullptr;napi_get_cb_info(env,info,&count,&arg,nullptr,nullptr);
  return ohos::String(env,foundation_window.State(count?ohos::js::String(env,arg):""));
}
} // namespace nativeapi::tests
