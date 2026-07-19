/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/browser/fingerprint_browser/fingerprint_proxy_service.h"

#include <cstdint>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "brave/components/fingerprint_browser/browser/pref_names.h"
#include "brave/net/proxy_resolution/profile_proxy_config_service.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/os_crypt/async/browser/os_crypt_async.h"
#include "components/os_crypt/async/common/encryptor.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_server.h"
#include "net/http/http_response_headers.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "services/cert_verifier/public/mojom/cert_verifier_service_factory.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace fingerprint_browser {
namespace {

constexpr base::TimeDelta kVerificationTimeout = base::Seconds(6);
constexpr base::TimeDelta kVerificationLifetime = base::Minutes(5);
constexpr base::TimeDelta kRevalidationInterval = base::Minutes(15);
constexpr size_t kMaxResponseSize = 128 * 1024;
constexpr char kFreeIpApiUrl[] = "https://free.freeipapi.com/api/json";
constexpr char kIpWhoIsUrl[] = "https://ipwho.is/";

GURL& FreeIpApiUrl() {
  static base::NoDestructor<GURL> url(kFreeIpApiUrl);
  return *url;
}

GURL& IpWhoIsUrl() {
  static base::NoDestructor<GURL> url(kIpWhoIsUrl);
  return *url;
}

constexpr net::NetworkTrafficAnnotationTag kProxyVerificationTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("fingerprint_proxy_verification", R"(
      semantics {
        sender: "Fingerprint Browser Proxy Verification"
        description:
          "Checks the public exit IP and geographic attributes of a proxy "
          "configured by the user."
        trigger:
          "The user verifies a proxy or an active proxy is periodically "
          "revalidated."
        data:
          "The request exposes the proxy exit IP to the selected IP location "
          "provider. It carries no browser cookies or user credentials."
        destination: OTHER
        destination_other:
          "FreeIPAPI, with IPWHOIS.IO as a fallback."
      }
      policy {
        cookies_allowed: NO
        setting:
          "Users can configure or disable the profile proxy in settings."
        policy_exception_justification: "Not implemented."
      })");

bool DraftsEqual(const ProfileProxyDraft& left,
                 const ProfileProxyDraft& right) {
  return left.scheme == right.scheme && left.host == right.host &&
         left.port == right.port && left.username == right.username &&
         left.password == right.password;
}

bool DraftMatchesSavedProxyIdentity(const ProfileProxyDraft& draft,
                                    const PrefService& pref_service) {
  return draft.scheme ==
             pref_service.GetString(prefs::kProfileProxyScheme) &&
         draft.host == pref_service.GetString(prefs::kProfileProxyHost) &&
         draft.port == pref_service.GetInteger(prefs::kProfileProxyPort) &&
         draft.username ==
             pref_service.GetString(prefs::kProfileProxyUsername);
}

bool HasSavedProxyPassword(const PrefService& pref_service) {
  return !pref_service.GetString(prefs::kProfileProxyEncryptedPassword)
              .empty() ||
         !pref_service.GetString(prefs::kProfileProxyPassword).empty();
}

std::optional<net::ProxyServer> BlockingProxyServer() {
  return net::ProxyServer::FromSchemeHostAndPort(net::ProxyServer::SCHEME_HTTP,
                                                 "127.0.0.1", 9);
}

bool IsProxyConnectionError(int net_error) {
  switch (net_error) {
    case net::ERR_INVALID_AUTH_CREDENTIALS:
    case net::ERR_PROXY_AUTH_UNSUPPORTED:
    case net::ERR_PROXY_CONNECTION_FAILED:
    case net::ERR_SOCKS_CONNECTION_FAILED:
    case net::ERR_TUNNEL_CONNECTION_FAILED:
      return true;
    default:
      return false;
  }
}

ProfileProxyGeo ToProfileProxyGeo(const ProxyGeoLookupResult& lookup) {
  ProfileProxyGeo geo;
  geo.country_code = lookup.country_code;
  geo.country_name = lookup.country_name;
  geo.region_name = lookup.region_name;
  geo.city_name = lookup.city_name;
  geo.timezone = lookup.timezone;
  geo.latitude = lookup.latitude;
  geo.longitude = lookup.longitude;
  geo.accept_languages = AcceptLanguagesForCountryCode(lookup.country_code)
                             .value_or(std::string());
  return geo;
}

base::Time ReadTimePref(const PrefService& prefs, const char* pref_name) {
  const int64_t milliseconds = prefs.GetInt64(pref_name);
  if (milliseconds <= 0) {
    return base::Time();
  }
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Milliseconds(milliseconds));
}

