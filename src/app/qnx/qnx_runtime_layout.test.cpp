#include "qnx_runtime_layout.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;

template <class F> static void rejects(F f) {
    bool rejected = false;

    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
}

int main() try {
    constexpr std::string_view passion_profile = "passion-11-russian-20260403-2.1.4.80";
    const auto passion = qnx_runtime_layout("sa8155-passion-21480");

    assert(passion.memory_mib == 11713 && passion.images.size() == 14 &&
           passion.loaders.size() == 16 && passion.projection.is_null());
    const auto profile_path = std::filesystem::path(__FILE__)
                                      .parent_path()
                                      .parent_path()
                                      .parent_path()
                                      .parent_path() /
                              "profiles" / passion_profile / "profile-qnx.json";
    std::ifstream profile_input(profile_path);
    const auto profile_definition = Json::parse(profile_input);
    assert(profile_definition.at("status") == "incomplete" &&
           profile_definition.at("hardware_adapter") == passion.adapter &&
           profile_definition.at("originals").size() == passion.originals.size());

    for (const auto &original : passion.originals) {
        const auto &item = profile_definition.at("originals").at(original);
        assert(item.at("file") == original && item.at("sha256").get<std::string>().size() == 64);
    }

    Json passion_metadata = {{"guest_arch", "aarch64"}, {"images", Json::object()}};
    qnx_runtime_metadata(passion_metadata, passion);
    assert(passion_metadata.at("hardware_adapter") == passion.adapter);

    for (const auto &image : passion.images) {
        passion_metadata["images"][image] = {
                {"file", "qnx/" + image}, {"bytes", 1}, {"sha256", std::string(64, 'a')}};
    }

    assert(qnx_portable_layout(passion_profile, passion_metadata).loaders == passion.loaders);
    rejects([&] {
        qnx_portable_layout("free-09-chinese-20260106-6.0.3", passion_metadata);
    });
    rejects([&] {
        qnx_runtime_layout(passion_profile, Json::object());
    });

    for (const auto &image : passion.images) {
        auto bad = passion_metadata;
        bad["images"].erase(image);
        rejects([&] {
            qnx_portable_layout(passion_profile, bad);
        });
    }

    auto extra = passion_metadata;
    extra["mcu_projection"] = Json::object();
    rejects([&] {
        qnx_portable_layout(passion_profile, extra);
    });
    extra = passion_metadata;
    extra["memory_mib"] = 8192;
    rejects([&] {
        qnx_portable_layout(passion_profile, extra);
    });
    assert(passion.loaders[13].second == "0x27c000000" &&
           passion.loaders[14].second == "0x2bc000000" &&
           passion.loaders[15].second == "0x2fc000000");
    const auto a9 = qnx_runtime_layout("legacy-fixture", Json::object());
    assert(a9.memory_mib == 8192 && a9.originals.size() == 4 && a9.images.size() == 8 &&
           a9.loaders.size() == 7 && a9.projection.is_null());
    assert((a9.loaders == std::vector<std::pair<std::string, std::string>>{
                                  {"smem.bin", "0x86000000"},
                                  {"host-native-lzo.bin", "0x90000000"},
                                  {"ifs2-native.img", "0xa0000000"},
                                  {"ifs2-native.img", "0xb0000000"},
                                  {"qnx-ifs2-restore.bin", "0x8a000000"},
                                  {"synthetic-gpt.bin", "0x8c000000"},
                                  {"sysla-gpt-ramdisk-v3.img", "0xC0000000"}}));
    Json metadata = {{"guest_arch", "aarch64"}, {"images", Json::object()}}, old = metadata;
    qnx_runtime_metadata(metadata, a9);
    assert(metadata == old);
    auto network = old;
    network["interguest_network"] = qnx_interguest_contract();
    const auto connected = qnx_runtime_layout("free-09-chinese-20260106-6.0.3", network);
    assert(connected.interguest_network == qnx_interguest_contract());
    Json forwarded;
    qnx_runtime_metadata(forwarded, connected);
    assert(forwarded.at("interguest_network") == network.at("interguest_network"));

    for (const auto &image : connected.images) {
        network["images"][image] = {
                {"file", "qnx/" + image}, {"bytes", 1}, {"sha256", std::string(64, 'a')}};
    }

    assert(qnx_portable_layout("free-09-chinese-20260106-6.0.3", network).interguest_network ==
           qnx_interguest_contract());

    for (const auto *key :
         {"schema", "transport", "address", "peer_address", "prefix", "qconn_port"}) {
        auto bad = network;
        bad["interguest_network"].erase(key);
        rejects([&] {
            qnx_portable_layout("free-09-chinese-20260106-6.0.3", bad);
        });
    }

    auto redirected = network;
    redirected["interguest_network"]["address"] = "10.0.2.2";
    rejects([&] {
        qnx_portable_layout("free-09-chinese-20260106-6.0.3", redirected);
    });
    auto persisted = network;
    persisted["persistence"] = {{"schema", 1},
                                {"system_image", "sysla-gpt-ramdisk-v3.img"},
                                {"userdata_image", "userdata-qnx6.img"}};
    persisted["images"]["userdata-qnx6.img"] = {{"file", "qnx/userdata-qnx6.img"},
                                                {"bytes", 33554432},
                                                {"sha256", std::string(64, 'a')}};
    const auto persistent_layout = qnx_portable_layout("free-09-chinese-20260106-6.0.3", persisted);
    assert(persistent_layout.images.size() == 9 && !persistent_layout.persistence.is_null());
    auto missing_userdata = persisted;
    missing_userdata["images"].erase("userdata-qnx6.img");
    rejects([&] {
        qnx_portable_layout("free-09-chinese-20260106-6.0.3", missing_userdata);
    });
    auto renamed_userdata = persisted;
    renamed_userdata["persistence"]["userdata_image"] = "other.img";
    rejects([&] {
        qnx_portable_layout("free-09-chinese-20260106-6.0.3", renamed_userdata);
    });
    Json persistent_metadata;
    qnx_runtime_metadata(persistent_metadata, persistent_layout);
    assert(persistent_metadata.at("persistence") == persisted.at("persistence"));
    auto fractional_network = network;
    fractional_network["interguest_network"]["prefix"] = 24.0;
    rejects([&] {
        qnx_portable_layout("free-09-chinese-20260106-6.0.3", fractional_network);
    });
    Json portable611 = {{"guest_arch", "aarch64"}, {"images", Json::object()}};
    const auto layout611 = qnx_runtime_layout("sa8155-qinggan11-611");
    qnx_runtime_metadata(portable611, layout611);
    portable611["interguest_network"] = qnx_interguest_contract();
    portable611["simulated_initial_native_state"] = qnx_qinggan11_simulated_initial_state(
            Json{{"eps_calibrationFlag", 1}}, "sa8155-qinggan11-611");

    for (const auto &image : layout611.images) {
        portable611["images"][image] = {
                {"file", "qnx/" + image}, {"bytes", 1}, {"sha256", std::string(64, 'a')}};
    }

    const auto accepted611 = qnx_portable_layout(qnx_qinggan11_611_profile, portable611);
    assert(accepted611.images.size() == 10 && accepted611.projection.at("projected_fields") == 56 &&
           accepted611.projection.at("unsupported_fields") == 523);
    auto older611 = portable611;
    older611["mcu_projection"] =
            qnx_qinggan11_611_mcu_projection(qnx_qinggan11_611_previous_projected_fields);
    assert(qnx_portable_layout(qnx_qinggan11_611_profile, older611)
                   .projection.at("projected_fields") == 34);
    auto fractional611 = older611;
    fractional611["mcu_projection"]["projected_fields"] = 34.0;
    rejects([&] {
        qnx_portable_layout(qnx_qinggan11_611_profile, fractional611);
    });
    auto previous611 = portable611;
    previous611["mcu_projection"]["projected_fields"] = 33;
    previous611["mcu_projection"]["unsupported_fields"] = 546;
    rejects([&] {
        qnx_portable_layout(qnx_qinggan11_611_profile, previous611);
    });
    assert(qnx_portable_upgrade_source_layout(qnx_qinggan11_611_profile, previous611)
                   .projection.at("projected_fields") == 33);
    rejects([&] {
        qnx_portable_upgrade_source_layout(qnx_qinggan11_731_profile, previous611);
    });
    auto bad_previous = previous611;
    bad_previous["mcu_projection"]["projected_fields"] = 32;
    rejects([&] {
        qnx_portable_upgrade_source_layout(qnx_qinggan11_611_profile, bad_previous);
    });
    bad_previous = previous611;
    bad_previous["mcu_projection"]["projected_fields"] = 19;
    bad_previous["mcu_projection"]["unsupported_fields"] = 560;
    rejects([&] {
        qnx_portable_upgrade_source_layout(qnx_qinggan11_611_profile, bad_previous);
    });
    bad_previous = previous611;
    bad_previous["hardware_adapter"] = "sa8155-qinggan11-731";
    rejects([&] {
        qnx_portable_upgrade_source_layout(qnx_qinggan11_611_profile, bad_previous);
    });
    bad_previous = previous611;
    bad_previous["mcu_projection"]["unsupported_field_policy"] = "ignore";
    rejects([&] {
        qnx_portable_upgrade_source_layout(qnx_qinggan11_611_profile, bad_previous);
    });
    rejects([&] {
        qnx_portable_layout(qnx_qinggan11_731_profile, portable611);
    });
    auto wrong611 = portable611;
    wrong611["mcu_projection"] = qnx_qinggan11_mcu_projection();
    rejects([&] {
        qnx_portable_layout(qnx_qinggan11_611_profile, wrong611);
    });
    wrong611 = portable611;
    wrong611["simulated_initial_native_state"]["hardware_adapter"] = "sa8155-qinggan11-731";
    rejects([&] {
        qnx_portable_layout(qnx_qinggan11_611_profile, wrong611);
    });
    wrong611 = portable611;
    wrong611["mcu_projection"]["settings_feedback_supported"] = true;
    rejects([&] {
        qnx_portable_layout(qnx_qinggan11_611_profile, wrong611);
    });
    auto free = qnx_runtime_layout("sa8155-qinggan11-731");
    qnx_runtime_metadata(metadata, free);
    const auto actual = qnx_runtime_layout(qnx_qinggan11_731_profile, metadata);
    auto free_network = metadata;
    free_network["interguest_network"] = qnx_interguest_contract();
    const auto connected_free = qnx_runtime_layout(qnx_qinggan11_731_profile, free_network);
    assert(connected_free.interguest_network == qnx_interguest_contract());
    Json free_forwarded;
    qnx_runtime_metadata(free_forwarded, connected_free);
    assert(free_forwarded.at("interguest_network") == qnx_interguest_contract());

    for (const auto &image : connected_free.images) {
        free_network["images"][image] = {
                {"file", "qnx/" + image}, {"bytes", 1}, {"sha256", std::string(64, 'a')}};
    }

    assert(qnx_portable_layout(qnx_qinggan11_731_profile, free_network).interguest_network ==
           qnx_interguest_contract());
    auto redirected_free = free_network;
    redirected_free["interguest_network"]["address"] = "10.0.2.2";
    rejects([&] {
        qnx_portable_layout(qnx_qinggan11_731_profile, redirected_free);
    });
    assert(actual.originals ==
           std::vector<std::string>({"hyp_la.img", "ifs2_la.img", "system_la.img"}));
    assert(actual.images.size() == 10 && qnx_image_roles(actual).size() == 10 &&
           actual.loaders.size() == 9);
    assert(actual.loaders.at(7) ==
           std::make_pair(std::string("gpt-primary.bin"), std::string("0xaffc0000")));
    assert(actual.loaders.at(8) ==
           std::make_pair(std::string("gpt-backup.bin"), std::string("0xbffbe000")));
    assert(actual.loaders.at(2).first == actual.loaders.at(3).first &&
           actual.loaders.at(2).second != actual.loaders.at(3).second);
    rejects([&] {
        qnx_runtime_layout(qnx_qinggan11_731_profile, old);
    });
    rejects([&] {
        qnx_runtime_layout("other-profile", metadata);
    });
    rejects([] {
        qnx_runtime_layout("unknown");
    });

    for (const auto *key :
         {"packet_bytes", "wire_fields", "projected_fields", "unsupported_fields"}) {
        auto bad = metadata;
        bad["mcu_projection"][key] = 0;
        rejects([&] {
            qnx_runtime_layout(qnx_qinggan11_731_profile, bad);
        });
    }

    auto fractional = metadata;
    fractional["mcu_projection"]["projected_fields"] = 462.0;
    rejects([&] {
        qnx_runtime_layout(qnx_qinggan11_731_profile, fractional);
    });
    const auto legacy_projection = metadata.at("mcu_projection");

    for (unsigned value = 0; value < 4; ++value) {
        auto native = metadata;
        native["simulated_initial_native_state"] =
                qnx_qinggan11_simulated_initial_state(Json{{"eps_calibrationFlag", value}});
        const auto layout = qnx_runtime_layout(qnx_qinggan11_731_profile, native);
        Json launch;
        qnx_runtime_metadata(launch, layout);
        assert(launch.at("simulated_initial_native_state") ==
                       native.at("simulated_initial_native_state") &&
               launch.at("mcu_projection") == legacy_projection);
        auto portable = native;

        for (const auto &image : layout.images) {
            portable["images"][image] = {
                    {"file", "qnx/" + image}, {"bytes", 1}, {"sha256", std::string(64, 'a')}};
        }

        assert(qnx_portable_layout(qnx_qinggan11_731_profile, portable)
                       .simulated_initial_native_state == layout.simulated_initial_native_state);
        auto prior = portable;
        prior.erase("simulated_initial_native_state");
        assert(prior.size() == 4 && qnx_portable_layout(qnx_qinggan11_731_profile, prior)
                                            .simulated_initial_native_state.is_null());
        auto extra = portable;
        extra["unknown"] = 1;
        rejects([&] {
            qnx_portable_layout(qnx_qinggan11_731_profile, extra);
        });
        auto wrong = native;
        wrong["simulated_initial_native_state"]["fields"]["eps_calibrationFlag"]["width"] = 1;
        rejects([&] {
            qnx_runtime_layout(qnx_qinggan11_731_profile, wrong);
        });
        wrong = native;
        wrong["simulated_initial_native_state"]["fields"]["eps_calibrationFlag"]["value"] = 4;
        rejects([&] {
            qnx_runtime_layout(qnx_qinggan11_731_profile, wrong);
        });
        wrong = old;
        wrong["simulated_initial_native_state"] = native.at("simulated_initial_native_state");
        rejects([&] {
            qnx_runtime_layout("legacy-fixture", wrong);
        });
    }

    assert(qnx_runtime_layout(qnx_qinggan11_731_profile, metadata)
                   .simulated_initial_native_state.is_null());
    std::cout
            << "A9 unchanged and exact Free A11 7.3.1 three-original/ten-image/two-GPT layout passed; no VM\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
