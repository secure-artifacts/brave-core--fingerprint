/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <optional>

#include "base/command_line.h"
#include "base/path_service.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/thread_test_helper.h"
#include "brave/browser/extensions/brave_base_local_data_files_browsertest.h"
#include "brave/browser/fingerprint_browser/persona_service_factory.h"
#include "brave/components/brave_component_updater/browser/local_data_files_service.h"
#include "brave/components/brave_shields/core/browser/brave_shields_utils.h"
#include "brave/components/brave_shields/core/common/features.h"
#include "brave/components/constants/brave_paths.h"
#include "brave/components/constants/pref_names.h"
#include "brave/components/webcompat/core/common/features.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "ui/base/window_open_disposition.h"

using brave_shields::ControlType;

constexpr char kEmbeddedTestServerDirectory[] = "webaudio";
constexpr char kTitleNumberScript[] = "Number(document.title);";
constexpr char kWebAudioResultScript[] =
    "(async () => await window.webAudioAnalysisPromise)()";
constexpr char kAudioBufferFingerprintScript[] = R"(
  (() => {
    const length = 1024;
    const sampleRate = 8000;
    const ctx = new AudioContext();
    const hash = (array) => {
      let sum = 0;
      for (let i = 0; i < array.length; ++i) {
        sum += array[i];
      }
      return Math.round(sum * 1000000);
    };
    const createBuffer = () => {
      const buffer = ctx.createBuffer(1, length, sampleRate);
      const source = new Float32Array(length);
      source.fill(1);
      buffer.copyToChannel(source, 0);
      return buffer;
    };
    const getChannelDataHash = () => {
      return hash(createBuffer().getChannelData(0));
    };
    const copyFromChannelHash = () => {
      const destination = new Float32Array(length);
      createBuffer().copyFromChannel(destination, 0);
      return hash(destination);
    };

    const repeatedBuffer = createBuffer();
    const repeatedView1 = repeatedBuffer.getChannelData(0);
    const repeatedHash1 = hash(repeatedView1);
    const repeatedView2 = repeatedBuffer.getChannelData(0);
    const repeatedHash2 = hash(repeatedView2);
    const repeatedCopy = new Float32Array(length);
    repeatedBuffer.copyFromChannel(repeatedCopy, 0);

    const writeIndex = 257;
    repeatedView2[writeIndex] = 0.31415927;
    const writtenCopy = new Float32Array(1);
    repeatedBuffer.copyFromChannel(writtenCopy, 0, writeIndex);
    const writtenValue = repeatedView2[writeIndex];

    const slicedBuffer = createBuffer();
    const slicedFull = slicedBuffer.getChannelData(0);
    const sliceOffset = 320;
    const slicedCopy = new Float32Array(64);
    slicedBuffer.copyFromChannel(slicedCopy, 0, sliceOffset);
    const sliceMatches = slicedCopy.every(
        (value, index) => value === slicedFull[sliceOffset + index]);

    const getChannelDataHash1 = getChannelDataHash();
    const getChannelDataHash2 = getChannelDataHash();
    const copyFromChannelHash1 = copyFromChannelHash();
    const copyFromChannelHash2 = copyFromChannelHash();
    ctx.close();
    return {
      getChannelDataHash: getChannelDataHash1,
      getChannelDataStable: getChannelDataHash1 === getChannelDataHash2,
      copyFromChannelHash: copyFromChannelHash1,
      copyFromChannelStable: copyFromChannelHash1 === copyFromChannelHash2,
      repeatedBufferStable: repeatedHash1 === repeatedHash2 &&
          repeatedHash2 === hash(repeatedCopy),
      scriptWriteStable: writtenValue === writtenCopy[0],
      sliceMatches
    };
  })()
)";

void ExpectDictBool(const base::DictValue& values,
                    const char* key,
                    bool expected) {
  const std::optional<bool> value = values.FindBool(key);
  ASSERT_TRUE(value.has_value()) << key;
  EXPECT_EQ(expected, *value) << key;
}

int GetDictInt(const base::DictValue& values, const char* key) {
  const std::optional<int> value = values.FindInt(key);
  EXPECT_TRUE(value.has_value()) << key;
  return value.value_or(0);
}

