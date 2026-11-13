#pragma once

#include <furi_ble/profile_interface.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Optional arguments to pass along with the profile template as
 * FuriHalBleProfileParams */
typedef struct {
    /** Name shown to apps while scanning, at most SENSORFAKE_NAME_MAX_LENGTH
     * characters. NULL for SENSORFAKE_NAME_DEFAULT */
    const char* device_name;
} SensorFakeProfileParams;

/** BLE profile with a Heart Rate service (0x180D) and a Running Speed and
 * Cadence service (0x1814) */
extern const FuriHalBleProfileTemplate* sensorfake_profile;

/** Check if an app is connected to the sensor
 *
 * The firmware's own Bluetooth status only reports a connection once the
 * other side has paired. Fitness apps do not pair with a heart rate sensor, so
 * the profile tracks the link itself.
 *
 * @param profile   profile instance
 *
 * @return          true if connected
 */
bool sensorfake_profile_is_connected(FuriHalBleProfileBase* profile);

/** Number of connections made since the profile was started
 *
 * @param profile   profile instance
 *
 * @return          connection count
 */
uint32_t sensorfake_profile_get_connection_count(FuriHalBleProfileBase* profile);

/** Send a Heart Rate Measurement (0x2A37) notification
 *
 * @param profile   profile instance
 * @param packet    SENSORFAKE_HRM_PACKET_SIZE bytes
 */
void sensorfake_profile_update_hrm(FuriHalBleProfileBase* profile, const uint8_t* packet);

/** Send an RSC Measurement (0x2A53) notification
 *
 * @param profile   profile instance
 * @param packet    SENSORFAKE_RSC_PACKET_SIZE bytes
 */
void sensorfake_profile_update_rsc(FuriHalBleProfileBase* profile, const uint8_t* packet);

#ifdef __cplusplus
}
#endif
