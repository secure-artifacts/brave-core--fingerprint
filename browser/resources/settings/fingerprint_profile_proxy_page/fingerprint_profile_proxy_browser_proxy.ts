/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import {sendWithPromise} from 'chrome://resources/js/cr.js'

export interface ProfileProxyConfig {
  enabled: boolean
  scheme: string
  host: string
  port: number
  username: string
  password: string
  conflictWarning: string
  manualCountryCode: string
  manualTimezone: string
  manualLatitude: string
  manualLongitude: string
  geoWarning: string
  derivedCountryCode: string
  derivedTimezone: string
  derivedLatitude: string
  derivedLongitude: string
}

export interface ProfileProxySaveResult {
  success: boolean
  error: string
  conflictWarning: string
  geoWarning: string
}

export interface ProfileProxyLastError {
  message: string
  code: number
}

export interface FingerprintProfileProxyBrowserProxy {
  getConfig: () => Promise<ProfileProxyConfig>
  setConfig: (config: ProfileProxyConfig) => Promise<ProfileProxySaveResult>
  getLastError: () => Promise<ProfileProxyLastError>
}

export class FingerprintProfileProxyBrowserProxyImpl
implements FingerprintProfileProxyBrowserProxy {
  static getInstance() {
    return instance || (instance = new FingerprintProfileProxyBrowserProxyImpl())
  }

  static setInstance(obj: FingerprintProfileProxyBrowserProxy) {
    instance = obj
  }

  getConfig() {
    return sendWithPromise<ProfileProxyConfig>(
      'fingerprint_profile_proxy.getConfig')
  }

  setConfig(config: ProfileProxyConfig) {
    return sendWithPromise<ProfileProxySaveResult>(
      'fingerprint_profile_proxy.setConfig', config)
  }

  getLastError() {
    return sendWithPromise<ProfileProxyLastError>(
      'fingerprint_profile_proxy.getLastError')
  }
}

let instance: FingerprintProfileProxyBrowserProxy|null = null
