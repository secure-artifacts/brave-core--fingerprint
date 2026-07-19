/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  html,
  RegisterPolymerPrototypeModification,
  RegisterPolymerTemplateModifications
} from 'chrome://resources/brave/polymer_overriding.js'
import {loadTimeData} from '../i18n_setup.js'
import {Router} from '../router.js'

RegisterPolymerPrototypeModification({
  'settings-privacy-page': (prototype) => {
    const oldGetAssociatedControlFor = prototype.getAssociatedControlFor
    prototype.getAssociatedControlFor = function(childViewId: string) {
      if (childViewId === 'fingerprintProfileProxy') {
        return this.shadowRoot.querySelector(
          '#fingerprintProfileProxyLinkRow')!
      }
      return oldGetAssociatedControlFor.call(this, childViewId)
    }

    prototype.onFingerprintProfileProxyClick_ = () => {
      const router = Router.getInstance()
      router.navigateTo(router.getRoutes().FINGERPRINT_PROFILE_PROXY)
    }
  }
})

RegisterPolymerTemplateModifications({
  'settings-privacy-page': (templateContent) => {
    const section = templateContent.querySelector('settings-section')
    if (!section) {
      console.error(
        `[Settings] Couldn't find privacy_page settings-section`)
      return
    }

    const siteSettingsLinkRow =
      templateContent.getElementById('siteSettingsLinkRow')
    if (!siteSettingsLinkRow) {
      console.error(
        '[Brave Settings Overrides] Couldn\'t find siteSettingsLinkRow')
    } else {
      const parent = siteSettingsLinkRow.parentNode!
      const insertionPoint = siteSettingsLinkRow.nextSibling
      parent.insertBefore(html`
        <cr-link-row
          id="fingerprintProfileProxyLinkRow"
          class="hr"
          label="${loadTimeData.getString('profileProxyTitle')}"
          sub-label="${loadTimeData.getString('profileProxyEnabledDesc')}"
          on-click="onFingerprintProfileProxyClick_"
          role="link">
        </cr-link-row>
      `, insertionPoint)
      parent.insertBefore(html`
        <settings-brave-personalization-options prefs="{{prefs}}">
        </settings-brave-personalization-options>
      `, insertionPoint)
    }
    const thirdPartyCookiesLinkRow =
      templateContent.getElementById('thirdPartyCookiesLinkRow')
    if (!thirdPartyCookiesLinkRow) {
      console.error(
        '[Brave Settings Overrides] Could not find ' +
        'thirdPartyCookiesLinkRow id on privacy page.')
    } else {
      thirdPartyCookiesLinkRow.setAttribute('hidden', 'true')
    }

    if (!loadTimeData.getBoolean('isPrivacySandboxRestricted')) {
      const privacySandboxSettings3Template = templateContent.
        querySelector(`template[if*='isPrivacySandboxSettings3Enabled_']`)
      if (!privacySandboxSettings3Template) {
        console.error(
          '[Brave Settings Overrides] Could not find template with ' +
          'if*=isPrivacySandboxSettings3Enabled_ on privacy page.')
      } else {
        const privacySandboxLinkRow = privacySandboxSettings3Template.content.
          getElementById('privacySandboxLinkRow')
        if (!privacySandboxLinkRow) {
          console.error(
            '[Brave Settings Overrides] Could not find privacySandboxLinkRow' +
            ' id on privacy page.')
        } else {
          privacySandboxLinkRow.setAttribute('hidden', 'true')
        }
        const privacySandboxLink = privacySandboxSettings3Template.content.
          getElementById('privacySandboxLink')
        if (!privacySandboxLink) {
          console.error(
            '[Brave Settings Overrides] Could not find privacySandboxLink id' +
            ' on privacy page.')
        } else {
          privacySandboxSettings3Template.setAttribute('hidden', 'true')
        }
      }
      const privacySandboxSettings4Template = templateContent.
        querySelector(`template[if*='isPrivacySandboxSettings4Enabled_']`)
      if (!privacySandboxSettings4Template) {
        console.error(
          '[Brave Settings Overrides] Could not find template with ' +
          'if*=isPrivacySandboxSettings4Enabled_ on privacy page.')
      } else {
        const privacySandboxLinkRow = privacySandboxSettings4Template.content.
          getElementById('privacySandboxLinkRow')
        if (!privacySandboxLinkRow) {
          console.error(
            '[Brave Settings Overrides] Could not find privacySandboxLinkRow ' +
            'id on privacy page.')
        } else {
          privacySandboxLinkRow.setAttribute('hidden', 'true')
        }
      }
    }

    const showPrivacyGuideEntryPointTemplate =
      templateContent.querySelector(`template[if*='isPrivacyGuideAvailable']`)
    if (!showPrivacyGuideEntryPointTemplate) {
      console.error(
        '[Brave Settings Overrides] Could not find template with' +
        ' if*=isPrivacyGuideAvailable on privacy page.')
    } else {
      const privacyGuideLinkRow = showPrivacyGuideEntryPointTemplate.content.
        getElementById('privacyGuideLinkRow')
      if (!privacyGuideLinkRow) {
        console.error(
          '[Brave Settings Overrides] Could not find privacyGuideLinkRow id' +
          ' on privacy page.')
      } else {
        privacyGuideLinkRow.setAttribute('hidden', 'true')
      }
    }
  }
})