void WriteTimePref(PrefService& prefs, const char* pref_name, base::Time time) {
  prefs.SetInt64(pref_name, time.ToDeltaSinceWindowsEpoch().InMilliseconds());
}

}  // namespace

FingerprintProxyService::FingerprintProxyService(Profile* profile)
    : profile_(profile), prefs_(profile->GetPrefs()) {
  content::GetNetworkConnectionTracker()->AddNetworkConnectionObserver(this);
  proxy_control_pref_change_registrar_.Init(prefs_);
  const auto proxy_control_changed = base::BindRepeating(
      &FingerprintProxyService::OnProxyControlChanged,
      base::Unretained(this));
  proxy_control_pref_change_registrar_.Add(proxy_config::prefs::kProxy,
                                           proxy_control_changed);
  proxy_control_pref_change_registrar_.Add(
      proxy_config::prefs::kProxyOverrideRules, proxy_control_changed);
  g_browser_process->os_crypt_async()->GetInstance(base::BindOnce(
      &FingerprintProxyService::OnEncryptorReady, weak_factory_.GetWeakPtr()));

  const ProfileProxyConfigConflict conflict =
      GetProfileProxyConfigConflict(*prefs_);
  if (conflict != ProfileProxyConfigConflict::kNone) {
    SetState(kProxyStateConflict, ProfileProxyConfigConflictWarning(conflict));
  } else if (prefs_->GetBoolean(prefs::kProfileProxyEnabled)) {
    SetState(kProxyStateStale, "Proxy is waiting for verification.");
  } else {
    SetState(kProxyStateUnconfigured, std::string_view());
  }
}

FingerprintProxyService::~FingerprintProxyService() {
  content::GetNetworkConnectionTracker()->RemoveNetworkConnectionObserver(this);
}

FingerprintProxyState FingerprintProxyService::GetState() const {
  FingerprintProxyState state;
  state.state = prefs_->GetString(prefs::kProfileProxyState);
  state.status_message = prefs_->GetString(prefs::kProfileProxyStatusMessage);
  state.change_warning = prefs_->GetString(prefs::kProfileProxyChangeWarning);
  state.scheme = prefs_->GetString(prefs::kProfileProxyScheme);
  state.host = prefs_->GetString(prefs::kProfileProxyHost);
  state.port = prefs_->GetInteger(prefs::kProfileProxyPort);
  state.username = prefs_->GetString(prefs::kProfileProxyUsername);
  state.enabled = prefs_->GetBoolean(prefs::kProfileProxyEnabled);
  state.has_saved_password =
      !prefs_->GetString(prefs::kProfileProxyEncryptedPassword).empty() ||
      !prefs_->GetString(prefs::kProfileProxyPassword).empty();
  state.egress_ip = prefs_->GetString(prefs::kProfileProxyEgressIp);
  state.geo_provider = prefs_->GetString(prefs::kProfileProxyGeoProvider);
  state.last_verified =
      ReadTimePref(*prefs_, prefs::kProfileProxyLastVerifiedTime);
  state.geo = GetProfileProxyGeoForPrefs(*prefs_);

  const ProfileProxyConfigConflict conflict =
      GetProfileProxyConfigConflict(*prefs_);
  if (conflict != ProfileProxyConfigConflict::kNone) {
    state.state = kProxyStateConflict;
    state.status_message =
        std::string(ProfileProxyConfigConflictWarning(conflict));
  }
  return state;
}