class BraveWebAudioFarblingBrowserTest : public InProcessBrowserTest {
 public:
  BraveWebAudioFarblingBrowserTest() {
    scoped_feature_list_.InitWithFeatures(
        /*enabled_features=*/
        {webcompat::features::kBraveWebcompatExceptionsService,
         brave_shields::features::kBraveShowStrictFingerprintingMode},
        /*disabled_features=*/{});
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    host_resolver()->AddRule("*", "127.0.0.1");
    content::SetupCrossSiteRedirector(embedded_test_server());

    base::FilePath test_data_dir;
    base::PathService::Get(brave::DIR_TEST_DATA, &test_data_dir);
    test_data_dir = test_data_dir.AppendASCII(kEmbeddedTestServerDirectory);
    embedded_test_server()->ServeFilesFromDirectory(test_data_dir);

    ASSERT_TRUE(embedded_test_server()->Start());

    top_level_page_url_ = embedded_test_server()->GetURL("a.com", "/");
    farbling_url_ = embedded_test_server()->GetURL("a.com", "/farbling.html");
    farbling2_url_ = embedded_test_server()->GetURL("a.com", "/farbling2.html");
    copy_from_channel_url_ =
        embedded_test_server()->GetURL("a.com", "/copyFromChannel.html");
  }

  const GURL& copy_from_channel_url() { return copy_from_channel_url_; }

  const GURL& farbling_url() { return farbling_url_; }

  const GURL& farbling2_url() { return farbling2_url_; }

  HostContentSettingsMap* content_settings() {
    return HostContentSettingsMapFactory::GetForProfile(browser()->profile());
  }

  void AllowFingerprinting() {
    brave_shields::SetFingerprintingControlType(
        content_settings(), ControlType::ALLOW, top_level_page_url_);
  }

  void BlockFingerprinting() {
    brave_shields::SetFingerprintingControlType(
        content_settings(), ControlType::BLOCK, top_level_page_url_);
  }

  void SetFingerprintingDefault() {
    brave_shields::SetFingerprintingControlType(
        content_settings(), ControlType::DEFAULT, top_level_page_url_);
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  int ReadTitleNumber() {
    return content::EvalJs(contents(), kTitleNumberScript).ExtractInt();
  }

  int ReadWebAudioResult() {
    return content::EvalJs(contents(), kWebAudioResultScript).ExtractInt();
  }

 private:
  GURL top_level_page_url_;
  GURL copy_from_channel_url_;
  GURL farbling_url_;
  GURL farbling2_url_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

class BraveWebAudioFarblingWithoutPersonaBrowserTest
    : public BraveWebAudioFarblingBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    BraveWebAudioFarblingBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(
        "disable-fingerprint-browser-persona-for-testing");
  }
};

// Tests for crash in copyFromChannel as reported in
// https://github.com/brave/brave-browser/issues/9552
// No crash indicates a successful test.
IN_PROC_BROWSER_TEST_F(BraveWebAudioFarblingWithoutPersonaBrowserTest,
                       CopyFromChannelNoCrash) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), copy_from_channel_url()));
}

IN_PROC_BROWSER_TEST_F(BraveWebAudioFarblingWithoutPersonaBrowserTest,
                       FarbleWebAudio) {
  AllowFingerprinting();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const int off_audio_sum = ReadTitleNumber();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling2_url()));
  const int off_analysis_sum = ReadWebAudioResult();

  BlockFingerprinting();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const int maximum_audio_sum = ReadTitleNumber();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  EXPECT_EQ(maximum_audio_sum, ReadTitleNumber());
  EXPECT_NE(off_audio_sum, maximum_audio_sum);

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling2_url()));
  EXPECT_NE(off_analysis_sum, ReadWebAudioResult());

  SetFingerprintingDefault();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const int balanced_audio_sum = ReadTitleNumber();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  EXPECT_EQ(balanced_audio_sum, ReadTitleNumber());

  SetFingerprintingDefault();
  brave_shields::SetWebcompatEnabled(content_settings(),
                                     ContentSettingsType::BRAVE_WEBCOMPAT_AUDIO,
                                     true, farbling_url(), nullptr);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  EXPECT_EQ(off_audio_sum, ReadTitleNumber());

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling2_url()));
  EXPECT_EQ(off_analysis_sum, ReadWebAudioResult());
}

