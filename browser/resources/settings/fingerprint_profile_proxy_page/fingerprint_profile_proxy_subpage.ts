/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import 'chrome://resources/cr_elements/cr_button/cr_button.js'
import 'chrome://resources/cr_elements/cr_input/cr_input.js'

import {I18nMixin} from 'chrome://resources/cr_elements/i18n_mixin.js'
import {WebUiListenerMixin} from 'chrome://resources/cr_elements/web_ui_listener_mixin.js'
import {PolymerElement} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js'

import {loadTimeData} from '../i18n_setup.js'

import {FingerprintProfileProxyBrowserProxyImpl} from './fingerprint_profile_proxy_browser_proxy.js'
import type {
  FingerprintProfileProxyBrowserProxy,
  ProfileProxyDraft,
  ProfileProxyGeo,
  ProfileProxyState,
  ProxyVerificationResult,
} from './fingerprint_profile_proxy_browser_proxy.js'
import {getTemplate} from './fingerprint_profile_proxy_subpage.html.js'

const SettingsFingerprintProfileProxySubpageElementBase =
  WebUiListenerMixin(I18nMixin(PolymerElement))

const ISO_COUNTRY_CODES =
  'ad ae af ag ai al am ao aq ar as at au aw ax az ba bb bd be bf bg bh bi ' +
  'bj bl bm bn bo bq br bs bt bv bw by bz ca cc cd cf cg ch ci ck cl cm cn ' +
  'co cr cu cv cw cx cy cz de dj dk dm do dz ec ee eg eh er es et fi fj fk ' +
  'fm fo fr ga gb gd ge gf gg gh gi gl gm gn gp gq gr gs gt gu gw gy hk hm ' +
  'hn hr ht hu id ie il im in io iq ir is it je jm jo jp ke kg kh ki km kn ' +
  'kp kr kw ky kz la lb lc li lk lr ls lt lu lv ly ma mc md me mf mg mh mk ' +
  'ml mm mn mo mp mq mr ms mt mu mv mw mx my mz na nc ne nf ng ni nl no np ' +
  'nr nu nz om pa pe pf pg ph pk pl pm pn pr ps pt pw py qa re ro rs ru rw ' +
  'sa sb sc sd se sg sh si sj sk sl sm sn so sr ss st sv sx sy sz tc td tf ' +
  'tg th tj tk tl tm tn to tr tt tv tw tz ua ug um us uy uz va vc ve vg vi ' +
  'vn vu wf ws ye yt za zm zw'