void FingerprintProxyService::VerifyDraft(ProfileProxyDraft draft,
                                          VerificationCallback callback) {
  base::TrimWhitespaceASCII(draft.host, base::TRIM_ALL, &draft.host);
  if (draft.password.empty() && HasSavedProxyPassword(*prefs_)) {
    if (!DraftMatchesSavedProxyIdentity(draft, *prefs_)) {
      ProxyVerificationResult result;
      result.error =
          "Enter the proxy password again after changing proxy details.";
      std::move(callback).Run(std::move(result));
      return;
    }
    const std::optional<std::string> saved_password = GetSavedPassword();
    if (HasCredentialFailure()) {
      ProxyVerificationResult result;
      result.error = "Saved proxy credentials could not be unlocked.";
      std::move(callback).Run(std::move(result));
      return;
    }
    draft.password = saved_password.value_or(std::string());
  }

  if (!BuildProfileProxyServer(draft)) {
    ProxyVerificationResult result;
    result.error = "Enter a valid proxy protocol, host, and port.";
    std::move(callback).Run(std::move(result));
    return;
  }

  const ProfileProxyConfigConflict conflict =
      GetProfileProxyConfigConflict(*prefs_);
  if (conflict != ProfileProxyConfigConflict::kNone) {
    SetState(kProxyStateConflict, ProfileProxyConfigConflictWarning(conflict));
    ProxyVerificationResult result;
    result.error = std::string(ProfileProxyConfigConflictWarning(conflict));
    std::move(callback).Run(std::move(result));
    return;
  }

  pending_verification_.reset();
  StartVerification(std::move(draft), false, std::move(callback));
}

void FingerprintProxyService::ApplyVerified(std::string verification_id,
                                            ApplyCallback callback) {
  ProxyApplyResult result;
  if (!pending_verification_ || pending_verification_->id != verification_id) {
    result.error = "Verification is missing or was already used.";
    std::move(callback).Run(std::move(result));
    return;
  }
  if (base::TimeTicks::Now() >= pending_verification_->expires_at) {
    pending_verification_.reset();
    result.error = "Verification expired. Verify the proxy again.";
    std::move(callback).Run(std::move(result));
    return;
  }
  const ProfileProxyConfigConflict conflict =
      GetProfileProxyConfigConflict(*prefs_);
  if (conflict != ProfileProxyConfigConflict::kNone) {
    pending_verification_.reset();
    SetState(kProxyStateConflict, ProfileProxyConfigConflictWarning(conflict));
    result.error = std::string(ProfileProxyConfigConflictWarning(conflict));
    std::move(callback).Run(std::move(result));
    return;
  }

  PendingVerification pending = std::move(*pending_verification_);
  pending_verification_.reset();
  if (!StorePassword(pending.draft.password)) {
    SetState(kProxyStateError, "Proxy credentials could not be encrypted.");
    result.error = "Proxy credentials could not be encrypted.";
    std::move(callback).Run(std::move(result));
    return;
  }

  prefs_->SetString(prefs::kProfileProxyScheme, pending.draft.scheme);
  prefs_->SetString(prefs::kProfileProxyHost, pending.draft.host);
  prefs_->SetInteger(prefs::kProfileProxyPort, pending.draft.port);
  prefs_->SetString(prefs::kProfileProxyUsername, pending.draft.username);
  prefs_->ClearPref(prefs::kProfileProxyPassword);
  prefs_->SetBoolean(prefs::kProfileProxyEnabled, true);

  ClearProfileProxyLastError(*prefs_);
  ApplyLookup(pending.lookup, pending.geo, false);
  SyncProfileProxyWebRTCPolicy(*prefs_);
  prefs_->SetInteger(
      prefs::kProfileProxyCredentialGeneration,
      prefs_->GetInteger(prefs::kProfileProxyCredentialGeneration) + 1);
  content::WebContents::SyncRendererPrefsForBrowserContext(profile_);
  ScheduleRevalidation();

  result.success = true;
  std::move(callback).Run(std::move(result));
}

