#include <gtest/gtest.h>

#include <libtorrent/settings_pack.hpp>

#include "seekserve/upload_tap.hpp"

using namespace seekserve;

// What the engine shares, a finished torrent and while it downloads (app
// DEC-995, DEC-996).
namespace {

int setting(const lt::settings_pack& pack, int name) {
    EXPECT_TRUE(pack.has_val(name)) << "setting " << name << " not in the pack";
    return pack.get_int(name);
}

}  // namespace

TEST(UploadTapTest, AFinishedTorrentHasItsTapClosedOnlyWhenSharingItIsOff) {
    EXPECT_EQ(tap_rate(/*finished=*/true, /*share_when_finished=*/false), kClosedTapRate);
    EXPECT_EQ(tap_rate(true, true), kOpenTapRate);
    EXPECT_EQ(tap_rate(false, false), kOpenTapRate);
    EXPECT_EQ(tap_rate(false, true), kOpenTapRate);
    // libtorrent reads 0 as no limit: the closed tap is never 0.
    EXPECT_GT(kClosedTapRate, 0);
}

TEST(UploadTapTest, TheNormalGearIsLibtorrentsOwn) {
    const auto defaults = lt::default_settings();
    const auto pack = upload_gear_settings(UploadGear::normal, 2);

    EXPECT_EQ(setting(pack, lt::settings_pack::unchoke_slots_limit),
              defaults.get_int(lt::settings_pack::unchoke_slots_limit));
    EXPECT_EQ(setting(pack, lt::settings_pack::allowed_fast_set_size),
              defaults.get_int(lt::settings_pack::allowed_fast_set_size));
}

TEST(UploadTapTest, TheReducedGearServesFewerPeers) {
    const auto defaults = lt::default_settings();
    const auto pack = upload_gear_settings(UploadGear::reduced, 2);

    EXPECT_EQ(setting(pack, lt::settings_pack::unchoke_slots_limit), 2);
    EXPECT_EQ(setting(pack, lt::settings_pack::allowed_fast_set_size),
              defaults.get_int(lt::settings_pack::allowed_fast_set_size));
}

TEST(UploadTapTest, TheOffGearServesNoneNotEvenAllowedFast) {
    const auto pack = upload_gear_settings(UploadGear::off, 2);

    EXPECT_EQ(setting(pack, lt::settings_pack::unchoke_slots_limit), 0);
    EXPECT_EQ(setting(pack, lt::settings_pack::allowed_fast_set_size), 0);
}

TEST(UploadTapTest, AGearPackTouchesNothingElse) {
    const auto pack = upload_gear_settings(UploadGear::off, 2);

    EXPECT_FALSE(pack.has_val(lt::settings_pack::proxy_type));
    EXPECT_FALSE(pack.has_val(lt::settings_pack::proxy_hostname));
    EXPECT_FALSE(pack.has_val(lt::settings_pack::upload_rate_limit));
}

TEST(UploadTapTest, OnlyTheThreeGearsHaveANumber) {
    EXPECT_EQ(upload_gear_from(0), UploadGear::normal);
    EXPECT_EQ(upload_gear_from(1), UploadGear::reduced);
    EXPECT_EQ(upload_gear_from(2), UploadGear::off);
    EXPECT_EQ(upload_gear_from(3), std::nullopt);
    EXPECT_EQ(upload_gear_from(-1), std::nullopt);
}
