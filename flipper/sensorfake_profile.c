#include "sensorfake_profile.h"
#include "sensorfake_sim.h"

#include <furi.h>
#include <furi_hal_version.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <ble/core/ble_defs.h>
#include <ble/core/ble_std.h>

#define TAG "SensorFakeProfile"

#define HEART_RATE_SERVICE_UUID     (0x180D)
#define HEART_RATE_MEASUREMENT_UUID (0x2A37)
#define RSC_SERVICE_UUID            (0x1814)
#define RSC_MEASUREMENT_UUID        (0x2A53)
#define RSC_FEATURE_UUID            (0x2A54)

/* Attribute records: 1 for the service, 2 per characteristic, 1 per CCCD */
#define HEART_RATE_SERVICE_ATTR_RECORDS (1 + 3)
#define RSC_SERVICE_ATTR_RECORDS        (1 + 3 + 2)

/* Start of every event the BLE stack hands to a service handler. The stack's
 * own definitions of this framing are not part of the app SDK. */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t evt;
    uint8_t plen;
    uint8_t data[];
} SensorFakeHciEvent;

typedef struct {
    FuriHalBleProfileBase base;
    GapSvcEventHandler* event_handler;
    volatile bool connected;
    volatile uint32_t connection_count;
    uint16_t hr_svc_handle;
    BleGattCharacteristicInstance hrm_char;
    uint16_t rsc_svc_handle;
    BleGattCharacteristicInstance rsc_meas_char;
    BleGattCharacteristicInstance rsc_feat_char;
} SensorFakeProfile;
_Static_assert(offsetof(SensorFakeProfile, base) == 0, "Wrong layout");

/* Supported RSC features: total distance, walking/running status */
static const uint8_t rsc_feature[2] = {0x06, 0x00};

static const BleGattCharacteristicParams hrm_char_params = {
    .name = "Heart Rate Measurement",
    .data_prop_type = FlipperGattCharacteristicDataFixed,
    .data.fixed.length = SENSORFAKE_HRM_PACKET_SIZE,
    .uuid.Char_UUID_16 = HEART_RATE_MEASUREMENT_UUID,
    .uuid_type = UUID_TYPE_16,
    .char_properties = CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_CONSTANT,
};

static const BleGattCharacteristicParams rsc_meas_char_params = {
    .name = "RSC Measurement",
    .data_prop_type = FlipperGattCharacteristicDataFixed,
    .data.fixed.length = SENSORFAKE_RSC_PACKET_SIZE,
    .uuid.Char_UUID_16 = RSC_MEASUREMENT_UUID,
    .uuid_type = UUID_TYPE_16,
    .char_properties = CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_CONSTANT,
};

static const BleGattCharacteristicParams rsc_feat_char_params = {
    .name = "RSC Feature",
    .data_prop_type = FlipperGattCharacteristicDataFixed,
    .data.fixed.length = sizeof(rsc_feature),
    .data.fixed.ptr = rsc_feature,
    .uuid.Char_UUID_16 = RSC_FEATURE_UUID,
    .uuid_type = UUID_TYPE_16,
    .char_properties = CHAR_PROP_READ,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_CONSTANT,
};

/* Runs on the BLE event thread, for every event, before the firmware's handler */
static BleEventAckStatus sensorfake_profile_event_handler(void* event, void* context) {
    SensorFakeProfile* profile = context;
    const SensorFakeHciEvent* hci_event = event;

    if(hci_event->evt == HCI_LE_META_EVT_CODE) {
        if(hci_event->data[0] == HCI_LE_CONNECTION_COMPLETE_SUBEVT_CODE) {
            const hci_le_connection_complete_event_rp0* connection =
                (const hci_le_connection_complete_event_rp0*)&hci_event->data[1];
            if(connection->Status == 0) {
                profile->connection_count++;
                profile->connected = true;
            }
        }
    } else if(hci_event->evt == HCI_DISCONNECTION_COMPLETE_EVT_CODE) {
        const hci_disconnection_complete_event_rp0* disconnection =
            (const hci_disconnection_complete_event_rp0*)hci_event->data;
        if(disconnection->Status == 0) {
            profile->connected = false;
        }
    }

    /* Only watching: the firmware still has to process the event */
    return BleEventNotAck;
}