void FingerprintProxyService::Revalidate(VerificationCallback callback) {
  if (!prefs_->GetBoolean(prefs::kProfileProxyEnabled)) {
    ProxyVerificationResult result;
    result.error = "No active proxy is configured.";
    if (callback) {
      std::move(callback).Run(std::move(result));
    }
    return;
  }

  ProfileProxyDraft draft = GetAppliedDraft();
  if (HasCredentialFailure() || !BuildProfileProxyServer(draft)) {
    SetState(kProxyStateError, "Saved proxy credentials are unavailable.");
    ProxyVerificationResult result;
    result.error = "Saved proxy credentials are unavailable.";
    if (callback) {
      std::move(callback).Run(std::move(result));
    }
    return;
  }
  StartVerification(std::move(draft), true, std::move(callback));
}

void FingerprintProxyService::Disable(DisableCallback callback) {
  pending_verification_.reset();
  verification_loader_.reset();
  verification_url_loader_factory_.reset();
  verification_network_context_.reset();
  verification_in_progress_ = false;
  ProxyVerificationResult cancelled;
  cancelled.error = "Proxy verification was cancelled.";
  RunVerificationCallbacks(std::move(cancelled));
  revalidation_timer_.Stop();

  prefs_->SetBoolean(prefs::kProfileProxyEnabled, false);
  ClearProfileProxyLastError(*prefs_);
  SyncProfileProxyWebRTCPolicy(*prefs_);
  ClearVerifiedProfileProxyGeo(*prefs_);
  SetState(kProxyStateUnconfigured, std::string_view());
  prefs_->SetInteger(
      prefs::kProfileProxyCredentialGeneration,
      prefs_->GetInteger(prefs::kProfileProxyCredentialGeneration) + 1);
  content::WebContents::SyncRendererPrefsForBrowserContext(profile_);
  std::move(callback).Run();
}

std::optional<net::ProxyServer> FingerprintProxyService::GetProxyServer()
    const {
  if (!prefs_->GetBoolean(prefs::kProfileProxyEnabled) ||
      GetProfileProxyConfigConflict(*prefs_) !=
          ProfileProxyConfigConflict::kNone) {
    return std::nullopt;
  }

  ProfileProxyDraft draft = GetAppliedDraft();
  if (HasCredentialFailure()) {
    return BlockingProxyServer();
  }
  const std::optional<net::ProxyServer> proxy_server =
      BuildProfileProxyServer(draft);
  return proxy_server ? proxy_server : BlockingProxyServer();
}

void FingerprintProxyService::ReportProxyError(int net_error) {
  if (!prefs_->GetBoolean(prefs::kProfileProxyEnabled) ||
      !IsProxyConnectionError(net_error)) {
    return;
  }
  SetProfileProxyLastError(*prefs_, net_error);
  SetState(kProxyStateError, prefs_->GetString(prefs::kProfileProxyLastError));
  ScheduleRevalidation();
}

void FingerprintProxyService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void FingerprintProxyService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

// static
void FingerprintProxyService::SetGeoProviderUrlsForTesting(
    const GURL& free_ip_api_url,
    const GURL& ip_who_is_url) {
  FreeIpApiUrl() = free_ip_api_url;
  IpWhoIsUrl() = ip_who_is_url;
}

// static
void FingerprintProxyService::ResetGeoProviderUrlsForTesting() {
  FreeIpApiUrl() = GURL(kFreeIpApiUrl);
  IpWhoIsUrl() = GURL(kIpWhoIsUrl);
}

bool FingerprintProxyService::IsCredentialStoreReadyForTesting() const {
  return encryptor_ != nullptr;
}

bool FingerprintProxyService::MigratePlaintextPasswordForTesting() {
  return MigratePlaintextPassword();
}

void FingerprintProxyService::ExpirePendingVerificationForTesting() {
  if (pending_verification_) {
    pending_verification_->expires_at = base::TimeTicks::Now();
  }
}

