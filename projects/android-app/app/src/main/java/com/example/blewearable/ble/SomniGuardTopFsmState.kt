package com.example.blewearable.ble

/**
 * Các trạng thái chính của System Brain FSM (Top Level) trên thiết bị SomniGuard EFR32 xG26 DevKit
 * Đồng bộ với enum somniguard_top_fsm_state_t trong firmware (global.h).
 */
enum class SomniGuardTopFsmState(
    val code: Int,
    val stateName: String,
    val description: String,
    val isTracking: Boolean
) {
    INACTIVE(
        code = 0,
        stateName = "INACTIVE",
        description = "Device powered off or placed on charging dock",
        isTracking = false
    ),
    OFF_FINGER_SUSPEND(
        code = 1,
        stateName = "OFF-FINGER SUSPEND",
        description = "Sensor removed from finger - Low-power standby mode",
        isTracking = false
    ),
    ACTIVE_MODE(
        code = 2,
        stateName = "ACTIVE MODE",
        description = "Wearable attached - User awake or moving",
        isTracking = true
    ),
    NORMAL_SLEEP(
        code = 3,
        stateName = "NORMAL SLEEP",
        description = "Normal sleep tracking - 30s Tensor buffer & respiratory monitoring",
        isTracking = true
    ),
    DEEP_ANALYSIS(
        code = 4,
        stateName = "DEEP ANALYSIS (APNEA ALERT)",
        description = "Apnea alert / Haptic vibration & alarm intervention",
        isTracking = true
    );

    companion object {
        fun fromCode(code: Int): SomniGuardTopFsmState {
            return entries.firstOrNull { it.code == code } ?: INACTIVE
        }
    }
}
