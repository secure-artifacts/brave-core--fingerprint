/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

import {sendWithPromise} from 'chrome://resources/js/cr.js'

export interface ProfileProxyGeo {
  countryCode: string
  countryName: string
  regionName: string
  cityName: string
  timezone: string
  latitude: number
  longitude: number
  acceptLanguages: string
}

export interface ProfileProxyState {
  state: string
  statusCode: string
  warningCode: string
  netError: number
  statusMessage: string
  changeWarning: string
  enabled: boolean
  scheme: string
  host: string
  port: number
  username: string
  hasSavedPassword: boolean
  egressIp: string
  geoProvider: string
  lastVerified: number
  geo?: ProfileProxyGeo
}

export interface ProfileProxyDraft {
  scheme: string
  host: string
  port: number
  username: string
  password: string
}

export interface ProxyVerificationResult {
  success: boolean
  verificationId: string
  errorCode: string
  netError: number
  error: string
  egressIp: string
  geoProvider: string
  geo?: ProfileProxyGeo
}

export interface ProxyApplyResult {
  success: boolean
  errorCode: string
  netError: number
  error: string
}

export interface FingerprintProfileProxyBrowserProxy {
  getState: () => Promise<ProfileProxyState>
  verifyDraft: (config: ProfileProxyDraft) =>
    Promise<ProxyVerificationResult>
  applyVerified: (verificationId: string) => Promise<ProxyApplyResult>
  revalidate: () => Promise<ProxyVerificationResult>
  disable: () => Promise<ProxyApplyResult>
}

export class FingerprintProfileProxyBrowserProxyImpl
implements FingerprintProfileProxyBrowserProxy {
  static getInstance() {
    return instance || (instance = new FingerprintProfileProxyBrowserProxyImpl())
  }

  static setInstance(obj: FingerprintProfileProxyBrowserProxy) {
    instance = obj
  }

  getState() {
    return sendWithPromise<ProfileProxyState>(
      'fingerprint_profile_proxy.getState')
  }

  verifyDraft(config: ProfileProxyDraft) {
    return sendWithPromise<ProxyVerificationResult>(
      'fingerprint_profile_proxy.verifyDraft', config)
  }

  applyVerified(verificationId: string) {
    return sendWithPromise<ProxyApplyResult>(
      'fingerprint_profile_proxy.applyVerified', verificationId)
  }

  revalidate() {
    return sendWithPromise<ProxyVerificationResult>(
      'fingerprint_profile_proxy.revalidate')
  }

  disable() {
    return sendWithPromise<ProxyApplyResult>(
      'fingerprint_profile_proxy.disable')
  }
}

let instance: FingerprintProfileProxyBrowserProxy|null = null