IN_PROC_BROWSER_TEST_F(BraveWebAudioFarblingBrowserTest,
                       PersonaAudioBufferOutputsAreStable) {
  SetFingerprintingDefault();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const content::EvalJsResult persona_result =
      content::EvalJs(contents(), kAudioBufferFingerprintScript);
  const base::DictValue& persona_values = persona_result.ExtractDict();
  ExpectDictBool(persona_values, "getChannelDataStable", true);
  ExpectDictBool(persona_values, "copyFromChannelStable", true);
  ExpectDictBool(persona_values, "repeatedBufferStable", true);
  ExpectDictBool(persona_values, "scriptWriteStable", true);
  ExpectDictBool(persona_values, "sliceMatches", true);
  const int persona_get_channel_data_hash =
      GetDictInt(persona_values, "getChannelDataHash");
  const int persona_copy_from_channel_hash =
      GetDictInt(persona_values, "copyFromChannelHash");

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const content::EvalJsResult reloaded_result =
      content::EvalJs(contents(), kAudioBufferFingerprintScript);
  const base::DictValue& reloaded_values = reloaded_result.ExtractDict();
  ExpectDictBool(reloaded_values, "repeatedBufferStable", true);
  ExpectDictBool(reloaded_values, "scriptWriteStable", true);
  ExpectDictBool(reloaded_values, "sliceMatches", true);
  EXPECT_EQ(persona_get_channel_data_hash,
            GetDictInt(reloaded_values, "getChannelDataHash"));
  EXPECT_EQ(persona_copy_from_channel_hash,
            GetDictInt(reloaded_values, "copyFromChannelHash"));

  content::RenderFrameHost* new_tab =
      ui_test_utils::NavigateToURLWithDisposition(
          browser(), farbling_url(), WindowOpenDisposition::NEW_FOREGROUND_TAB,
          ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP);
  ASSERT_TRUE(new_tab);
  const content::EvalJsResult new_tab_result =
      content::EvalJs(contents(), kAudioBufferFingerprintScript);
  const base::DictValue& new_tab_values = new_tab_result.ExtractDict();
  EXPECT_EQ(persona_get_channel_data_hash,
            GetDictInt(new_tab_values, "getChannelDataHash"));
  EXPECT_EQ(persona_copy_from_channel_hash,
            GetDictInt(new_tab_values, "copyFromChannelHash"));

  AllowFingerprinting();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const content::EvalJsResult real_result =
      content::EvalJs(contents(), kAudioBufferFingerprintScript);
  const base::DictValue& real_values = real_result.ExtractDict();
  ExpectDictBool(real_values, "getChannelDataStable", true);
  ExpectDictBool(real_values, "copyFromChannelStable", true);
  ExpectDictBool(real_values, "repeatedBufferStable", true);
  ExpectDictBool(real_values, "scriptWriteStable", true);
  ExpectDictBool(real_values, "sliceMatches", true);
  EXPECT_NE(GetDictInt(real_values, "getChannelDataHash"),
            persona_get_channel_data_hash);
  EXPECT_NE(GetDictInt(real_values, "copyFromChannelHash"),
            persona_copy_from_channel_hash);

  SetFingerprintingDefault();
  brave_shields::SetWebcompatEnabled(content_settings(),
                                     ContentSettingsType::BRAVE_WEBCOMPAT_AUDIO,
                                     true, farbling_url(), nullptr);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const content::EvalJsResult webcompat_result =
      content::EvalJs(contents(), kAudioBufferFingerprintScript);
  const base::DictValue& webcompat_values = webcompat_result.ExtractDict();
  ExpectDictBool(webcompat_values, "getChannelDataStable", true);
  ExpectDictBool(webcompat_values, "copyFromChannelStable", true);
  ExpectDictBool(webcompat_values, "repeatedBufferStable", true);
  ExpectDictBool(webcompat_values, "scriptWriteStable", true);
  ExpectDictBool(webcompat_values, "sliceMatches", true);
  EXPECT_EQ(GetDictInt(real_values, "getChannelDataHash"),
            GetDictInt(webcompat_values, "getChannelDataHash"));
  EXPECT_EQ(GetDictInt(real_values, "copyFromChannelHash"),
            GetDictInt(webcompat_values, "copyFromChannelHash"));
}

IN_PROC_BROWSER_TEST_F(BraveWebAudioFarblingWithoutPersonaBrowserTest,
                       NativeAudioFarblingRemainsEnabled) {
  ASSERT_FALSE(fingerprint_browser::GetPersonaForProfile(browser()->profile()));
  SetFingerprintingDefault();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const content::EvalJsResult protected_result =
      content::EvalJs(contents(), kAudioBufferFingerprintScript);
  const base::DictValue& protected_values = protected_result.ExtractDict();

  AllowFingerprinting();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), farbling_url()));
  const content::EvalJsResult real_result =
      content::EvalJs(contents(), kAudioBufferFingerprintScript);
  const base::DictValue& real_values = real_result.ExtractDict();
  EXPECT_NE(GetDictInt(protected_values, "getChannelDataHash"),
            GetDictInt(real_values, "getChannelDataHash"));
  EXPECT_NE(GetDictInt(protected_values, "copyFromChannelHash"),
            GetDictInt(real_values, "copyFromChannelHash"));
}
