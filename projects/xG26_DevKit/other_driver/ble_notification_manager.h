#ifndef BLE_NOTIFICATION_MANAGER_H
#define BLE_NOTIFICATION_MANAGER_H

/**
 * @file ble_notification_manager.h
 * @brief SomniGuard BLE Notification System (Truyền tin Đa hành động)
 * 
 * Module quản lý và đóng gói các thông báo sự kiện khẩn cấp, cảnh báo sinh lý,
 * tình trạng cảm biến và trạng thái nguồn về Mobile App qua BLE GATT Notification/Indication.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mã nhóm sự kiện BLE (Event Type) - 1 byte */
typedef enum {
    SOMNIGUARD_BLE_EVT_TYPE_HEALTH_ALERT   = 0x01, // Cảnh báo sức khỏe / ngưng thở
    SOMNIGUARD_BLE_EVT_TYPE_SENSOR_STATUS  = 0x02, // Trạng thái cảm biến (Finger On/Off, Fault)
    SOMNIGUARD_BLE_EVT_TYPE_POWER_SYSTEM   = 0x03  // Trạng thái nguồn & FSM State
} somniguard_ble_evt_type_t;

/* Mã chi tiết sự kiện BLE (Event Code) - 1 byte */
typedef enum {
    /* 0x01: Health Alerts */
    SOMNIGUARD_BLE_EVT_CODE_APNEA_WARNING  = 0x11, // Phát hiện ngưng thở
    SOMNIGUARD_BLE_EVT_CODE_SPO2_CRITICAL  = 0x12, // SpO2 sụt giảm mạnh (<93%)
    SOMNIGUARD_BLE_EVT_CODE_HR_ABNORMAL    = 0x13, // Nhịp tim bất thường

    /* 0x02: Sensor Status */
    SOMNIGUARD_BLE_EVT_CODE_FINGER_REMOVED = 0x21, // Tuột / tháo ngón tay
    SOMNIGUARD_BLE_EVT_CODE_FINGER_ATTACHED= 0x22, // Đã đeo ngón tay trở lại
    SOMNIGUARD_BLE_EVT_CODE_SENSOR_FAULT   = 0x23, // Lỗi ngắt / bus I2C cảm biến

    /* 0x03: Power & System */
    SOMNIGUARD_BLE_EVT_CODE_BATTERY_LOW    = 0x31, // Cảnh báo pin yếu (<5%)
    SOMNIGUARD_BLE_EVT_CODE_EM4_SHUTOFF    = 0x32, // Tắt nguồn thiết bị (EM4)
    SOMNIGUARD_BLE_EVT_CODE_FSM_STATE_CHG  = 0x33  // Thay đổi trạng thái FSM
} somniguard_ble_evt_code_t;

/* Gói tin binary 8 bytes truyền qua BLE Notification */
#pragma pack(push, 1)
typedef struct {
    uint8_t  event_type;  // Mã nhóm sự kiện (somniguard_ble_evt_type_t)
    uint8_t  event_code;  // Mã chi tiết sự kiện (somniguard_ble_evt_code_t)
    uint16_t seq_num;     // Số thứ tự gói tin (tăng dần 1..65535)
    uint16_t param1;      // Thông số kèm theo 1 (SpO2 %, Battery %, etc.)
    uint16_t param2;      // Thông số kèm theo 2 (Heart Rate bpm, FSM State, etc.)
} somniguard_ble_event_pkt_t;
#pragma pack(pop)

/**
 * @brief Khởi tạo BLE Notification Manager
 */
void somniguard_ble_manager_init(void);

/**
 * @brief Cập nhật trạng thái đăng ký CCCD của Mobile App
 * @param connection_handle Handle kết nối của App
 * @param is_subscribed true nếu App đã bật Notify/Indicate
 */
void somniguard_ble_set_subscribed(uint8_t connection_handle, bool is_subscribed);

/**
 * @brief Kiểm tra xem App hiện tại có đang subscribe Notify/Indicate không
 */
bool somniguard_ble_is_subscribed(void);

/**
 * @brief Hàm chính đóng gói và gửi thông báo BLE khẩn cấp / định kỳ
 * @param type Mã nhóm sự kiện
 * @param code Mã chi tiết sự kiện
 * @param param1 Thông số 1 đi kèm
 * @param param2 Thông số 2 đi kèm
 * @return true nếu gửi thành công, false nếu thất bại
 */
bool somniguard_ble_notify_event(somniguard_ble_evt_type_t type,
                                 somniguard_ble_evt_code_t code,
                                 uint16_t param1,
                                 uint16_t param2);

/**
 * @brief In thông tin debug của gói tin BLE event
 */
void somniguard_ble_log_packet(const somniguard_ble_event_pkt_t *pkt);

#ifdef __cplusplus
}
#endif

#endif // BLE_NOTIFICATION_MANAGER_H
