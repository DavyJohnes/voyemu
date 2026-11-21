#include "adas_ecu.hpp"
#include "model.hpp"
#include "native_mcu_qinggan11.h"
#include "qnx_qinggan11_mcu_layout.hpp"
#include "qnx_runtime_layout.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
using Json = voyemu::Json;
using u32 = unsigned;
using u64 = unsigned long long;
using usize = unsigned long;
static Qinggan11McuField qinggan11_fields[579];
static unsigned qinggan11_calibration_fixture = 1, qinggan11_calibration_fixture_enabled = 1;
static unsigned qinggan11_projected_fields = 0, qinggan11_settings_feedback_enabled = 0;
#define QGFX_QINGGAN11_LAYOUT_DEFINED 1
#include "mcu_catalog.inc"
static u32 normal[579];
static unsigned char original_bytes[650];
struct TestPacket {
    u32 fault, out_len;
    unsigned char data[3840];
};
static TestPacket storage{}, *packet = &storage;
static bool enabled = true, bad = false;
static u64 offered = 2;
static unsigned original_calls = 0, rpc_calls = 0;

static void acquire() {}

static void release() {}

static void copy(void *d, const void *s, usize n) {
    std::memcpy(d, s, n);
}

static void emit(const char *) {}

static char *test_getenv(const char *name) {
    return enabled && !std::strcmp(name, "QGFX_NATIVE_MCU") ? (char *)"1" : nullptr;
}

static int test_usleep(unsigned) {
    return 0;
}

static int test_pthread_create(int *, const void *, void *(*)(void *), void *) {
    return 11;
}

static int test_pthread_join(int, void **) {
    return 3;
}

static int genuine(void *out, unsigned short size) {
    original_calls++;

    if (out && size >= 650) {
        std::memcpy(out, original_bytes, 650);
    }

    return 650;
}

static int serializer(void *, void *, unsigned) {
    return -7;
}

static void *test_dlopen(const char *, int) {
    return (void *)1;
}

static void *test_dlsym(void *, const char *n) {
    return !std::strcmp(n, "dataParser_makedumydata") ? (void *)genuine : (void *)serializer;
}

static u64 call(u32 op, const u64 *args, unsigned count, const void *, unsigned) {
    rpc_calls++;
    storage = {};

    if (op == 363) {
        assert(count == 1);

        return 0;
    }

    assert(op == 364 && count == 4 && args[1] == 650 && args[2] == 736 &&
           args[3] == qinggan11_projected_fields);
    u32 values[581] = {1, 579};
    std::memcpy(values + 2, normal, sizeof normal);

    if (bad) {
        values[2 + 6] ^= 1;
    }

    storage.out_len = sizeof values;
    std::memcpy(storage.data, values, sizeof values);

    return offered;
}

#define getenv test_getenv
#define dlopen test_dlopen
#define dlsym test_dlsym
#define usleep test_usleep
#define pthread_create test_pthread_create
#define pthread_join test_pthread_join
#define QGFX_INITIALIZER
#define QGFX_NATIVE_MCU_DESTRUCTOR
#include "native_mcu_qinggan11.c"
#undef getenv
#undef dlopen
#undef dlsym
#undef usleep
#undef pthread_create
#undef pthread_join