void FingerprintProxyService::OnEncryptorReady(
    scoped_refptr<os_crypt_async::Encryptor> encryptor) {
  encryptor_ = std::move(encryptor);
  if (!MigratePlaintextPassword()) {
    credential_failure_ = true;
    if (prefs_->GetBoolean(prefs::kProfileProxyEnabled)) {
      SetState(kProxyStateError,
               "Saved proxy credentials could not be encrypted.");
    }
    return;
  }

  const std::string& encrypted =
      prefs_->GetString(prefs::kProfileProxyEncryptedPassword);
  if (!encrypted.empty() && !GetSavedPassword()) {
    credential_failure_ = true;
    if (prefs_->GetBoolean(prefs::kProfileProxyEnabled)) {
      SetState(kProxyStateError,
               "Saved proxy credentials could not be unlocked.");
    }
    return;
  }

  credential_failure_ = false;
  if (prefs_->GetBoolean(prefs::kProfileProxyEnabled)) {
    prefs_->SetInteger(
        prefs::kProfileProxyCredentialGeneration,
        prefs_->GetInteger(prefs::kProfileProxyCredentialGeneration) + 1);
    Revalidate(VerificationCallback());
    ScheduleRevalidation();
  }
}

bool FingerprintProxyService::MigratePlaintextPassword() {
  const std::string& plaintext =
      prefs_->GetString(prefs::kProfileProxyPassword);
  if (plaintext.empty()) {
    prefs_->ClearPref(prefs::kProfileProxyPassword);
    return true;
  }
  if (!StorePassword(plaintext)) {
    return false;
  }
  prefs_->ClearPref(prefs::kProfileProxyPassword);
  prefs_->SetInteger(
      prefs::kProfileProxyCredentialGeneration,
      prefs_->GetInteger(prefs::kProfileProxyCredentialGeneration) + 1);
  return true;
}

std::optional<std::string> FingerprintProxyService::GetSavedPassword() const {
  const std::string& encoded =
      prefs_->GetString(prefs::kProfileProxyEncryptedPassword);
  if (encoded.empty()) {
    const std::string& plaintext =
        prefs_->GetString(prefs::kProfileProxyPassword);
    return plaintext.empty() ? std::optional<std::string>()
                             : std::optional<std::string>(plaintext);
  }
  if (!encryptor_) {
    return std::nullopt;
  }

  std::string encrypted;
  std::string password;
  if (!base::Base64Decode(encoded, &encrypted) ||
      !encryptor_->DecryptString(encrypted, &password)) {
    return std::nullopt;
  }
  return password;
}

bool FingerprintProxyService::StorePassword(std::string_view password) {
  if (password.empty()) {
    prefs_->ClearPref(prefs::kProfileProxyEncryptedPassword);
    credential_failure_ = false;
    return true;
  }
  if (!encryptor_) {
    return false;
  }

  std::string encrypted;
  if (!encryptor_->EncryptString(std::string(password), &encrypted)) {
    return false;
  }
  prefs_->SetString(prefs::kProfileProxyEncryptedPassword,
                    base::Base64Encode(encrypted));
  credential_failure_ = false;
  return true;
}

void FingerprintProxyService::StartVerification(ProfileProxyDraft draft,
                                                bool is_revalidation,
                                                VerificationCallback callback) {
  if (verification_in_progress_) {
    if (is_revalidation && verification_is_revalidation_ &&
        DraftsEqual(draft, verification_draft_)) {
      if (callback) {
        verification_callbacks_.push_back(std::move(callback));
      }
      return;
    }
    ProxyVerificationResult result;
    result.error = "Another proxy verification is already running.";
    if (callback) {
      std::move(callback).Run(std::move(result));
    }
    return;
  }

  verification_in_progress_ = true;
  verification_is_revalidation_ = is_revalidation;
  state_before_verification_ = prefs_->GetString(prefs::kProfileProxyState);
  status_before_verification_ =
      prefs_->GetString(prefs::kProfileProxyStatusMessage);
  warning_before_verification_ =
      prefs_->GetString(prefs::kProfileProxyChangeWarning);
  verification_draft_ = std::move(draft);
  verification_callbacks_.clear();
  if (callback) {
    verification_callbacks_.push_back(std::move(callback));
  }
  SetState(kProxyStateVerifying, "Checking proxy exit location.");
  CreateVerificationNetworkContext(verification_draft_);
  StartLookup(ProxyGeoProvider::kFreeIpApi);
}