const COUNTRY_CODE_LIST = ISO_COUNTRY_CODES.split(' ')
const FLAG_ATLAS_COLUMNS = 16
const FLAG_CELL_WIDTH = 64
const FLAG_CELL_HEIGHT = 48

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
      state_: {type: String, value: 'unconfigured'},
      statusCode_: {type: String, value: 'none'},
      warningCode_: {type: String, value: 'none'},
      statusMessage_: {type: String, value: ''},
      changeWarning_: {type: String, value: ''},
      enabled_: {type: Boolean, value: false},
      hasSavedPassword_: {type: Boolean, value: false},
      egressIp_: {type: String, value: ''},
      geoProvider_: {type: String, value: ''},
      lastVerified_: {type: Number, value: 0},
      activeGeo_: {type: Object, value: null},
      verification_: {type: Object, value: null},
      scheme_: {type: String, value: 'http'},
      host_: {type: String, value: ''},
      port_: {type: String, value: ''},
      username_: {type: String, value: ''},
      password_: {type: String, value: ''},
      hostError_: {type: String, value: ''},
      portError_: {type: String, value: ''},
      actionError_: {type: String, value: ''},
      isBusy_: {type: Boolean, value: false},
      initialized_: {type: Boolean, value: false},
    }
  }

  private declare state_: string
  private declare statusCode_: string
  private declare warningCode_: string
  private declare statusMessage_: string
  private declare changeWarning_: string
  private declare enabled_: boolean
  private declare hasSavedPassword_: boolean
  private declare egressIp_: string
  private declare geoProvider_: string
  private declare lastVerified_: number
  private declare activeGeo_: ProfileProxyGeo|null
  private declare verification_: ProxyVerificationResult|null
  private declare scheme_: string
  private declare host_: string
  private declare port_: string
  private declare username_: string
  private declare password_: string
  private declare hostError_: string
  private declare portError_: string
  private declare actionError_: string
  private declare isBusy_: boolean
  private declare initialized_: boolean
  private draftRevision_ = 0

  private browserProxy_: FingerprintProfileProxyBrowserProxy =
    FingerprintProfileProxyBrowserProxyImpl.getInstance()

  override ready() {
    super.ready()

    if (loadTimeData.getBoolean('shouldExposeElementsForTesting')) {
      window.testing = window.testing || {}
      window.testing['fingerprintProfileProxySubpage'] = this.shadowRoot
    }

    this.addWebUiListener(
      'fingerprint-profile-proxy-state-changed',
      (state: ProfileProxyState) => this.setState_(state))
    this.refreshState_()
  }

  private async refreshState_() {
    this.setState_(await this.browserProxy_.getState())
  }

  private setState_(state: ProfileProxyState) {
    this.state_ = state.state || 'unconfigured'
    this.statusCode_ = state.statusCode || 'unknown'
    this.warningCode_ = state.warningCode || 'none'
    this.statusMessage_ = state.statusMessage || ''
    this.changeWarning_ = state.changeWarning || ''
    this.enabled_ = state.enabled
    this.hasSavedPassword_ = state.hasSavedPassword
    this.egressIp_ = state.egressIp || ''
    this.geoProvider_ = state.geoProvider || ''
    this.lastVerified_ = state.lastVerified || 0
    this.activeGeo_ = state.geo || null

    if (!this.initialized_) {
      this.scheme_ = state.scheme === 'https' ? 'https' : 'http'
      this.host_ = state.host || ''
      this.port_ = state.port ? String(state.port) : ''
      this.username_ = state.username || ''
      this.initialized_ = true
    }
  }

  private onSchemeChanged_(event: Event) {
    this.scheme_ = (event.target as HTMLSelectElement).value
    this.onInputChanged_()
  }

  private onInputChanged_() {
    this.draftRevision_++
    this.verification_ = null
    this.hostError_ = ''
    this.portError_ = ''
    this.actionError_ = ''
  }

  private validate_() {
    this.hostError_ = ''
    this.portError_ = ''
    this.actionError_ = ''

    if (!this.host_.trim()) {
      this.hostError_ = this.i18n('profileProxyHostRequired')
    }
    const port = Number(this.port_.trim())
    if (!/^\d+$/.test(this.port_.trim()) || port < 1 || port > 65535) {
      this.portError_ = this.i18n('profileProxyPortInvalid')
    }
    return !this.hostError_ && !this.portError_
  }

  private buildDraft_(): ProfileProxyDraft {
    return {
      scheme: this.scheme_,
      host: this.host_.trim(),
      port: Number(this.port_.trim()),
      username: this.username_,
      password: this.password_,
    }
  }

  private async onVerify_() {
    if (!this.validate_()) {
      return
    }
    this.isBusy_ = true
    this.verification_ = null
    const revision = this.draftRevision_
    const draft = this.buildDraft_()
    try {
      const result = await this.browserProxy_.verifyDraft(draft)
      if (revision !== this.draftRevision_) {
        return
      }
      if (!result.success) {
        this.actionError_ = this.resultError_(result)
        return
      }
      this.verification_ = result
    } catch {
      this.actionError_ = '代理验证失败，请稍后重试。'
    } finally {
      this.isBusy_ = false
    }
  }

  private async onApply_() {
    if (!this.verification_) {
      return
    }
    this.isBusy_ = true
    try {
      const result = await this.browserProxy_.applyVerified(
        this.verification_.verificationId)
      if (!result.success) {
        this.actionError_ = this.resultError_(result)
        this.verification_ = null
        return
      }
      this.password_ = ''
      this.verification_ = null
      await this.refreshState_()
    } catch {
      this.actionError_ = '无法应用代理，请重新验证后再试。'
    } finally {
      this.isBusy_ = false
    }
  }

  private async onRevalidate_() {
    this.isBusy_ = true
    this.actionError_ = ''
    try {
      const result = await this.browserProxy_.revalidate()
      if (!result.success) {
        this.actionError_ = this.resultError_(result)
      }
      await this.refreshState_()
    } catch {
      this.actionError_ = '代理复检失败，请稍后重试。'
    } finally {
      this.isBusy_ = false
    }
  }

  private async onDisable_() {
    this.isBusy_ = true
    this.actionError_ = ''
    try {
      const result = await this.browserProxy_.disable()
      if (!result.success) {
        this.actionError_ = this.resultError_(result)
        return
      }
      this.verification_ = null
      await this.refreshState_()
    } catch {
      this.actionError_ = '无法禁用代理，请稍后重试。'
    } finally {
      this.isBusy_ = false
    }
  }

  private schemeEqual_(expected: string, actual: string) {
    return expected === actual
  }

  private isNonEmpty_(value: string) {
    return value.length > 0
  }

  private resultError_(result: {error?: string, netError?: number}) {
    if (result.error && /[\u3400-\u9fff]/.test(result.error)) {
      return result.error
    }
    return result.netError ?
      `代理操作失败（网络错误：${result.netError}）` :
      '代理操作失败，请稍后重试。'
  }

  private isInteractionDisabled_(isBusy: boolean, state: string) {
    return isBusy || state === 'verifying'
  }

  private isActive_(state: string, geo: ProfileProxyGeo|null) {
    return !!geo && (state === 'active' || state === 'stale')
  }

  private showWarningState_(state: string) {
    return state === 'stale' || state === 'error' || state === 'conflict'
  }

  private showRecoveryActions_(enabled: boolean, state: string) {
    return enabled && (state === 'error' || state === 'conflict')
  }

  private warningStateClass_(state: string) {
    return state === 'error' ? 'error-row' : 'warning-row'
  }

  private passwordHint_(hasSavedPassword: boolean) {
    return hasSavedPassword ?
      this.i18n('profileProxyPasswordSavedHint') :
      this.i18n('profileProxyPasswordOptionalHint')
  }

  private countryFlagStyle_(countryCode: string) {
    const index = COUNTRY_CODE_LIST.indexOf((countryCode || '').toLowerCase())
    if (index < 0) {
      return 'background-image: none'
    }
    const x = index % FLAG_ATLAS_COLUMNS * FLAG_CELL_WIDTH
    const y = Math.floor(index / FLAG_ATLAS_COLUMNS) * FLAG_CELL_HEIGHT
    return `background-position: -${x}px -${y}px`
  }

  private countryFlagFallback_(countryCode: string) {
    const code = (countryCode || '').toUpperCase()
    return COUNTRY_CODE_LIST.includes(code.toLowerCase()) ? '' : code || '--'
  }

  private formatCoordinates_(geo: ProfileProxyGeo) {
    return `${geo.latitude.toFixed(4)}, ${geo.longitude.toFixed(4)}`
  }

  private formatLastVerified_(milliseconds: number) {
    return milliseconds ? new Date(milliseconds).toLocaleString() : '-'
  }
}

customElements.define(
  SettingsFingerprintProfileProxySubpageElement.is,
  SettingsFingerprintProfileProxySubpageElement)