int main(int argc, char **argv) {
    try {
        assert(argc == 2);
        qinggan11_projected_fields =
                voyemu::qnx_adapter(voyemu::QnxGuestAdapter::sa8155_qinggan11_611)
                        .mcu_projected_fields;
        const auto root = std::filesystem::canonical(argv[1]);
        const auto source = voyemu::read_private_json(
                root / "src/guest/qnx/native_mcu_qinggan11_611.json", 256 * 1024);
        const auto generated = voyemu::qnx_qinggan11_mcu_layout(source,
                                                                Json{{"eps_calibrationFlag", 1}});
        assert(generated.find(
                       "qinggan11_projected_fields=" + std::to_string(qinggan11_projected_fields) +
                       "U") != std::string::npos &&
               generated.find("qinggan11_settings_feedback_enabled=0U") != std::string::npos);
        const auto &fields = voyemu::catalog();
        const auto initial = voyemu::initial_state().at("raw_oem");
        unsigned mapped = 0;

        for (unsigned i = 0; i < 579; i++) {
            normal[i] = initial.at(fields[i].name).get<unsigned>();
            qinggan11_fields[i] = {65535, 0, normal[i]};
            const auto &row = source.at("fields").at(i);

            if (row.contains("wire")) {
                qinggan11_fields[i].bit = row.at("wire").at("start_bit");
                qinggan11_fields[i].width = row.at("wire").at("width");

                if (row.at("wire").contains("unknown")) {
                    qinggan11_fields[i].unknown_input = row.at("wire").at("unknown").at("input");
                    qinggan11_fields[i].unknown_wire = row.at("wire").at("unknown").at("wire");
                }

                mapped++;
            }
        }

        assert(mapped == qinggan11_projected_fields && qinggan11_fields[37].bit == 69 &&
               qinggan11_fields[287].bit == 1111 && qinggan11_fields[288].bit == 1112 &&
               qinggan11_fields[290].bit == 1144 && qinggan11_fields[154].bit == 335 &&
               qinggan11_fields[154].width == 1);

        for (unsigned i = 0; i < 650; i++) {
            original_bytes[i] = static_cast<unsigned char>(i * 73 + 19);
        }

        qinggan11_settings.have = 1;
        std::memset(qinggan11_settings.bytes, 0xab, 10);
        unsigned char out[650];
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered);

        for (unsigned bit = 0; bit < 5200; bit++) {
            bool supported = bit == 465 || bit == 466;

            for (const auto &field : qinggan11_fields) {
                if (field.width && bit >= field.bit && bit < field.bit + field.width) {
                    supported = true;
                }
            }

            if (!supported) {
                assert(((out[bit / 8] ^ original_bytes[bit / 8]) & (1U << (bit % 8))) == 0);
            }
        }

        assert(((out[58] >> 1) & 3) == 1 && !std::memcmp(out + 250, original_bytes + 250, 10));
        assert(((out[335 / 8] >> (335 % 8)) & 1) == 1);
        offered++;
        normal[154] = 0;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               ((out[335 / 8] >> (335 % 8)) & 1) == 0);
        offered++;
        normal[154] = 1;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               ((out[335 / 8] >> (335 % 8)) & 1) == 1);
        // The model ignition reaches bit 0, the OEM power item of powermgr, in both states.
        assert(qinggan11_fields[0].bit == 0 && qinggan11_fields[0].width == 1 && (out[0] & 1) == 1);
        offered++;
        normal[0] = 0;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               (out[0] & 1) == 0);
        offered++;
        normal[0] = 1;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
               (out[0] & 1) == 1);
        // Verify the packed bits independently of the table used by the projector.
        auto packed = [&](unsigned bit, unsigned width) {
            unsigned value = 0;

            for (unsigned i = 0; i < width; ++i) {
                value |= unsigned((out[(bit + i) / 8] >> ((bit + i) % 8)) & 1U) << i;
            }

            return value;
        };
        struct Case {
            std::string_view name;
            unsigned bit, width, value;
        };
        // Each written field reaches its OEM wire bits and leaves on the next revision.
        auto deliver_cases = [&](std::initializer_list<Case> cases) {
            for (const auto &signal : cases) {
                const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto &f) {
                    return f.name == signal.name;
                });
                assert(field != fields.end());
                const auto position = static_cast<unsigned>(field - fields.begin());
                const auto previous = normal[position];
                normal[position] = signal.value;
                ++offered;
                assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
                       packed(signal.bit, signal.width) == signal.value);
                normal[position] = previous;
                ++offered;
                assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
                       packed(signal.bit, signal.width) == previous);
            }
        };
        deliver_cases({{"pos_lamp", 3, 1, 1},
                       {"low_beam", 4, 1, 1},
                       {"high_beam", 5, 1, 1},
                       {"rear_fog", 10, 1, 1},
                       {"bcm_DRLStatus", 13, 1, 1},
                       {"hood", 184, 1, 1},
                       {"door_fr", 186, 1, 1},
                       {"door_rl", 187, 1, 1},
                       {"door_rr", 188, 1, 1},
                       {"tail_gate", 189, 1, 1},
                       {"remain_mileage_e", 1090, 11, 280},
                       {"remain_mileage_f", 2218, 11, 420},
                       {"odo", 1120, 24, 12346},
                       {"fuel_percent", 1236, 7, 68},
                       {"actual_power", 1262, 7, 12},
                       {"allowed_power", 1255, 7, 80},
                       {"actual_power10x", 1407, 12, 120},
                       {"allowed_power10x", 1395, 12, 800},
                       {"vcu_remainingMileageReal", 1433, 11, 280},
                       {"vcu_remainingMileageCLTCWLTC", 1571, 11, 280},
                       {"remain_mileage_f_nedc", 1463, 10, 420},
                       {"remain_mileage_f_wltc", 2229, 11, 420}});
        // Start bits and widths from the OEM 6.11.1 parser descriptors, one list per group.
        deliver_cases({
                {"overspdWarning", 94, 2, 3},
                {"mrr_fcwSts", 113, 3, 3},
                {"esc_hbbSts", 119, 3, 3},
                {"mrr_r1_rearCollisionWarning", 166, 1, 1},
                {"mrr_r1_systemErrorSts", 178, 2, 3},
                {"icm_dmsWearyLeverSts", 202, 3, 3},
                {"gsm_faultLevel", 207, 2, 3},
                {"las_status", 452, 3, 3},
                {"mpc_laneAssitType", 459, 2, 3},
                {"mpc_text_infor", 678, 5, 11},
                {"mrr_text_info", 696, 5, 11},
                {"pebs_warning", 701, 3, 3},
                {"icm_dmsCameraSts", 729, 3, 3},
                {"icm_dmsSts", 732, 3, 3},
                {"icm_dmsDistractLeverSts", 735, 2, 3},
                {"icm_dmsDrvBehaviorMon", 737, 3, 3},
                {"nvs_displayReq", 1215, 3, 3},
                {"nvs_Sts", 1218, 2, 3},
                {"nvs_cameraSts", 1220, 2, 3},
                {"nvs_warnningSts", 1222, 1, 1},
                {"nvs_textPrompt", 1223, 4, 11},
                {"IHBC_status", 6, 3, 3},
                {"nvs_warningXCoordinate", 1320, 12, 2731},
                {"nvs_warningYCoordinate", 1332, 10, 683},
                {"nvs_warningType", 1342, 3, 3},
                {"gsm_warnInformation", 1473, 3, 3},
                {"mrr_r1_leftBSDLCAWarning", 1549, 2, 3},
                {"mrr_r1_rightBSDLCAWarning", 1551, 2, 3},
                {"mrr_r1_leftDOWWarnning", 1553, 2, 3},
                {"mrr_r1_rightDOWWarnning", 1555, 2, 3},
                {"mrr_r1_leftRCTAWarnning", 1557, 1, 1},
                {"mrr_r1_rightRCTAWarnning", 1558, 1, 1},
        });

        deliver_cases({
                {"asc_ASC_errorSts", 122, 3, 3},
                {"sound_enable", 134, 1, 1},
                {"hcm_leftHighBeamFaultSt", 135, 2, 3},
                {"hcm_rightHighBeamFaultSt", 137, 1, 1},
                {"hcm_leftLowBeamFaultSt", 138, 1, 1},
                {"hcm_rightLowBeamFaultSt", 139, 1, 1},
                {"hcm_turnLampWarningLt", 140, 1, 1},
                {"hcm_turnLampWarningRt", 141, 1, 1},
                {"bcm_rllmLeftTurnLampFault", 142, 1, 1},
                {"bcm_rrlmRightTurnLampFault", 143, 1, 1},
                {"bcm_rmlmleftTurnLampFault", 144, 1, 1},
                {"bcm_rmlmrightTurnLampFault", 145, 1, 1},
                {"hcm_middleLeftPositionLampFault", 146, 1, 1},
                {"hcm_positionLampWarningLt", 147, 1, 1},
                {"hcm_middleRightPositionLampFault", 148, 1, 1},
                {"hcm_positionLampWarningRt", 149, 1, 1},
                {"bcm_rllmPositionLampFault", 150, 1, 1},
                {"bcm_rmlmpositionLampFault", 151, 1, 1},
                {"bcm_rrlmpositionLampFault", 152, 1, 1},
                {"hcm_dayTimeRunningLampWarningLt", 153, 1, 1},
                {"hcm_dayTimeRunningLampWarningRt", 154, 1, 1},
                {"hcm_alsErrorStsLt", 155, 1, 1},
                {"hcm_alsErrorStsRt", 156, 1, 1},
                {"bcm_rllmstopLampFault", 157, 2, 3},
                {"bcm_rmlmstopLampFault", 159, 1, 1},
                {"bcm_rmlmhighStopLampFault", 160, 1, 1},
                {"bcm_rrlmstopLampFault", 161, 1, 1},
                {"bcm_rllmfogLampFault", 162, 1, 1},
                {"bcm_rrlmfogLampFault", 163, 1, 1},
                {"bcm_rllmreverseLampFault", 164, 1, 1},
                {"bcm_rrlmreverseLampFault", 165, 1, 1},
                {"washing_liquid_low", 180, 1, 1},
                {"fuel_cap", 190, 1, 1},
                {"etc_disassemblySts", 195, 1, 1},
                {"etc_lockSts", 196, 1, 1},
                {"etc_detect", 197, 1, 1},
                {"etc_lamp", 198, 1, 1},
                {"etc_tranStauts", 199, 2, 3},
                {"maintenance_sts_km", 209, 1, 1},
                {"maintenance_sts_day", 210, 1, 1},
                {"remain_maintenance_km", 211, 9, 171},
                {"remain_maintenance_days", 220, 4, 11},
                {"Maintain_Reminder", 224, 1, 1},
                {"maintenance", 225, 2, 3},
                {"pos_lamp_tips", 308, 1, 1},
                {"bcm_sl_errSts", 309, 4, 11},
                {"peps_key_not_incar", 313, 1, 1},
                {"peps_key_serch_fail", 314, 1, 1},
                {"immo_release", 315, 1, 1},
                {"peps_key_close", 316, 1, 1},
                {"peps_low_bat", 317, 1, 1},
                {"peps_relay_err", 318, 1, 1},
                {"bcm_12VBatterySOC", 327, 8, 171},
                {"vsp_errorIdentification", 337, 1, 1},
                {"peps_key_incar", 338, 1, 1},
                {"wcm_reminder", 718, 1, 1},
                {"bcm_readyReq", 723, 1, 1},
                {"Tbox_SysErSt", 724, 2, 3},
                {"ASC_SusTempAdjust", 728, 1, 1},
                {"bcm_IndicationPressBrake", 740, 2, 3},
                {"bcm_LowFrequencyAntennaFault", 742, 2, 3},
                {"driving_long_tips", 747, 1, 1},
                {"bcm_remoteForbidden", 748, 2, 3},
                {"ivi_naviDirection", 1155, 4, 11},
                {"ivi_naviAltitude", 1159, 7, 43},
                {"ac_driverTempC_off", 1176, 1, 1},
                {"ac_driverTempC", 1177, 5, 11},
                {"ivi_timeInfoSecond", 1182, 6, 43},
                {"ivi_timeInfoMinute", 1188, 6, 43},
                {"ivi_timeInfoHour", 1194, 5, 11},
                {"ivi_timeInfoDay", 1199, 5, 11},
                {"ivi_timeInfoMonth", 1204, 4, 11},
                {"ivi_timeInfoYear", 1208, 6, 43},
                {"ivi_timeValid", 1214, 1, 1},
                {"bcm_sl_currentPos", 1276, 2, 2},
                {"bcm_PressureWarningLF", 1349, 2, 3},
                {"bcm_PressureWarningRF", 1351, 2, 3},
                {"bcm_TireTempWarningLF", 1353, 1, 1},
                {"bcm_TireTempWarningRF", 1354, 1, 1},
                {"bcm_tpmsLFLOW_BATT_Warning", 1355, 1, 1},
                {"bcm_tpmsRFLOW_BATT_Warning", 1356, 1, 1},
                {"bcm_tpmsLFRAPID_LEAK_Warning", 1357, 1, 1},
                {"bcm_tpmsRFRAPID_LEAK_Warning", 1358, 1, 1},
                {"bcm_PressureWarningLR", 1363, 2, 3},
                {"bcm_PressureWarningRR", 1365, 2, 3},
                {"bcm_TireTempWarningLR", 1367, 1, 1},
                {"bcm_TireTempWarningRR", 1368, 1, 1},
                {"bcm_tpmsLRLOW_BATT_Warning", 1369, 1, 1},
                {"bcm_tpmsRRLOW_BATT_Warning", 1370, 1, 1},
                {"bcm_tpmsLRRAPID_LEAK_Warning", 1371, 1, 1},
                {"bcm_tpmsRRRAPID_LEAK_Warning", 1372, 1, 1},
                {"upgradeMCU", 1375, 8, 171},
                {"OTA_mode", 1453, 2, 3},
                {"hcm_adbFuctionStsLt", 1455, 3, 3},
                {"hcm_adbFuctionStsRt", 1458, 3, 3},
                {"asc_AirPumpOverHeatWarning", 1461, 2, 3},
                {"asc_ascSysSts", 1476, 3, 3},
                {"etc_dataSts", 1503, 8, 171},
                {"fuel_cap_err", 1513, 2, 3},
                {"alarm_play_req", 1515, 12, 2730},
                {"soc_menu_info", 1559, 8, 171},
                {"icyroad_reminder", 1567, 2, 3},
                {"reset_dsp", 1597, 1, 1},
                {"reboot_request", 1838, 2, 3},
        });

        deliver_cases({
                {"epb_fault", 11, 2, 3},
                {"brake_oil_low", 18, 1, 1},
                {"abs_fault", 19, 1, 1},
                {"ebd_fault", 20, 1, 1},
                {"epb_parking", 21, 2, 3},
                {"autohold_lamp", 23, 3, 3},
                {"assist_sys_fault", 26, 2, 3},
                {"esc_lamp", 28, 2, 3},
                {"esc_off", 30, 1, 1},
                {"eps_fault", 31, 2, 3},
                {"hdc_lamp", 33, 2, 3},
                {"esc_hhcActive", 167, 1, 1},
                {"break_warning", 183, 1, 1},
                {"epb_text", 319, 4, 11},
                {"esc_hhcAvailable", 323, 1, 0},
                {"avh_available", 324, 1, 1},
                {"avh_stdby", 325, 2, 3},
                {"esc_vehicleSpeed", 1307, 13, 2731},
                {"vcu_brakePedalPressed", 1444, 1, 1},
        });

        deliver_cases({
                {"acu_driverSeatBeltSts", 35, 2, 3},
                {"acu_passengerSeatBeltSts", 37, 2, 3},
                {"RearLeftSeatBelt", 39, 2, 3},
                {"RearMidSeatBelt", 41, 2, 3},
                {"RearRightSeatBelt", 43, 2, 3},
                {"acu_warning", 45, 2, 3},
                {"SeatBeltLamp", 1546, 2, 3},
        });

        deliver_cases({
                {"fuel_low_alert", 47, 2, 3},
                {"mil_lamp", 49, 2, 3},
                {"eng_coolant_temp_high", 51, 2, 3},
                {"eng_coolant_temp", 53, 8, 171},
                {"engine_oil_low", 61, 1, 1},
                {"cool_ant_low", 62, 2, 3},
                {"fuel_instant", 1039, 8, 171},
                {"fuel_instant_last1km", 1047, 8, 171},
                {"rpm", 1065, 13, 2731},
                {"vcu_stEgyFlowEngAndGenMot", 1086, 2, 3},
        });

        deliver_cases({
                {"bms_authenticationResult", 14, 2, 3},
                {"pwr_bat_soc_low", 68, 1, 1},
                {"mcu0_fault", 70, 1, 1},
                {"turtle_lamp", 71, 2, 3},
                {"vcu_alertLevel", 73, 3, 3},
                {"bms_alert", 76, 2, 3},
                {"isolationFault", 78, 2, 3},
                {"vcu_CCStas", 125, 2, 3},
                {"charge_lamp", 127, 1, 1},
                {"bms_state", 128, 4, 11},
                {"mcuf0_overTemperature", 132, 1, 1},
                {"vcu_tcmFail", 133, 1, 1},
                {"ev_lamp", 181, 2, 1},
                {"charge_cap", 191, 1, 1},
                {"volOrSOCLowAlarm", 192, 2, 3},
                {"vcu_batteryOverTemperature", 194, 1, 1},
                {"bms_f003_packOverCrntChg", 201, 1, 1},
                {"vcu_towMode", 336, 1, 1},
                {"dc_charge_state", 339, 4, 11},
                {"ac_charge_state", 343, 4, 11},
                {"ac_charge_connect", 347, 1, 1},
                {"dc_charge_connect", 348, 1, 1},
                {"remain_charge_time", 349, 10, 683},
                {"bms_pPack", 359, 14, 10923},
                {"obc_chargeVoltage", 373, 17, 43691},
                {"obc_chargeCurrent", 390, 13, 2731},
                {"ivi_maxChrgSocSet", 411, 8, 171},
                {"bms_chargeFormSts", 419, 3, 3},
                {"ivi_ChargeEndTimeHour", 422, 4, 11},
                {"ivi_ChargeStartTimeHour", 426, 4, 11},
                {"ivi_ChargeEndTimeHalfDay", 430, 1, 1},
                {"ivi_ChargeStartTimeHalfDay", 431, 1, 1},
                {"ivi_ChargeEndTimeMinute", 432, 6, 43},
                {"ivi_ChargeStartTimeMinute", 438, 6, 43},
                {"obc_state", 602, 4, 11},
                {"vcu_vDisSet", 706, 8, 171},
                {"vcu_ccState", 714, 4, 11},
                {"obc_ccOhm", 719, 4, 11},
                {"vcu_KL15offReq", 177, 1, 1},
                {"vcu_GearPosNotEqual", 750, 3, 3},
                {"bms_stEgyFlowSysDischRgn", 1078, 2, 3},
                {"bms_stEgyFlowOBCAndBatt", 1080, 2, 3},
                {"mcuf0_stEgyFlowAndBatt", 1082, 2, 3},
                {"mcur0_stEgyFlowAndBatt", 1084, 2, 3},
                {"vcu_stEgyFlowGenMotAndBatt", 1088, 2, 3},
                {"vcu_driveMode", 1152, 3, 3},
                {"soc_warning", 1253, 2, 3},
                {"recovery_power", 1269, 7, 43},
                {"BMS_ThrmlRunawaySt", 1373, 2, 3},
                {"recovery_power10x", 1419, 12, 2731},
                {"VCU_StandStillPreheatBattWarn", 1445, 1, 1},
                {"VCU_LowTempInhibitPowerUpWarn", 1446, 1, 1},
                {"lowBatChargeFault", 1447, 2, 3},
                {"vcu_ExhibitionSts", 1511, 2, 3},
                {"vcu_hvilWarningIndicate", 1569, 2, 3},
                {"bms_batteryConditionFail", 1595, 1, 1},
                {"bms_f054_dcChrgOverTemp", 1596, 1, 1},
                {"OBC_halfConnectWarn", 1653, 2, 3},
                {"VCU_SOCmodeSet", 745, 2, 3},
                {"charge_connect_warn", 1796, 2, 3},
        });

        deliver_cases({
                {"vcu_consumpRatio50km", 403, 8, 171},
                {"trip_ax", 759, 24, 11184811},
                {"tripax_avg_pwr", 783, 11, 683},
                {"tripax_avg_spd", 794, 8, 171},
                {"tripax_avg_fuelconsump", 802, 8, 171},
                {"tripax_time_min", 810, 13, 2731},
                {"trip_fuel", 823, 24, 11184811},
                {"tripfuel_avg_pwr", 847, 11, 683},
                {"tripfuel_avg_spd", 858, 8, 171},
                {"tripfuel_avg_fuelconsump", 866, 8, 171},
                {"tripfuel_time_min", 874, 13, 2731},
                {"trip_interval_ready", 887, 24, 11184811},
                {"current_trip_avg_pwr", 911, 11, 683},
                {"current_trip_avg_spd", 922, 8, 171},
                {"current_trip_avg_fuelconsump", 930, 8, 171},
                {"current_trip_time_min", 938, 13, 2731},
                {"trip_interval_charge", 951, 24, 11184811},
                {"last_charge_avg_pwr", 975, 11, 683},
                {"last_charge_avg_fuelconsump", 986, 8, 171},
                {"last_charge_avg_spd", 994, 8, 171},
                {"last_charge_time_min", 1002, 13, 2731},
                {"vcu_consumpRatioInstant", 1015, 11, 683},
                {"vcu_consumpRatioInstant_last1km", 1026, 11, 683},
                {"instant_value_unit", 1037, 2, 3},
                {"instant_value_trip", 1055, 10, 683},
                {"odoRev", 1479, 24, 11184847},
                {"trip_ax_rev", 1756, 20, 699051},
                {"trip_current_rev", 1776, 20, 699051},
        });

        // AdasEcu acc and cancel, the power-off request, the lane departure lamp and the language
        // write fields that this layout does not project. The guest rejects the logical revision as
        // a whole, and the host state for the guest carries their initial values and the new speed.
        {
            auto state = voyemu::apply_patch(voyemu::initial_state(),
                                             {{"speed", 80}, {"gear", 3}, {"gear_valid", 1}});
            voyemu::AdasEcu adas;
            const auto now = voyemu::AdasEcu::Clock::now();
            std::uint32_t sequence = 0;
            auto press = [&](const char *button) {
                const bool pressed =
                        adas.input(state, ++sequence, button, "down", "test", now).handled;
                assert(pressed);
                const auto outcome = adas.input(state, ++sequence, button, "up", "test", now);
                state = voyemu::apply_patch(state, outcome.patch);
            };
            press("acc");
            assert(state.at("raw_oem").at("acc_mode") == 3);
            press("cancel");
            state = voyemu::apply_patch(
                    state,
                    {{"speed", 60}, {"poweroff_req", 1}, {"ldws_lamp", 1}, {"ivi_languageSet", 1}});
            const auto &raw = state.at("raw_oem");
            assert(raw.at("acc_mode") == 0 && raw.at("mpc_icaSts") == 1 &&
                   raw.at("mrr_vSetDis") == 160 && raw.at("time_gap_set") == 1);
            const auto &speed = qinggan11_fields[310];
            auto deliver = [&](const Json &values) {
                for (unsigned i = 0; i < 579; i++) {
                    normal[i] = values.at(fields[i].name).get<unsigned>();
                }

                ++offered;

                return dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered &&
                       packed(speed.bit, speed.width) == 60;
            };
            const auto rejects = qinggan11_rejects;
            assert(!deliver(raw) && qinggan11_rejects == rejects + 1);
            const auto guest = voyemu::qnx_qinggan11_guest_state(source, raw);

            for (const auto *name : {"acc_mode", "mpc_icaSts", "mrr_vSetDis", "time_gap_set",
                                     "poweroff_req", "ldws_lamp", "ivi_languageSet"}) {
                assert(guest.at(name) == 0);
            }

            assert(deliver(guest) && qinggan11_rejects == rejects + 1);

            for (unsigned i = 0; i < 579; i++) {
                normal[i] = initial.at(fields[i].name).get<unsigned>();
            }

            ++offered;
            assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered);
        }

        normal[286] = normal[373] = normal[404] = 1023;
        ++offered;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered);
        assert(packed(2218, 11) == 2047 && packed(1463, 10) == 1023 && packed(2229, 11) == 2047);
        assert(voyemu::qnx_qinggan11_mcu_layout(source).find("{2218,11,0U,1023U,2047U}") !=
               std::string::npos);
        normal[286] = normal[373] = normal[404] = 0;
        offered++;
        bad = true;
        qinggan11_have = 0;
        assert(dataParser_makedumydata(out, 650) == 650 && qinggan11_ack == offered - 1 &&
               !std::memcmp(out, original_bytes, 650));
        enabled = false;
        const auto calls = rpc_calls;
        assert(dataParser_makedumydata(out, 650) == 650 && rpc_calls == calls &&
               !std::memcmp(out, original_bytes, 650));
        auto reject = [&](Json definition) {
            bool rejected = false;
            try {
                voyemu::qnx_qinggan11_mcu_layout(definition);
            } catch (...) {
                rejected = true;
            }

            assert(rejected);
        };
        auto wrong = source;
        wrong["hardware_adapter"] = "sa8155-qinggan11-731";
        reject(wrong);
        wrong = source;
        wrong["source_parser_sha256"] = std::string(64, '0');
        reject(wrong);
        wrong = source;
        wrong["fields"][37]["wire"]["start_bit"] = 0;
        reject(wrong);
        wrong = source;
        wrong["fields"][286]["wire"]["unknown"]["wire"] = 4095;
        reject(wrong);
        bool old_scope_rejected = false;
        try {
            voyemu::qnx_mcu_request_projection(650, 736, 19);
        } catch (...) {
            old_scope_rejected = true;
        }

        assert(old_scope_rejected);
        bool previous_scope_rejected = false;
        try {
            voyemu::qnx_mcu_request_projection(650, 736, 33);
        } catch (...) {
            previous_scope_rejected = true;
        }

        assert(previous_scope_rejected);
        const auto previous = voyemu::qnx_mcu_projection(
                voyemu::QnxGuestAdapter::sa8155_qinggan11_611,
                voyemu::qnx_adapter(voyemu::QnxGuestAdapter::sa8155_qinggan11_611)
                        .mcu_previous_projected_fields);
        assert(voyemu::qnx_mcu_request_projection(650, 736, 34) == previous);
        assert(!voyemu::qnx_mcu_delivery_scope(
                Json{{"mcu_projection", previous}},
                voyemu::qnx_mcu_projection(voyemu::QnxGuestAdapter::sa8155_qinggan11_611)));
        const auto projection =
                voyemu::qnx_mcu_projection(voyemu::QnxGuestAdapter::sa8155_qinggan11_611);
        assert(voyemu::qnx_mcu_request_projection(650, 736, qinggan11_projected_fields) ==
               projection);
        assert(voyemu::qnx_mcu_delivery_scope(Json{{"mcu_projection", projection}}, projection));
        assert(!voyemu::qnx_mcu_delivery_scope(Json{{"mcu_projection", projection}}, previous));
        assert(!voyemu::qnx_mcu_delivery_scope(
                Json{{"mcu_projection", projection}},
                voyemu::qnx_mcu_projection(voyemu::QnxGuestAdapter::sa8155_qinggan11_731)));
        const Json metadata = {{"hardware_adapter", "sa8155-qinggan11-611"},
                               {"mcu_projection", projection}};
        assert(voyemu::qnx_runtime_layout("test-profile", metadata).projection == projection);
        auto foreign = metadata;
        foreign["hardware_adapter"] = "sa8155-qinggan11-731";
        bool rejected = false;
        try {
            voyemu::qnx_runtime_layout("test-profile", foreign);
        } catch (...) {
            rejected = true;
        }

        assert(rejected);
        assert(!projection.at("settings_feedback_supported").get<bool>() &&
               projection.at("unsupported_fields") == 579 - qinggan11_projected_fields);
        std::cout
                << "sa8155-qinggan11-611 source layout, stock controls, range sentinels, scoped RPC and unchanged unsupported bits passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