void FingerprintProxyService::CreateVerificationNetworkContext(
    const ProfileProxyDraft& draft) {
  verification_loader_.reset();
  verification_url_loader_factory_.reset();
  verification_network_context_.reset();

  const std::optional<net::ProxyServer> proxy_server =
      BuildProfileProxyServer(draft);
  CHECK(proxy_server);

  net::ProxyConfig proxy_config;
  proxy_config.proxy_rules().type =
      net::ProxyConfig::ProxyRules::Type::PROXY_LIST;
  proxy_config.proxy_rules().single_proxies.SetSingleProxyServer(*proxy_server);
  proxy_config.proxy_rules().bypass_rules.AddRulesToSubtractImplicit();

  auto context_params = network::mojom::NetworkContextParams::New();
  context_params->http_cache_enabled = false;
  context_params->enable_referrers = false;
  context_params->cert_verifier_params = content::GetCertVerifierParams(
      cert_verifier::mojom::CertVerifierCreationParams::New());
  context_params->initial_proxy_config = net::ProxyConfigWithAnnotation(
      proxy_config, kProxyVerificationTrafficAnnotation);
  content::CreateNetworkContextInNetworkService(
      verification_network_context_.BindNewPipeAndPassReceiver(),
      std::move(context_params));

  auto factory_params = network::mojom::URLLoaderFactoryParams::New();
  factory_params->process_id = network::OriginatingProcessId::browser();
  factory_params->is_trusted = true;
  factory_params->is_orb_enabled = false;
  mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_factory;
  verification_network_context_->CreateURLLoaderFactory(
      pending_factory.InitWithNewPipeAndPassReceiver(),
      std::move(factory_params));
  verification_url_loader_factory_ =
      base::MakeRefCounted<network::WrapperSharedURLLoaderFactory>(
          std::move(pending_factory));
}

void FingerprintProxyService::StartLookup(ProxyGeoProvider provider) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url =
      provider == ProxyGeoProvider::kFreeIpApi ? FreeIpApiUrl() : IpWhoIsUrl();
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->redirect_mode = network::mojom::RedirectMode::kError;
  request->skip_service_worker = true;
  request->load_flags = net::LOAD_BYPASS_CACHE | net::LOAD_DISABLE_CACHE;

  verification_loader_ = network::SimpleURLLoader::Create(
      std::move(request), kProxyVerificationTrafficAnnotation);
  verification_loader_->SetTimeoutDuration(kVerificationTimeout);
  verification_loader_->SetAllowHttpErrorResults(true);
  verification_loader_->DownloadToString(
      verification_url_loader_factory_.get(),
      base::BindOnce(&FingerprintProxyService::OnLookupComplete,
                     weak_factory_.GetWeakPtr(), provider),
      kMaxResponseSize);
}

void FingerprintProxyService::OnLookupComplete(
    ProxyGeoProvider provider,
    std::optional<std::string> response_body) {
  const int net_error = verification_loader_->NetError();
  int response_code = 0;
  if (verification_loader_->ResponseInfo() &&
      verification_loader_->ResponseInfo()->headers) {
    response_code =
        verification_loader_->ResponseInfo()->headers->response_code();
  }
  verification_loader_.reset();

  std::optional<ProxyGeoLookupResult> lookup;
  if (net_error == net::OK && response_code >= 200 && response_code < 300 &&
      response_body) {
    lookup = ParseProxyGeoResponse(provider, *response_body);
  }
  if (lookup) {
    FinishLookupSuccess(std::move(*lookup));
    return;
  }

  if (provider == ProxyGeoProvider::kFreeIpApi) {
    StartLookup(ProxyGeoProvider::kIpWhoIs);
    return;
  }
  FinishLookupFailure(
      IsProxyConnectionError(net_error)
          ? "Proxy connection or authentication failed."
          : "Proxy location services are temporarily unavailable.",
      net_error);
}

