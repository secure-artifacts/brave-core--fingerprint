/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/fingerprint_browser/browser/truth_pool.h"

namespace fingerprint_browser {

namespace {

std::vector<UserAgentBrand> ReducedBrands() {
  return {
      {"Not.A/Brand", "99"},
      {"Chromium", "150"},
      {"Google Chrome", "150"},
  };
}

std::vector<UserAgentBrand> FullVersionBrands() {
  return {
      {"Not.A/Brand", "99.0.0.0"},
      {"Chromium", "150.1.94.0"},
      {"Google Chrome", "150.1.94.0"},
  };
}

UserAgentMetadata WindowsUACH() {
  return {
      .platform = "Windows",
      .platform_version = "19.0.0",
      .architecture = "x86",
      .bitness = "64",
      .full_version = "150.1.94.0",
      .brands = ReducedBrands(),
      .full_version_list = FullVersionBrands(),
      .mobile = false,
  };
}

UserAgentMetadata MacUACH() {
  return {
      .platform = "macOS",
      .platform_version = "15.5.0",
      .architecture = "arm",
      .bitness = "64",
      .full_version = "150.1.94.0",
      .brands = ReducedBrands(),
      .full_version_list = FullVersionBrands(),
      .mobile = false,
  };
}

}  // namespace

TruthPool GetDefaultTruthPool() {
  TruthPool pool;

  pool.user_agents = {
      {
          .id = "chrome-150-win10-x64",
          .os = PersonaOS::kWindows,
          .user_agent =
              "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 "
              "Safari/537.36",
          .metadata = WindowsUACH(),
          .platform = "Win32",
      },
      {
          .id = "chrome-150-win11-x64",
          .os = PersonaOS::kWindows,
          .user_agent =
              "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 "
              "Safari/537.36",
          .metadata = WindowsUACH(),
          .platform = "Win32",
      },
      {
          .id = "chrome-150-macos-arm64",
          .os = PersonaOS::kMacOS,
          .user_agent =
              "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_5) "
              "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 "
              "Safari/537.36",
          .metadata = MacUACH(),
          .platform = "MacIntel",
      },
  };

  pool.renderers = {
      {
          .id = "win-nvidia-rtx-3060-d3d11",
          .os = PersonaOS::kWindows,
          .webgl =
              {
                  .vendor = "Google Inc. (NVIDIA)",
                  .renderer =
                      "ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 "
                      "vs_5_0 ps_5_0, D3D11)",
              },
          .webgpu =
              {
                  .vendor = "nvidia",
                  .architecture = "ampere",
                  .device = "geforce-rtx-3060",
                  .description = "NVIDIA GeForce RTX 3060",
              },
      },
      {
          .id = "win-intel-uhd-770-d3d11",
          .os = PersonaOS::kWindows,
          .webgl =
              {
                  .vendor = "Google Inc. (Intel)",
                  .renderer =
                      "ANGLE (Intel, Intel(R) UHD Graphics 770 Direct3D11 "
                      "vs_5_0 ps_5_0, D3D11)",
              },
          .webgpu =
              {
                  .vendor = "intel",
                  .architecture = "gen12",
                  .device = "uhd-graphics-770",
                  .description = "Intel(R) UHD Graphics 770",
              },
      },
      {
          .id = "mac-apple-m1-metal",
          .os = PersonaOS::kMacOS,
          .webgl =
              {
                  .vendor = "Google Inc. (Apple)",
                  .renderer = "ANGLE (Apple, ANGLE Metal Renderer: Apple M1, "
                              "Unspecified Version)",
              },
          .webgpu =
              {
                  .vendor = "apple",
                  .architecture = "apple-m1",
                  .device = "apple-m1",
                  .description = "Apple M1",
              },
      },
      {
          .id = "mac-apple-m2-metal",
          .os = PersonaOS::kMacOS,
          .webgl =
              {
                  .vendor = "Google Inc. (Apple)",
                  .renderer = "ANGLE (Apple, ANGLE Metal Renderer: Apple M2, "
                              "Unspecified Version)",
              },
          .webgpu =
              {
                  .vendor = "apple",
                  .architecture = "apple-m2",
                  .device = "apple-m2",
                  .description = "Apple M2",
              },
      },
  };

  pool.screens = {
      {
          .id = "desktop-1920x1080",
          .os = PersonaOS::kWindows,
          .screen =
              {
                  .width = 1920,
                  .height = 1080,
                  .avail_width = 1920,
                  .avail_height = 1040,
                  .color_depth = 24,
                  .device_scale_factor = 1.0,
                  .window_x = 0,
                  .window_y = 0,
              },
          .hardware_concurrency = 8,
          .device_memory_gb = 8.0,
          .max_touch_points = 0,
      },
      {
          .id = "desktop-2560x1440",
          .os = PersonaOS::kWindows,
          .screen =
              {
                  .width = 2560,
                  .height = 1440,
                  .avail_width = 2560,
                  .avail_height = 1392,
                  .color_depth = 24,
                  .device_scale_factor = 1.0,
                  .window_x = 0,
                  .window_y = 0,
              },
          .hardware_concurrency = 12,
          .device_memory_gb = 16.0,
          .max_touch_points = 0,
      },
      {
          .id = "macbook-1512x982",
          .os = PersonaOS::kMacOS,
          .screen =
              {
                  .width = 1512,
                  .height = 982,
                  .avail_width = 1512,
                  .avail_height = 945,
                  .color_depth = 30,
                  .device_scale_factor = 2.0,
                  .window_x = 0,
                  .window_y = 25,
              },
          .hardware_concurrency = 8,
          .device_memory_gb = 8.0,
          .max_touch_points = 0,
      },
      {
          .id = "macbook-1728x1117",
          .os = PersonaOS::kMacOS,
          .screen =
              {
                  .width = 1728,
                  .height = 1117,
                  .avail_width = 1728,
                  .avail_height = 1079,
                  .color_depth = 30,
                  .device_scale_factor = 2.0,
                  .window_x = 0,
                  .window_y = 25,
              },
          .hardware_concurrency = 10,
          .device_memory_gb = 16.0,
          .max_touch_points = 0,
      },
  };

  pool.font_sets = {
      {
          .id = "win-en-us-core",
          .os = PersonaOS::kWindows,
          .locale = "en-US",
          .fonts =
              {
                  "Arial",
                  "Calibri",
                  "Cambria",
                  "Consolas",
                  "Courier New",
                  "Georgia",
                  "Segoe UI",
                  "Times New Roman",
                  "Verdana",
              },
      },
      {
          .id = "mac-en-us-core",
          .os = PersonaOS::kMacOS,
          .locale = "en-US",
          .fonts =
              {
                  "Apple Color Emoji",
                  "Arial",
                  "Courier New",
                  "Georgia",
                  "Helvetica",
                  "Helvetica Neue",
                  "Menlo",
                  "Monaco",
                  "SF Pro Text",
                  "Times New Roman",
              },
      },
  };

  pool.locales = {
      {
          .id = "en-us",
          .locale = "en-US",
          .languages = {"en-US", "en"},
          .accept_language = "en-US,en;q=0.9",
      },
  };

  pool.noise_seeds = {
      {
          .id = "noise-a",
          .canvas_noise_seed = "9a4f1c62d7b083e5",
          .audio_noise_seed = "78f01bd0c5924e31",
      },
      {
          .id = "noise-b",
          .canvas_noise_seed = "b083e59a4f1c62d7",
          .audio_noise_seed = "c5924e3178f01bd0",
      },
      {
          .id = "noise-c",
          .canvas_noise_seed = "d7b083e59a4f1c62",
          .audio_noise_seed = "4e3178f01bd0c592",
      },
  };

  pool.media_device_sets = {
      {
          .id = "win-desktop-media",
          .os = PersonaOS::kWindows,
          .devices =
              {
                  {
                      .kind = PersonaMediaDeviceKind::kAudioInput,
                      .device_id = "persona-win-audio-input-primary",
                      .label = "Microphone Array (Realtek(R) Audio)",
                      .group_id = "persona-win-audio",
                  },
                  {
                      .kind = PersonaMediaDeviceKind::kVideoInput,
                      .device_id = "persona-win-video-input-primary",
                      .label = "Integrated Camera",
                      .group_id = "persona-win-camera",
                  },
                  {
                      .kind = PersonaMediaDeviceKind::kAudioOutput,
                      .device_id = "persona-win-audio-output-primary",
                      .label = "Speakers (Realtek(R) Audio)",
                      .group_id = "persona-win-audio",
                  },
              },
      },
      {
          .id = "macbook-media",
          .os = PersonaOS::kMacOS,
          .devices =
              {
                  {
                      .kind = PersonaMediaDeviceKind::kAudioInput,
                      .device_id = "persona-mac-audio-input-primary",
                      .label = "MacBook Pro Microphone",
                      .group_id = "persona-mac-audio",
                  },
                  {
                      .kind = PersonaMediaDeviceKind::kVideoInput,
                      .device_id = "persona-mac-video-input-primary",
                      .label = "FaceTime HD Camera",
                      .group_id = "persona-mac-camera",
                  },
                  {
                      .kind = PersonaMediaDeviceKind::kAudioOutput,
                      .device_id = "persona-mac-audio-output-primary",
                      .label = "MacBook Pro Speakers",
                      .group_id = "persona-mac-audio",
                  },
              },
      },
  };

  pool.speech_voice_sets = {
      {
          .id = "win-en-us-voices",
          .os = PersonaOS::kWindows,
          .locale = "en-US",
          .voices =
              {
                  {
                      .voice_uri = "persona:windows:en-US:aria",
                      .name = "Microsoft Aria - English (United States)",
                      .lang = "en-US",
                      .local_service = true,
                      .is_default = true,
                  },
                  {
                      .voice_uri = "persona:windows:en-US:guy",
                      .name = "Microsoft Guy - English (United States)",
                      .lang = "en-US",
                      .local_service = true,
                      .is_default = false,
                  },
              },
      },
      {
          .id = "mac-en-us-voices",
          .os = PersonaOS::kMacOS,
          .locale = "en-US",
          .voices =
              {
                  {
                      .voice_uri = "persona:macos:en-US:samantha",
                      .name = "Samantha",
                      .lang = "en-US",
                      .local_service = true,
                      .is_default = true,
                  },
                  {
                      .voice_uri = "persona:macos:en-US:alex",
                      .name = "Alex",
                      .lang = "en-US",
                      .local_service = true,
                      .is_default = false,
                  },
              },
      },
  };

  return pool;
}

}  // namespace fingerprint_browser
