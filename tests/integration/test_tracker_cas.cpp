#include <gtest/gtest.h>

#include <cstdio>
#include <memory>

#include <libtorrent/aux_/session_impl.hpp>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "seekserve/session_manager.hpp"

namespace seekserve {
namespace {

// Self-signed test CA (fixtures/test_tracker_ca.pem), trusted by nothing else.
static const std::string kTestCa =
    std::string(SEEKSERVE_FIXTURE_DIR) + "/test_tracker_ca.pem";

using X509Ptr = std::unique_ptr<X509, decltype(&X509_free)>;

X509Ptr read_test_ca() {
    FILE* f = std::fopen(kTestCa.c_str(), "r");
    X509* cert = f ? PEM_read_X509(f, nullptr, nullptr, nullptr) : nullptr;
    if (f) std::fclose(f);
    return X509Ptr(cert, &X509_free);
}

// Whether the SSL context libtorrent uses for HTTPS trackers and web seeds
// trusts cert.
bool tracker_context_trusts(TorrentSessionManager& manager, X509* cert) {
    auto impl = manager.session().native_handle();
    lt::aux::session_interface* ses = impl.get();
    auto* ctx = ses->ssl_ctx();
    X509_STORE* store = SSL_CTX_get_cert_store(ctx->native_handle());
    STACK_OF(X509_OBJECT)* objects = X509_STORE_get1_objects(store);
    bool found = false;
    for (int i = 0; objects && i < sk_X509_OBJECT_num(objects); ++i) {
        X509* c = X509_OBJECT_get0_X509(sk_X509_OBJECT_value(objects, i));
        if (c && X509_cmp(c, cert) == 0) found = true;
    }
    sk_X509_OBJECT_pop_free(objects, X509_OBJECT_free);
    return found;
}

// The proxy on port 0 keeps the session off the network: no DHT, and every
// tracker or peer connection fails at once.
SessionConfig config_with_cas(const std::string& ca_file) {
    SessionConfig config;
    config.save_path = "/tmp/seekserve_test_cas";
    config.listen_port_start = 16891;
    config.proxy.enabled = true;
    config.proxy.port = 0;
    config.ca_cert_file = ca_file;
    return config;
}

TEST(TrackerCas, CaFileIsTrustedForTrackers) {
    auto ca = read_test_ca();
    ASSERT_NE(ca, nullptr);
    TorrentSessionManager manager(config_with_cas(kTestCa));
    EXPECT_TRUE(tracker_context_trusts(manager, ca.get()));
}

TEST(TrackerCas, WithoutCaFileOnlyTheDefaults) {
    auto ca = read_test_ca();
    ASSERT_NE(ca, nullptr);
    TorrentSessionManager manager(config_with_cas(""));
    EXPECT_FALSE(tracker_context_trusts(manager, ca.get()));
}

TEST(TrackerCas, MissingCaFileKeepsOnlyTheDefaults) {
    auto ca = read_test_ca();
    ASSERT_NE(ca, nullptr);
    TorrentSessionManager manager(config_with_cas("/nonexistent/ca.pem"));
    EXPECT_FALSE(tracker_context_trusts(manager, ca.get()));
    EXPECT_TRUE(manager.list_torrents().empty());
}

}  // namespace
}  // namespace seekserve
