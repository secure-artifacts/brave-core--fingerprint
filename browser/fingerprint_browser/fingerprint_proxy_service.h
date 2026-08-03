/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_SERVICE_H_
#define BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "brave/components/fingerprint_browser/browser/profile_proxy_config.h"
#include "brave/components/fingerprint_browser/browser/proxy_geo_provider.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/network_change_notifier.h"
#include "services/network/public/cpp/network_connection_tracker.h"
#include "services/network/public/mojom/network_context.mojom.h"

class PrefService;
class Profile;
class GURL;

namespace net {
class ProxyServer;
}

namespace network {
class SharedURLLoaderFactory;
class SimpleURLLoader;
}  // namespace network

namespace os_crypt_async {
class Encryptor;
}

namespace fingerprint_browser {

inline constexpr char kProxyStateUnconfigured[] = "unconfigured";
inline constexpr char kProxyStateVerifying[] = "verifying";
inline constexpr char kProxyStateAwaitingConfirmation[] =
    "awaiting_confirmation";
inline constexpr char kProxyStateActive[] = "active";
inline constexpr char kProxyStateStale[] = "stale";
inline constexpr char kProxyStateError[] = "error";
inline constexpr char kProxyStateConflict[] = "conflict";

struct FingerprintProxyState {
  std::string state;
  std::string status_code;
  std::string warning_code;
  int net_error = 0;
  std::string scheme;
  std::string host;
  int port = 0;
  std::string username;
  bool enabled = false;
  bool has_saved_password = false;
  std::string egress_ip;
  std::string geo_provider;
  base::Time last_verified;
  std::optional<ProfileProxyGeo> geo;
};

struct ProxyVerificationResult {
  bool success = false;
  std::string verification_id;
  std::string error_code;
  int net_error = 0;
  std::string egress_ip;
  std::string geo_provider;
  std::optional<ProfileProxyGeo> geo;
};

struct ProxyApplyResult {
  bool success = false;
  std::string error_code;
  int net_error = 0;
};

class FingerprintProxyService
    : public KeyedService,
      public network::NetworkConnectionTracker::NetworkConnectionObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnFingerprintProxyStateChanged() = 0;
  };

  using VerificationCallback =
      base::OnceCallback<void(ProxyVerificationResult)>;
  using ApplyCallback = base::OnceCallback<void(ProxyApplyResult)>;
  using DisableCallback = base::OnceClosure;

  explicit FingerprintProxyService(Profile* profile);
  FingerprintProxyService(const FingerprintProxyService&) = delete;
  FingerprintProxyService& operator=(const FingerprintProxyService&) = delete;
  ~FingerprintProxyService() override;

  FingerprintProxyState GetState() const;
  void VerifyDraft(ProfileProxyDraft draft, VerificationCallback callback);
  void ApplyVerified(std::string verification_id, ApplyCallback callback);
  void Revalidate(VerificationCallback callback);
  void Disable(DisableCallback callback);

  std::optional<net::ProxyServer> GetProxyServer() const;
  void ReportProxyError(int net_error);

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  static void SetGeoProviderUrlsForTesting(const GURL& free_ip_api_url,
                                           const GURL& ip_who_is_url);
  static void ResetGeoProviderUrlsForTesting();
  bool IsCredentialStoreReadyForTesting() const;
  bool MigratePlaintextPasswordForTesting();
  void ExpirePendingVerificationForTesting();

 private:
  struct PendingVerification {
    std::string id;
    ProfileProxyDraft draft;
    ProxyGeoLookupResult lookup;
    ProfileProxyGeo geo;
    base::TimeTicks expires_at;
  };

  void OnEncryptorReady(scoped_refptr<os_crypt_async::Encryptor> encryptor);
  bool MigratePlaintextPassword();
  std::optional<std::string> GetSavedPassword() const;
  std::optional<std::string> EncryptPassword(std::string_view password) const;
  bool StorePassword(std::string_view password);

  void StartVerification(ProfileProxyDraft draft,
                         bool is_revalidation,
                         VerificationCallback callback);
  void CreateVerificationNetworkContext(const ProfileProxyDraft& draft);
  void StartLookup(ProxyGeoProvider provider);
  void OnLookupComplete(ProxyGeoProvider provider,
                        std::optional<std::string> response_body);
  void FinishLookupFailure(std::string_view error_code, int net_error);
  void FinishLookupSuccess(ProxyGeoLookupResult lookup);
  void RunVerificationCallbacks(ProxyVerificationResult result);

  ProfileProxyDraft GetAppliedDraft() const;
  std::string StoreLookupResult(const ProxyGeoLookupResult& lookup,
                                const ProfileProxyGeo& geo,
                                bool show_change_warning);
  void ApplyLookup(const ProxyGeoLookupResult& lookup,
                   const ProfileProxyGeo& geo,
                   bool show_change_warning);
  void SetState(std::string_view state,
                std::string_view status_code,
                std::string_view warning_code = {});
  void NotifyObservers();
  void OnProxyControlChanged();
  void ScheduleRevalidation();
  void OnRevalidationTimer();
  bool HasCredentialFailure() const;

  void OnConnectionChanged(
      net::NetworkChangeNotifier::ConnectionType type) override;

  raw_ptr<Profile> profile_;
  raw_ptr<PrefService> prefs_;
  scoped_refptr<os_crypt_async::Encryptor> encryptor_;
  bool credential_failure_ = false;
  bool apply_in_progress_ = false;
  bool verification_in_progress_ = false;
  bool verification_is_revalidation_ = false;
  std::string state_before_verification_;
  std::string status_before_verification_;
  std::string warning_before_verification_;
  ProfileProxyDraft verification_draft_;
  std::vector<VerificationCallback> verification_callbacks_;
  std::optional<PendingVerification> pending_verification_;
  PrefChangeRegistrar proxy_control_pref_change_registrar_;

  mojo::Remote<network::mojom::NetworkContext> verification_network_context_;
  scoped_refptr<network::SharedURLLoaderFactory>
      verification_url_loader_factory_;
  std::unique_ptr<network::SimpleURLLoader> verification_loader_;

  base::RepeatingTimer revalidation_timer_;
  base::ObserverList<Observer> observers_;
  base::WeakPtrFactory<FingerprintProxyService> weak_factory_{this};
};

}  // namespace fingerprint_browser

#endif  // BRAVE_BROWSER_FINGERPRINT_BROWSER_FINGERPRINT_PROXY_SERVICE_H_
