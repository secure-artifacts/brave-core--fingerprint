import fs from 'node:fs/promises'
import {isIP} from 'node:net'

import {pathExists} from './system.mjs'

const REQUIRED_PROXY_FIELDS = [
  'host',
  'port',
  'username',
  'password',
  'expectedIp',
  'countryCode',
  'timezone',
  'language',
  'latitude',
  'longitude',
  'geoVerifyUrl',
]

function validateProxy(name, value) {
  if (!value || typeof value !== 'object') {
    throw new Error(`Proxy fixture ${name} must be an object`)
  }
  for (const field of REQUIRED_PROXY_FIELDS) {
    if (value[field] === undefined || value[field] === null || value[field] === '') {
      throw new Error(`Proxy fixture ${name}.${field} is required`)
    }
  }
  if (!Number.isInteger(value.port) || value.port < 1 || value.port > 65535) {
    throw new Error(`Proxy fixture ${name}.port is invalid`)
  }
  if (!/^[A-Z]{2}$/.test(value.countryCode)) {
    throw new Error(`Proxy fixture ${name}.countryCode must be ISO alpha-2 uppercase`)
  }
  if (isIP(value.expectedIp) === 0) {
    throw new Error(`Proxy fixture ${name}.expectedIp must be an IP address`)
  }
  try {
    new Intl.DateTimeFormat('en-US', {timeZone: value.timezone}).format()
  } catch {
    throw new Error(`Proxy fixture ${name}.timezone must be an IANA time zone`)
  }
  try {
    Intl.getCanonicalLocales(value.language)
  } catch {
    throw new Error(`Proxy fixture ${name}.language must be a BCP-47 language tag`)
  }
  if (!Number.isFinite(value.latitude) || value.latitude < -90 || value.latitude > 90 ||
      !Number.isFinite(value.longitude) || value.longitude < -180 || value.longitude > 180) {
    throw new Error(`Proxy fixture ${name} coordinates are invalid`)
  }
  const verifyUrl = new URL(value.verifyUrl || 'https://api.ipify.org?format=json')
  if (verifyUrl.protocol !== 'https:') {
    throw new Error(`Proxy fixture ${name}.verifyUrl must use HTTPS`)
  }
  const geoVerifyUrl = new URL(value.geoVerifyUrl)
  if (geoVerifyUrl.protocol !== 'https:') {
    throw new Error(`Proxy fixture ${name}.geoVerifyUrl must use HTTPS`)
  }
  return {
    ...value,
    geoVerifyUrl: geoVerifyUrl.href,
    scheme: name,
    verifyUrl: verifyUrl.href,
  }
}

export async function loadProxyFixtures(file) {
  if (!file) {
    return {reason: '--proxy-fixtures was not provided', status: 'BLOCKED'}
  }
  if (!(await pathExists(file))) {
    return {reason: `Proxy fixture file does not exist: ${file}`, status: 'BLOCKED'}
  }
  const stat = await fs.lstat(file)
  if (stat.isSymbolicLink() || !stat.isFile()) {
    throw new Error('Proxy fixture must be a regular file, not a symlink')
  }
  const permissions = stat.mode & 0o777
  if (permissions !== 0o600) {
    throw new Error(
      `Proxy fixture permissions must be 0600, got ${permissions.toString(8).padStart(4, '0')}`)
  }
  const parsed = JSON.parse(await fs.readFile(file, 'utf8'))
  const http = parsed.http ? validateProxy('http', parsed.http) : null
  const socks5 = parsed.socks5 ? validateProxy('socks5', parsed.socks5) : null
  const missing = [
    !http && 'http',
    !socks5 && 'socks5',
  ].filter(Boolean)
  return {
    file,
    http,
    missing,
    reason: missing.length > 0
      ? `Proxy fixture is missing: ${missing.join(', ')}`
      : undefined,
    socks5,
    status: missing.length > 0 ? 'BLOCKED' : 'PASS',
  }
}

export function publicProxyRecord(proxy) {
  return {
    countryCode: proxy.countryCode,
    expectedIp: proxy.expectedIp,
    host: proxy.host,
    language: proxy.language,
    latitude: proxy.latitude,
    longitude: proxy.longitude,
    port: proxy.port,
    scheme: proxy.scheme,
    timezone: proxy.timezone,
    username: '<redacted>',
  }
}
