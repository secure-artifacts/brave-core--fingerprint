/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/fingerprint_test/fingerprint_test_ui.h"

#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/browser/ui/webui/brave_webui_source.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "brave/components/fingerprint_browser/resources/grit/fingerprint_test_generated_map.h"
#include "brave/components/fingerprint_browser/resources/grit/fingerprint_test_resources.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"

namespace {

base::ListValue ToListValue(const std::vector<std::string>& values) {
  base::ListValue list;
  for (const auto& value : values) {
    list.Append(value);
  }
  return list;
}

base::DictValue BuildPersonaSummary(const fingerprint_browser::Persona& persona,
                                    const Profile& profile) {
  base::DictValue summary;
  summary.Set("personaId", persona.persona_id);
  summary.Set("os",
              std::string(fingerprint_browser::PersonaOSToString(persona.os)));
  summary.Set("profile", profile.GetPath().BaseName().AsUTF8Unsafe());
  summary.Set("userAgent", persona.user_agent);
  summary.Set("platform", persona.platform);
  summary.Set("uaPlatform", persona.ua_metadata.platform);
  summary.Set("hardwareConcurrency", persona.hardware_concurrency);
  summary.Set("deviceMemory", persona.device_memory_gb);
  summary.Set("maxTouchPoints", persona.max_touch_points);
  summary.Set("languages", ToListValue(persona.languages));

  base::DictValue screen;
  screen.Set("width", persona.screen.width);
  screen.Set("height", persona.screen.height);
  screen.Set("availWidth", persona.screen.avail_width);
  screen.Set("availHeight", persona.screen.avail_height);
  screen.Set("colorDepth", persona.screen.color_depth);
  summary.Set("screen", std::move(screen));

  base::DictValue webgl;
  webgl.Set("vendor", persona.webgl.vendor);
  webgl.Set("renderer", persona.webgl.renderer);
  summary.Set("webgl", std::move(webgl));
  return summary;
}

constexpr char kReadFingerprintScript[] = R"JS((() => {
  const canvas = document.createElement('canvas');
  const gl = canvas.getContext('webgl');
  let webgl = null;
  if (gl) {
    const debug = gl.getExtension('WEBGL_debug_renderer_info');
    webgl = debug ? {
      vendor: String(gl.getParameter(debug.UNMASKED_VENDOR_WEBGL)),
      renderer: String(gl.getParameter(debug.UNMASKED_RENDERER_WEBGL)),
    } : {
      vendor: String(gl.getParameter(gl.VENDOR)),
      renderer: String(gl.getParameter(gl.RENDERER)),
    };
  }
  return JSON.stringify({
    url: location.href,
    fingerprint: {
      userAgent: navigator.userAgent,
      platform: navigator.platform,
      uaPlatform: navigator.userAgentData?.platform || '',
      hardwareConcurrency: navigator.hardwareConcurrency,
      deviceMemory: navigator.deviceMemory ?? null,
      maxTouchPoints: navigator.maxTouchPoints,
      languages: Array.from(navigator.languages),
      screen: {
        width: screen.width,
        height: screen.height,
        availWidth: screen.availWidth,
        availHeight: screen.availHeight,
        colorDepth: screen.colorDepth,
      },
      webgl,
      timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
      uaData: navigator.userAgentData || null,
    },
  });
})())JS";

content::WebContents* FindLastWebPage(content::WebContents* web_ui_contents) {
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          web_ui_contents);
  if (!browser) {
    return nullptr;
  }

  TabStripModel* tabs = browser->GetTabStripModel();
  for (int index = tabs->count() - 1; index >= 0; --index) {
    content::WebContents* contents = tabs->GetWebContentsAt(index);
    if (contents != web_ui_contents &&
        contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS()) {
      return contents;
    }
  }
  return nullptr;
}

class FingerprintTestMessageHandler : public content::WebUIMessageHandler {
 public:
  FingerprintTestMessageHandler() = default;
  FingerprintTestMessageHandler(const FingerprintTestMessageHandler&) = delete;
  FingerprintTestMessageHandler& operator=(
      const FingerprintTestMessageHandler&) = delete;
  ~FingerprintTestMessageHandler() override = default;

  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "getLastWebPageFingerprint",
        base::BindRepeating(
            &FingerprintTestMessageHandler::HandleGetLastWebPageFingerprint,
            base::Unretained(this)));
  }

 private:
  void HandleGetLastWebPageFingerprint(const base::ListValue& args) {
    AllowJavascript();
    const base::Value& callback_id = args[0];
    content::WebContents* contents =
        FindLastWebPage(web_ui()->GetWebContents());
    if (!contents || !contents->GetPrimaryMainFrame()->IsRenderFrameLive()) {
      ResolveJavascriptCallback(
          callback_id,
          base::Value(R"({"error":"Open a website in another tab first."})"));
      return;
    }

    content::RenderFrameHost::AllowInjectingJavaScript();
    contents->GetPrimaryMainFrame()->ExecuteJavaScript(
        base::UTF8ToUTF16(std::string(kReadFingerprintScript)),
        base::BindOnce(&FingerprintTestMessageHandler::OnGotFingerprint,
                       weak_ptr_factory_.GetWeakPtr(), callback_id.Clone()));
  }

  void OnGotFingerprint(base::Value callback_id, base::Value result) {
    if (!result.is_string()) {
      ResolveJavascriptCallback(
          callback_id,
          base::Value(R"({"error":"The selected webpage did not respond."})"));
      return;
    }
    ResolveJavascriptCallback(callback_id, std::move(result));
  }

  base::WeakPtrFactory<FingerprintTestMessageHandler> weak_ptr_factory_{this};
};

}  // namespace

FingerprintTestUI::FingerprintTestUI(content::WebUI* web_ui)
    : content::WebUIController(web_ui) {
  web_ui->AddMessageHandler(std::make_unique<FingerprintTestMessageHandler>());
  auto* source = CreateAndAddWebUIDataSource(web_ui, kFingerprintTestHost,
                                             kFingerprintTestGenerated,
                                             IDR_FINGERPRINT_TEST_HTML);
  Profile* profile = Profile::FromWebUI(web_ui);
  const auto* persona = fingerprint_browser::GetPersonaForProfile(profile);
  std::string persona_json;
  if (persona) {
    base::JSONWriter::Write(BuildPersonaSummary(*persona, *profile),
                            &persona_json);
  }
  source->AddString("fingerprintTestPersona", persona_json);
}

FingerprintTestUI::~FingerprintTestUI() = default;
