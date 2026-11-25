#include "seekserve/upload_tap.hpp"

namespace seekserve {

std::optional<UploadGear> upload_gear_from(int value) {
    switch (value) {
        case static_cast<int>(UploadGear::normal): return UploadGear::normal;
        case static_cast<int>(UploadGear::reduced): return UploadGear::reduced;
        case static_cast<int>(UploadGear::off): return UploadGear::off;
        default: return std::nullopt;
    }
}

lt::settings_pack upload_gear_settings(UploadGear gear, int reduced_slots) {
    const auto defaults = lt::default_settings();
    int slots = defaults.get_int(lt::settings_pack::unchoke_slots_limit);
    int allowed_fast = defaults.get_int(lt::settings_pack::allowed_fast_set_size);
    switch (gear) {
        case UploadGear::normal: break;
        case UploadGear::reduced: slots = reduced_slots; break;
        case UploadGear::off:
            slots = 0;
            allowed_fast = 0;
            break;
    }
    lt::settings_pack pack;
    pack.set_int(lt::settings_pack::unchoke_slots_limit, slots);
    pack.set_int(lt::settings_pack::allowed_fast_set_size, allowed_fast);
    return pack;
}

int tap_rate(bool finished, bool share_when_finished) {
    return finished && !share_when_finished ? kClosedTapRate : kOpenTapRate;
}

}  // namespace seekserve