void FingerprintProxyService::FinishLookupFailure(std::string error,
                                                  int net_error) {
  verification_loader_.reset();
  verification_url_loader_factory_.reset();
  verification_network_context_.reset();
  verification_in_progress_ = false;

  if (verification_is_revalidation_) {
    if (IsProxyConnectionError(net_error)) {
      SetProfileProxyLastError(*prefs_, net_error);
      SetState(kProxyStateError, error);
    } else {
      SetState(kProxyStateStale, error);
    }
    ScheduleRevalidation();
  } else if (prefs_->GetBoolean(prefs::kProfileProxyEnabled) &&
             !state_before_verification_.empty()) {
    SetState(state_before_verification_, status_before_verification_,
             warning_before_verification_);
  } else {
    SetState(kProxyStateError, error);
  }

  ProxyVerificationResult result;
  result.error = std::move(error);
  RunVerificationCallbacks(std::move(result));
}

void FingerprintProxyService::FinishLookupSuccess(ProxyGeoLookupResult lookup) {
  verification_loader_.reset();
  verification_url_loader_factory_.reset();
  verification_network_context_.reset();
  verification_in_progress_ = false;

  ProfileProxyGeo geo = ToProfileProxyGeo(lookup);
  if (geo.accept_languages.empty()) {
    FinishLookupFailure("Proxy country could not be mapped to a language.",
                        net::ERR_FAILED);
    return;
  }

  ProxyVerificationResult result;
  result.success = true;
  result.egress_ip = lookup.ip_address;
  result.geo_provider = std::string(ProxyGeoProviderName(lookup.provider));
  result.geo = geo;

  if (verification_is_revalidation_) {
    ApplyLookup(lookup, geo, true);
    ClearProfileProxyLastError(*prefs_);
    ScheduleRevalidation();
  } else {
    PendingVerification pending;
    pending.id = base::Uuid::GenerateRandomV4().AsLowercaseString();
    pending.draft = verification_draft_;
    pending.lookup = lookup;
    pending.geo = geo;
    pending.expires_at = base::TimeTicks::Now() + kVerificationLifetime;
    result.verification_id = pending.id;
    pending_verification_ = std::move(pending);
    SetState(kProxyStateAwaitingConfirmation,
             "Proxy verified. Confirm to apply it.");
  }
  RunVerificationCallbacks(std::move(result));
}

void FingerprintProxyService::RunVerificationCallbacks(
    ProxyVerificationResult result) {
  std::vector<VerificationCallback> callbacks =
      std::move(verification_callbacks_);
  verification_callbacks_.clear();
  for (auto& callback : callbacks) {
    std::move(callback).Run(result);
  }
}

ProfileProxyDraft FingerprintProxyService::GetAppliedDraft() const {
  ProfileProxyDraft draft;
  draft.scheme = prefs_->GetString(prefs::kProfileProxyScheme);
  draft.host = prefs_->GetString(prefs::kProfileProxyHost);
  draft.port = prefs_->GetInteger(prefs::kProfileProxyPort);
  draft.username = prefs_->GetString(prefs::kProfileProxyUsername);
  draft.password = GetSavedPassword().value_or(std::string());
  return draft;
}

