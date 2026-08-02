/* Copyright (c) 2025 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <string>
#include <string_view>

#include "brave/browser/brave_shields/brave_shields_settings_service_factory.h"
#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/components/brave_shields/core/browser/brave_shields_settings_service.h"
#include "brave/components/brave_shields/core/browser/brave_shields_utils.h"
#include "brave/components/brave_shields/core/common/shields_settings.mojom-shared.h"
#include "brave/components/containers/buildflags/buildflags.h"
#include "brave/components/fingerprint_browser/browser/persona.h"
#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/web_contents.h"
#include "url/url_constants.h"

#if BUILDFLAG(ENABLE_CONTAINERS)
#include "base/feature_list.h"
#include "brave/components/containers/content/browser/storage_partition_utils.h"
#include "brave/components/containers/core/common/features.h"
#endif

#include <chrome/browser/content_settings/content_settings_manager_delegate.cc>

namespace {

bool IsJsBlockingEnforced(content::BrowserContext* browser_context,
                          const GURL& url) {
  Profile* profile = Profile::FromBrowserContext(browser_context);
  auto* settings_service =
      BraveShieldsSettingsServiceFactory::GetForProfile(profile);
  if (!settings_service) {
    return false;
  }

  return settings_service->IsJsBlockingEnforced(url);
}

brave_shields::mojom::ShieldsSettingsPtr GetBraveShieldsSettingsOnUI(
    const content::GlobalRenderFrameHostToken& frame_token,
    ContentSettingsType webcompat_settings_type) {
  content::RenderFrameHost* rfh =
      content::RenderFrameHost::FromFrameToken(frame_token);
  if (!rfh) {
    return brave_shields::mojom::ShieldsSettings::New();
  }
  content::RenderFrameHost* top_frame_rfh = rfh->GetOutermostMainFrame();
  if (!top_frame_rfh) {
    return brave_shields::mojom::ShieldsSettings::New();
  }

  GURL top_frame_url = top_frame_rfh->GetLastCommittedURL();
  if (top_frame_url.SchemeIs(url::kBlobScheme)) {
    const url::Origin& committed_origin =
        top_frame_rfh->GetLastCommittedOrigin();
    if (!committed_origin.opaque()) {
      top_frame_url = committed_origin.GetURL();
    }
  }

  content::BrowserContext* browser_context = rfh->GetBrowserContext();
  auto* content_settings =
      HostContentSettingsMapFactory::GetForProfile(browser_context);
  brave_shields::mojom::FarblingLevel farbling_level =
      brave_shields::GetFarblingLevel(content_settings, top_frame_url);
  if (webcompat_settings_type != ContentSettingsType::BRAVE_WEBCOMPAT_NONE &&
      brave_shields::IsWebcompatEnabled(
          content_settings, webcompat_settings_type, top_frame_url)) {
    farbling_level = brave_shields::mojom::FarblingLevel::OFF;
  }
  std::string additional_entropy;
#if BUILDFLAG(ENABLE_CONTAINERS)
  if (base::FeatureList::IsEnabled(containers::features::kContainers)) {
    if (content::WebContents* web_contents =
            content::WebContents::FromRenderFrameHost(top_frame_rfh)) {
      additional_entropy =
          containers::GetContainerIdForWebContents(web_contents);
    }
  }
#endif
  const base::Token persona_token =
      fingerprint_browser::GetPersonaFarblingTokenForBrowserContext(
          browser_context);
  const bool has_persona_farbling_token = !persona_token.is_zero();
  const auto* persona =
      fingerprint_browser::GetPersonaForBrowserContext(browser_context);
  if (top_frame_url.SchemeIs("chrome-extension")) {
    farbling_level = persona && has_persona_farbling_token
                         ? brave_shields::mojom::FarblingLevel::BALANCED
                         : brave_shields::mojom::FarblingLevel::OFF;
  }
  const bool has_persona_l1 =
      farbling_level != brave_shields::mojom::FarblingLevel::OFF && persona;
  const bool has_persona_l2 =
      has_persona_l1 && !persona->webgl.vendor.empty() &&
      !persona->webgl.renderer.empty() && !persona->webgpu.vendor.empty() &&
      !persona->webgpu.architecture.empty() &&
      !persona->webgpu.device.empty() && !persona->webgpu.description.empty();
  auto* shields_settings_service =
      BraveShieldsSettingsServiceFactory::GetForProfile(
          Profile::FromBrowserContext(browser_context));
  const base::Token farbling_token =
      farbling_level == brave_shields::mojom::FarblingLevel::OFF
          ? base::Token()
          : (has_persona_farbling_token
                 ? persona_token
                 : (shields_settings_service
                        ? shields_settings_service->GetFarblingToken(
                              top_frame_url,
                              base::as_byte_span(additional_entropy))
                        : base::Token()));
  PrefService* pref_service = user_prefs::UserPrefs::Get(browser_context);

  return brave_shields::mojom::ShieldsSettings::New(
      farbling_level, farbling_token,
      farbling_level != brave_shields::mojom::FarblingLevel::OFF &&
          has_persona_farbling_token,
      has_persona_l1, has_persona_l2,
      has_persona_l1 ? persona->user_agent : std::string(),
      has_persona_l1 ? persona->platform : std::string(),
      has_persona_l1 ? fingerprint_browser::GetProfileProxyLanguagesForPrefs(
                           *pref_service, persona->languages)
                     : std::vector<std::string>(),
      has_persona_l1 ? persona->ua_metadata.platform_version : std::string(),
      has_persona_l1 ? persona->ua_metadata.architecture : std::string(),
      has_persona_l1 ? persona->ua_metadata.bitness : std::string(),
      has_persona_l1 ? persona->ua_metadata.full_version : std::string(),
      has_persona_l1 ? fingerprint_browser::UserAgentBrandNames(
                           persona->ua_metadata.brands)
                     : std::vector<std::string>(),
      has_persona_l1 ? fingerprint_browser::UserAgentBrandVersions(
                           persona->ua_metadata.brands)
                     : std::vector<std::string>(),
      has_persona_l1 ? fingerprint_browser::UserAgentBrandNames(
                           persona->ua_metadata.full_version_list)
                     : std::vector<std::string>(),
      has_persona_l1 ? fingerprint_browser::UserAgentBrandVersions(
                           persona->ua_metadata.full_version_list)
                     : std::vector<std::string>(),
      has_persona_l1 && persona->ua_metadata.mobile,
      has_persona_l1 ? static_cast<uint32_t>(persona->hardware_concurrency) : 0,
      has_persona_l1 ? persona->device_memory_gb : 0.0,
      has_persona_l1 ? static_cast<uint32_t>(persona->max_touch_points) : 0,
      has_persona_l1 ? persona->screen.width : 0,
      has_persona_l1 ? persona->screen.height : 0,
      has_persona_l1 ? persona->screen.avail_width : 0,
      has_persona_l1 ? persona->screen.avail_height : 0,
      has_persona_l1 ? persona->screen.color_depth : 0,
      has_persona_l1 ? persona->screen.device_scale_factor : 0.0,
      has_persona_l1 ? persona->screen.window_x : 0,
      has_persona_l1 ? persona->screen.window_y : 0,
      has_persona_l2 ? persona->webgl.vendor : std::string(),
      has_persona_l2 ? persona->webgl.renderer : std::string(),
      has_persona_l2 ? persona->webgpu.vendor : std::string(),
      has_persona_l2 ? persona->webgpu.architecture : std::string(),
      has_persona_l2 ? persona->webgpu.device : std::string(),
      has_persona_l2 ? persona->webgpu.description : std::string(),
      has_persona_l1 ? persona->fonts : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaMediaDeviceKinds(persona->media_devices)
          : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaMediaDeviceIds(persona->media_devices)
          : std::vector<std::string>(),
      has_persona_l1 ? fingerprint_browser::PersonaMediaDeviceLabels(
                           persona->media_devices)
                     : std::vector<std::string>(),
      has_persona_l1 ? fingerprint_browser::PersonaMediaDeviceGroupIds(
                           persona->media_devices)
                     : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaSpeechVoiceUris(persona->speech_voices)
          : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaSpeechVoiceNames(persona->speech_voices)
          : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaSpeechVoiceLangs(persona->speech_voices)
          : std::vector<std::string>(),
      has_persona_l1 ? fingerprint_browser::PersonaSpeechVoiceLocalServices(
                           persona->speech_voices)
                     : std::vector<bool>(),
      has_persona_l1 ? fingerprint_browser::PersonaSpeechVoiceDefaults(
                           persona->speech_voices)
                     : std::vector<bool>(),
      has_persona_l1 ? fingerprint_browser::PersonaPluginNames(persona->plugins)
                     : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaPluginFilenames(persona->plugins)
          : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaPluginDescriptions(persona->plugins)
          : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaPluginMimeTypeCounts(persona->plugins)
          : std::vector<uint32_t>(),
      has_persona_l1
          ? fingerprint_browser::PersonaMimeTypeTypes(persona->plugins)
          : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaMimeTypeDescriptions(persona->plugins)
          : std::vector<std::string>(),
      has_persona_l1
          ? fingerprint_browser::PersonaMimeTypeSuffixes(persona->plugins)
          : std::vector<std::vector<std::string>>(),
      std::vector<std::string>(),
      brave_shields::IsReduceLanguageEnabledForProfile(pref_service),
      IsJsBlockingEnforced(browser_context, top_frame_url));
}

}  // namespace

void ContentSettingsManagerDelegate::GetBraveShieldsSettings(
    const content::GlobalRenderFrameHostToken& frame_token,
    ContentSettingsType webcompat_settings_type,
    content_settings::mojom::ContentSettingsManager::
        GetBraveShieldsSettingsCallback callback) {
  content::GetUIThreadTaskRunner({})->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&GetBraveShieldsSettingsOnUI, frame_token,
                     webcompat_settings_type),
      std::move(callback));
}
