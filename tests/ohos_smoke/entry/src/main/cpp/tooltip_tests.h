#pragma once
#include "../../../../../../src/application.h"
#include "../../../../../../src/capi/view_c.h"
#include "../../../../../../src/foundation/handle_table.h"
#include "../../../../../../src/platform/ohos/ohos_views.h"
namespace nativeapi::tests {
struct TooltipJournal {
  std::shared_ptr<View> root, nest;
  std::shared_ptr<Button> primary, secondary;
  std::shared_ptr<Label> nested;
  std::shared_ptr<TextField> field;
  int passed = 0, failed = 0, clicks = 0;
  void Check(bool ok, const char* text) {
    (ok ? passed : failed)++;
    OH_LOG_Print(LOG_APP, ok ? LOG_INFO : LOG_ERROR, 1, "nativeapi-tooltip", "%{public}s %{public}s", ok ? "PASS" : "FAIL", text);
  }
  void Setup() {
    auto window = Application::GetInstance().GetPrimaryWindow();
    root = window->GetContentView(); root->ClearSubviews();
    root->SetLayout(ViewLayout::Absolute);
    auto title = std::make_shared<Label>("NativeAPI Tooltip Acceptance"); title->SetFrame({20,10,350,45}); root->AddSubview(title);
    primary = std::make_shared<Button>("NativeAPI Tooltip Primary"); primary->SetFrame({20,80,300,55});
    primary->SetTooltip("NativeAPI tip A 中文 🙂");
    primary->AddListener<ButtonClickedEvent>([this](const auto&) { ++clicks; }); root->AddSubview(primary);
    secondary = std::make_shared<Button>("NativeAPI Tooltip Secondary"); secondary->SetFrame({20,160,300,55}); root->AddSubview(secondary);
    auto c = HandleTable::GetInstance().Insert(secondary);
    native_view_set_tooltip(c, "NativeAPI tip B via C");
    auto text = native_view_get_tooltip(c);
    Check(text && std::string(text) == "NativeAPI tip B via C", "C tooltip getter preserves exact text");
    free_c_str(text); native_view_free(c);
    secondary->AddListener<ButtonClickedEvent>([this](const auto&) { ++clicks; });
    nest = std::make_shared<View>(); nest->SetFrame({20,240,300,55}); nest->SetLayout(ViewLayout::Column);
    nested = std::make_shared<Label>("NativeAPI Tooltip Nested"); nested->SetFlex(1); nested->SetTooltip("NativeAPI nested tip");
    nest->AddSubview(nested); root->AddSubview(nest);
    field = std::make_shared<TextField>("NativeAPI Tooltip Field"); field->SetFrame({20,320,300,65}); field->SetTooltip("NativeAPI field tip"); root->AddSubview(field);
    for (const auto& view : {std::static_pointer_cast<View>(primary), std::static_pointer_cast<View>(field)}) {
      auto node = static_cast<ArkUI_NodeHandle>(view->GetNativeObject());
      int32_t modern = -1;
      const int code = OH_ArkUI_NodeUtils_GetNodeUniqueId(node, &modern);
      const auto* legacy = ohos::NodeApi()->getAttribute(node, NODE_UNIQUE_ID);
      Check(code == 0 && modern >= 0 && ohos::NodeUniqueId(node) == modern && legacy && legacy->value &&
          legacy->size > 0 && legacy->value[0].i32 == modern, "modern SDK and public legacy popup target identity agree");
    }
    Check(primary->GetTooltip() == "NativeAPI tip A 中文 🙂" && secondary->GetTooltip() == "NativeAPI tip B via C", "C++ and C tooltip configuration applied");
  }
  void Command(const std::string& action) {
    if (action == "setup") { Setup(); return; }
    if (action == "update") primary->SetTooltip("NativeAPI tip A updated 更新 🙂");
    if (action == "clear") { auto c = HandleTable::GetInstance().Insert(primary); native_view_set_tooltip(c, nullptr); native_view_free(c); Check(!primary->GetTooltip(), "C null clears tooltip"); }
    if (action == "empty") { primary->SetTooltip(""); Check(primary->GetTooltip() == "", "empty tooltip value is retained"); }
    if (action == "restore") primary->SetTooltip("NativeAPI tip A 中文 🙂");
    if (action == "hide") primary->SetVisible(false);
    if (action == "show") primary->SetVisible(true);
    if (action == "disable") primary->SetEnabled(false);
    if (action == "enable") primary->SetEnabled(true);
    if (action == "hide-parent") nest->SetVisible(false);
    if (action == "show-parent") nest->SetVisible(true);
    if (action == "disable-parent") nest->SetEnabled(false);
    if (action == "enable-parent") nest->SetEnabled(true);
    if (action == "remove") root->RemoveSubview(primary);
    if (action == "readd") root->InsertSubview(1, primary);
    if (action == "multiline") { auto id = field->GetId(); field->SetMultiline(true); Check(field->GetId() == id && field->GetTooltip() == "NativeAPI field tip" && field->IsMultiline(), "native node replacement preserves view identity and tooltip"); field->SetTooltip("NativeAPI multiline tip"); }
    if (action == "destroy") { root->RemoveSubview(primary); primary.reset(); }
    if (action == "click-check") Check(clicks == 2, "real C++ and C-configured controls still receive exactly one click each");
  }
  std::string State() {
    std::ostringstream out;
    int32_t field_node = -1;
    if (field && field->GetNativeObject()) OH_ArkUI_NodeUtils_GetNodeUniqueId(static_cast<ArkUI_NodeHandle>(field->GetNativeObject()), &field_node);
    out << "{\"passed\":" << passed << ",\"failed\":" << failed << ",\"clicks\":" << clicks
        << ",\"primary\":" << (primary ? primary->GetId() : 0)
        << ",\"secondary\":" << (secondary ? secondary->GetId() : 0)
        << ",\"nested\":" << (nested ? nested->GetId() : 0)
        << ",\"field\":" << (field ? field->GetId() : 0) << ",\"fieldNode\":" << field_node << "}"; return out.str();
  }
};
inline TooltipJournal tooltip_journal;
inline napi_value TooltipAttach(napi_env env, napi_callback_info info) {
  size_t count = 3; napi_value args[3]{}; napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
  bool ok = count == 3 && ohos::AttachViewContent(env, args[0], args[1], args[2]);
  return ohos::Boolean(env, ok);
}
inline napi_value TooltipCommand(napi_env env, napi_callback_info info) {
  size_t count = 1; napi_value value = nullptr; napi_get_cb_info(env, info, &count, &value, nullptr, nullptr);
  if (count) tooltip_journal.Command(ohos::js::String(env, value));
  return ohos::String(env, tooltip_journal.State());
}
}  // namespace nativeapi::tests