static FuriHalBleProfileBase* sensorfake_profile_start(FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);

    SensorFakeProfile* profile = malloc(sizeof(SensorFakeProfile));
    profile->base.config = sensorfake_profile;
    profile->event_handler =
        ble_event_dispatcher_register_svc_handler(sensorfake_profile_event_handler, profile);

    Service_UUID_t hr_uuid = {.Service_UUID_16 = HEART_RATE_SERVICE_UUID};
    furi_check(ble_gatt_service_add(
        UUID_TYPE_16,
        &hr_uuid,
        PRIMARY_SERVICE,
        HEART_RATE_SERVICE_ATTR_RECORDS,
        &profile->hr_svc_handle));
    ble_gatt_characteristic_init(profile->hr_svc_handle, &hrm_char_params, &profile->hrm_char);

    Service_UUID_t rsc_uuid = {.Service_UUID_16 = RSC_SERVICE_UUID};
    furi_check(ble_gatt_service_add(
        UUID_TYPE_16,
        &rsc_uuid,
        PRIMARY_SERVICE,
        RSC_SERVICE_ATTR_RECORDS,
        &profile->rsc_svc_handle));
    ble_gatt_characteristic_init(
        profile->rsc_svc_handle, &rsc_meas_char_params, &profile->rsc_meas_char);
    ble_gatt_characteristic_init(
        profile->rsc_svc_handle, &rsc_feat_char_params, &profile->rsc_feat_char);
    ble_gatt_characteristic_update(profile->rsc_svc_handle, &profile->rsc_feat_char, NULL);

    return &profile->base;
}

static void sensorfake_profile_stop(FuriHalBleProfileBase* base) {
    furi_check(base);
    furi_check(base->config == sensorfake_profile);

    SensorFakeProfile* profile = (SensorFakeProfile*)base;
    ble_event_dispatcher_unregister_svc_handler(profile->event_handler);
    ble_gatt_characteristic_delete(profile->rsc_svc_handle, &profile->rsc_feat_char);
    ble_gatt_characteristic_delete(profile->rsc_svc_handle, &profile->rsc_meas_char);
    ble_gatt_service_delete(profile->rsc_svc_handle);
    ble_gatt_characteristic_delete(profile->hr_svc_handle, &profile->hrm_char);
    ble_gatt_service_delete(profile->hr_svc_handle);
    free(profile);
}

static void
    sensorfake_profile_get_gap_config(GapConfig* config, FuriHalBleProfileParams profile_params) {
    UNUSED(profile_params);
    furi_check(config);

    memset(config, 0, sizeof(GapConfig));
    /* Only one service fits in the advertisement; apps look for Heart Rate */
    config->adv_service.UUID_Type = UUID_TYPE_16;
    config->adv_service.Service_UUID_16 = HEART_RATE_SERVICE_UUID;
    config->appearance_char = GAP_APPEARANCE_GENERIC_HEART_RATE_SENSOR;
    /* No pairing needed, like a real strap. If the other side asks to bond
     * anyway, the keys go to the app's own key file */
    config->bonding_mode = true;
    config->pairing_method = GapPairingNone;
    config->conn_param.conn_int_min = 0x18; // 30 ms
    config->conn_param.conn_int_max = 0x24; // 45 ms
    config->conn_param.slave_latency = 0;
    config->conn_param.supervisor_timeout = 0;

    /* Use an address of our own, so phones do not mix this sensor up with the
     * Flipper itself (or with its BLE remote) and reuse a cached service list */
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
    config->mac_address[2] += 2;

    /* First byte is the advertisement data type, then the name */
    _Static_assert(
        sizeof(SENSORFAKE_DEVICE_NAME) + 1 <= sizeof(config->adv_name), "Device name is too long");
    config->adv_name[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&config->adv_name[1], SENSORFAKE_DEVICE_NAME, sizeof(SENSORFAKE_DEVICE_NAME));
}

static const FuriHalBleProfileTemplate profile_callbacks = {
    .start = sensorfake_profile_start,
    .stop = sensorfake_profile_stop,
    .get_gap_config = sensorfake_profile_get_gap_config,
};

const FuriHalBleProfileTemplate* sensorfake_profile = &profile_callbacks;

bool sensorfake_profile_is_connected(FuriHalBleProfileBase* base) {
    furi_check(base);
    furi_check(base->config == sensorfake_profile);

    return ((SensorFakeProfile*)base)->connected;
}

uint32_t sensorfake_profile_get_connection_count(FuriHalBleProfileBase* base) {
    furi_check(base);
    furi_check(base->config == sensorfake_profile);

    return ((SensorFakeProfile*)base)->connection_count;
}

void sensorfake_profile_update_hrm(FuriHalBleProfileBase* base, const uint8_t* packet) {
    furi_check(base);
    furi_check(base->config == sensorfake_profile);

    SensorFakeProfile* profile = (SensorFakeProfile*)base;
    ble_gatt_characteristic_update(profile->hr_svc_handle, &profile->hrm_char, packet);
}

void sensorfake_profile_update_rsc(FuriHalBleProfileBase* base, const uint8_t* packet) {
    furi_check(base);
    furi_check(base->config == sensorfake_profile);

    SensorFakeProfile* profile = (SensorFakeProfile*)base;
    ble_gatt_characteristic_update(profile->rsc_svc_handle, &profile->rsc_meas_char, packet);
}