void FingerprintProxyService::ApplyLookup(const ProxyGeoLookupResult& lookup,
                                          const ProfileProxyGeo& geo,
                                          bool show_change_warning) {
  std::string change_warning;
  if (show_change_warning) {
    const std::string& previous_ip =
        prefs_->GetString(prefs::kProfileProxyEgressIp);
    const std::string& previous_country =
        prefs_->GetString(prefs::kProfileProxyDerivedGeoCountryCode);
    if (!previous_country.empty() && previous_country != geo.country_code) {
      change_warning = "Proxy country changed. Fingerprint settings updated.";
    } else if (!previous_ip.empty() && previous_ip != lookup.ip_address) {
      change_warning = "Proxy exit IP changed.";
    } else if (base::StartsWith(warning_before_verification_,
                                "Proxy country changed.")) {
      change_warning = warning_before_verification_;
    }
  }

  ApplyVerifiedProfileProxyGeo(*prefs_, geo);
  prefs_->SetString(prefs::kProfileProxyEgressIp, lookup.ip_address);
  prefs_->SetString(prefs::kProfileProxyGeoProvider,
                    ProxyGeoProviderName(lookup.provider));
  WriteTimePref(*prefs_, prefs::kProfileProxyLastVerifiedTime,
                base::Time::Now());
  SetState(kProxyStateActive, "Proxy is active.", change_warning);
  SyncProfileProxyWebRTCPolicy(*prefs_);
  content::WebContents::SyncRendererPrefsForBrowserContext(profile_);
}

void FingerprintProxyService::SetState(std::string_view state,
                                       std::string_view message,
                                       std::string_view change_warning) {
  prefs_->SetString(prefs::kProfileProxyState, state);
  prefs_->SetString(prefs::kProfileProxyStatusMessage, message);
  prefs_->SetString(prefs::kProfileProxyChangeWarning, change_warning);
  NotifyObservers();
}

void FingerprintProxyService::NotifyObservers() {
  for (Observer& observer : observers_) {
    observer.OnFingerprintProxyStateChanged();
  }
}

void FingerprintProxyService::OnProxyControlChanged() {
  const ProfileProxyConfigConflict conflict =
      GetProfileProxyConfigConflict(*prefs_);
  if (conflict != ProfileProxyConfigConflict::kNone) {
    pending_verification_.reset();
    verification_loader_.reset();
    verification_url_loader_factory_.reset();
    verification_network_context_.reset();
    const bool was_verifying = verification_in_progress_;
    verification_in_progress_ = false;
    revalidation_timer_.Stop();
    SetState(kProxyStateConflict, ProfileProxyConfigConflictWarning(conflict));
    if (was_verifying) {
      ProxyVerificationResult result;
      result.error = std::string(ProfileProxyConfigConflictWarning(conflict));
      RunVerificationCallbacks(std::move(result));
    }
    return;
  }

  if (!prefs_->GetBoolean(prefs::kProfileProxyEnabled)) {
    SetState(kProxyStateUnconfigured, std::string_view());
    return;
  }

  SetState(kProxyStateStale, "Proxy control changed. Revalidating.");
  if (encryptor_) {
    Revalidate(VerificationCallback());
  }
  ScheduleRevalidation();
}

void FingerprintProxyService::ScheduleRevalidation() {
  if (!prefs_->GetBoolean(prefs::kProfileProxyEnabled) ||
      GetProfileProxyConfigConflict(*prefs_) !=
          ProfileProxyConfigConflict::kNone) {
    revalidation_timer_.Stop();
    return;
  }
  revalidation_timer_.Start(
      FROM_HERE, kRevalidationInterval,
      base::BindRepeating(&FingerprintProxyService::OnRevalidationTimer,
                          weak_factory_.GetWeakPtr()));
}

void FingerprintProxyService::OnRevalidationTimer() {
  Revalidate(VerificationCallback());
}

bool FingerprintProxyService::HasCredentialFailure() const {
  return credential_failure_ ||
         (!prefs_->GetString(prefs::kProfileProxyEncryptedPassword).empty() &&
          !GetSavedPassword());
}

void FingerprintProxyService::OnConnectionChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  if (type != net::NetworkChangeNotifier::CONNECTION_NONE &&
      prefs_->GetBoolean(prefs::kProfileProxyEnabled)) {
    Revalidate(VerificationCallback());
  }
}

}  // namespace fingerprint_browser
