/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import 'chrome://resources/cr_elements/cr_button/cr_button.js'
import 'chrome://resources/cr_elements/cr_input/cr_input.js'

import {I18nMixin} from 'chrome://resources/cr_elements/i18n_mixin.js'
import {WebUiListenerMixin} from 'chrome://resources/cr_elements/web_ui_listener_mixin.js'
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import type {SettingsToggleButtonElement} from '../controls/settings_toggle_button.js'
import {loadTimeData} from '../i18n_setup.js'

import {FingerprintProfileProxyBrowserProxyImpl} from './fingerprint_profile_proxy_browser_proxy.js'
import type {
  FingerprintProfileProxyBrowserProxy,
  ProfileProxyConfig,
  ProfileProxyLastError,
} from './fingerprint_profile_proxy_browser_proxy.js'
import {getTemplate} from './fingerprint_profile_proxy_subpage.html.js'

const SettingsFingerprintProfileProxySubpageElementBase =
  WebUiListenerMixin(I18nMixin(PolymerElement))

class SettingsFingerprintProfileProxySubpageElement extends
  SettingsFingerprintProfileProxySubpageElementBase {
  static get is() {
    return 'settings-fingerprint-profile-proxy-subpage'
  }

  static get template() {
    return getTemplate()
  }

  static get properties() {
    return {
      enabledPref_: {
        type: Object,
        value() {
          return {
            key: '',
            type: chrome.settingsPrivate.PrefType.BOOLEAN,
            value: false,
          }
        },
      },
      scheme_: {
        type: String,
        value: 'http',
      },
      host_: {
        type: String,
        value: '',
      },
      port_: {
        type: String,
        value: '',
      },
      username_: {
        type: String,
        value: '',
      },
      password_: {
        type: String,
        value: '',
      },
      manualCountryCode_: {
        type: String,
        value: '',
      },
      manualTimezone_: {
        type: String,
        value: '',
      },
      manualLatitude_: {
        type: String,
        value: '',
      },
      manualLongitude_: {
        type: String,
        value: '',
      },
      geoWarning_: {
        type: String,
        value: '',
      },
      conflictWarning_: {
        type: String,
        value: '',
      },
      lastError_: {
        type: String,
        value: '',
      },
      hostError_: {
        type: String,
        value: '',
      },
      portError_: {
        type: String,
        value: '',
      },
      saveError_: {
        type: String,
        value: '',
      },
      savedStatus_: {
        type: String,
        value: '',
      },
      isSaving_: {
        type: Boolean,
        value: false,
      },
      showNoProxyRisk_: {
        type: Boolean,
        computed: 'computeShowNoProxyRisk_(enabledPref_.value)',
      },
    }
  }

  private declare enabledPref_: chrome.settingsPrivate.PrefObject<boolean>
  private declare scheme_: string
  private declare host_: string
  private declare port_: string
  private declare username_: string
  private declare password_: string
  private declare manualCountryCode_: string
  private declare manualTimezone_: string
  private declare manualLatitude_: string
  private declare manualLongitude_: string
  private declare geoWarning_: string
  private declare conflictWarning_: string
  private declare lastError_: string
  private declare hostError_: string
  private declare portError_: string
  private declare saveError_: string
  private declare savedStatus_: string
  private declare isSaving_: boolean
  private declare showNoProxyRisk_: boolean

  private browserProxy_: FingerprintProfileProxyBrowserProxy =
    FingerprintProfileProxyBrowserProxyImpl.getInstance()

  override ready() {
    super.ready()

    if (loadTimeData.getBoolean('shouldExposeElementsForTesting')) {
      window.testing = window.testing || {}
      window.testing['fingerprintProfileProxySubpage'] = this.shadowRoot
    }

    this.addWebUiListener(
      'fingerprint-profile-proxy-error-changed',
      (error: ProfileProxyLastError) => this.setLastError_(error))

    this.browserProxy_.getConfig().then((config: ProfileProxyConfig) => {
      this.setEnabledPref_(config.enabled)
      this.scheme_ = config.scheme || 'http'
      this.host_ = config.host || ''
      this.port_ = config.port ? String(config.port) : ''
      this.username_ = config.username || ''
      this.password_ = config.password || ''
      this.manualCountryCode_ = config.manualCountryCode || ''
      this.manualTimezone_ = config.manualTimezone || ''
      this.manualLatitude_ = config.manualLatitude || ''
      this.manualLongitude_ = config.manualLongitude || ''
      this.geoWarning_ = config.geoWarning || ''
      this.conflictWarning_ = config.conflictWarning || ''
    })

    this.browserProxy_.getLastError().then(
      (error: ProfileProxyLastError) => this.setLastError_(error))
  }

  private setEnabledPref_(enabled: boolean) {
    this.enabledPref_ = {
      key: '',
      type: chrome.settingsPrivate.PrefType.BOOLEAN,
      value: enabled,
    }
  }

  private setLastError_(error: ProfileProxyLastError) {
    this.lastError_ = error.message || ''
  }

  private onEnabledChange_(event: Event) {
    event.stopPropagation()
    this.setEnabledPref_((event.target as SettingsToggleButtonElement).checked)
    this.clearTransientMessages_()
  }

  private onSchemeChanged_(event: Event) {
    this.scheme_ = (event.target as HTMLSelectElement).value
    this.clearTransientMessages_()
  }

  private onInputChanged_() {
    this.clearTransientMessages_()
  }

  private clearTransientMessages_() {
    this.hostError_ = ''
    this.portError_ = ''
    this.saveError_ = ''
    this.savedStatus_ = ''
  }

  private manualGeoValuePresent_() {
    return this.manualCountryCode_.trim().length > 0 ||
      this.manualTimezone_.trim().length > 0 ||
      this.manualLatitude_.trim().length > 0 ||
      this.manualLongitude_.trim().length > 0
  }

  private validate_() {
    this.clearTransientMessages_()
    const enabled = this.enabledPref_.value
    const host = this.host_.trim()
    const portText = this.port_.trim()
    const hasProxyValue = host.length > 0 || portText.length > 0

    if ((enabled || hasProxyValue) && host.length === 0) {
      this.hostError_ = this.i18n('profileProxyHostRequired')
    }

    const port = Number(portText)
    if ((enabled || hasProxyValue) &&
        (!/^\d+$/.test(portText) || port < 1 || port > 65535)) {
      this.portError_ = this.i18n('profileProxyPortInvalid')
    }

    if (this.manualGeoValuePresent_()) {
      const country = this.manualCountryCode_.trim()
      const timezone = this.manualTimezone_.trim()
      const latitudeText = this.manualLatitude_.trim()
      const longitudeText = this.manualLongitude_.trim()
      const latitude = Number(latitudeText)
      const longitude = Number(longitudeText)
      if (!country || !timezone || !latitudeText || !longitudeText) {
        this.saveError_ = this.i18n('profileProxyManualGeoIncomplete')
      } else if (!/^[a-zA-Z]{2}$/.test(country)) {
        this.saveError_ = this.i18n('profileProxyManualGeoCountryInvalid')
      } else if (!Number.isFinite(latitude) || latitude < -90 ||
          latitude > 90) {
        this.saveError_ = this.i18n('profileProxyManualGeoLatitudeInvalid')
      } else if (!Number.isFinite(longitude) || longitude < -180 ||
          longitude > 180) {
        this.saveError_ = this.i18n('profileProxyManualGeoLongitudeInvalid')
      }
    }

    return this.hostError_.length === 0 && this.portError_.length === 0 &&
      this.saveError_.length === 0
  }

  private async onSave_(event: Event) {
    event.stopPropagation()
    if (!this.validate_()) {
      return
    }

    this.isSaving_ = true
    try {
      const result = await this.browserProxy_.setConfig(this.buildConfig_())
      if (!result.success) {
        this.saveError_ = result.error
        return
      }
      this.conflictWarning_ = result.conflictWarning || ''
      this.geoWarning_ = result.geoWarning || ''
      this.setLastError_({message: '', code: 0})
      this.savedStatus_ = this.i18n('profileProxySaved')
    } finally {
      this.isSaving_ = false
    }
  }

  private buildConfig_(): ProfileProxyConfig {
    return {
      enabled: this.enabledPref_.value,
      scheme: this.scheme_,
      host: this.host_.trim(),
      port: this.port_.trim() ? Number(this.port_.trim()) : 0,
      username: this.username_,
      password: this.password_,
      conflictWarning: this.conflictWarning_,
      manualCountryCode: this.manualCountryCode_.trim(),
      manualTimezone: this.manualTimezone_.trim(),
      manualLatitude: this.manualLatitude_.trim(),
      manualLongitude: this.manualLongitude_.trim(),
      geoWarning: this.geoWarning_,
      derivedCountryCode: '',
      derivedTimezone: '',
      derivedLatitude: '',
      derivedLongitude: '',
    }
  }

  private schemeEqual_(expected: string, actual: string) {
    return expected === actual
  }

  private isNonEmpty_(value: string) {
    return value.length > 0
  }

  private computeShowNoProxyRisk_(enabled: boolean) {
    return !enabled
  }

}

customElements.define(
  SettingsFingerprintProfileProxySubpageElement.is,
  SettingsFingerprintProfileProxySubpageElement)
