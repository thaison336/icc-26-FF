# BLE Wearable Data Collector & Trend Visualizer Android App 📱⚡

Ứng dụng Android Native hiện đại được xây dựng bằng **Kotlin**, **Jetpack Compose**, **Room Database** và **BLE (Bluetooth Low Energy) APIs** để kết nối với thiết bị đeo (wearable), tự động lưu trữ dữ liệu cảm biến và trực quan hóa biểu đồ xu hướng lịch sử.

---

## 🏗️ Kiến Trúc Hệ Thống (MVVM + Clean Architecture)

```
┌────────────────────────────────────────────────────────────────────────┐
│                          UI LAYER (JETPACK COMPOSE)                    │
│   MainActivity ──> MainScreen ──> DashboardScreen & ScanScreen        │
│                        │                                               │
│                        ▼ (Lắng nghe StateFlow)                         │
├────────────────────────────────────────────────────────────────────────┤
│                         VIEWMODEL LAYER                                │
│                         MainViewModel                                  │
│   (Quản lý trạng thái UI, xử lý tương tác người dùng, bộ lọc thời gian) │
│                        │                                               │
│       ┌────────────────┴────────────────┐                              │
│       ▼                                 ▼                              │
├───────────────────────────────┬────────────────────────────────────────┤
│         BLE ENGINE LAYER      │          DATA / STORAGE LAYER          │
│          BleManager           │            SensorRepository            │
│  (Quét, Kết nối, Nhận gói)   │                   │                    │
│               │               │                   ▼                    │
│               │ (Bắn Stream)  │            AppDatabase                 │
│               └───────────────┼─────────> SensorDataDao                │
│                               │          SensorDataEntity              │
└───────────────────────────────┴────────────────────────────────────────┘
```

---

## 🛠️ Công Nghệ Sử Dụng (Tech Stack)

- **Ngôn ngữ**: Kotlin (JVM target 17)
- **UI Framework**: Jetpack Compose (Material3 Dark Theme)
- **Database**: Room DB (SQLite Abstraction với KSP)
- **Bất đồng bộ & Flow**: Kotlin Coroutines, StateFlow, SharedFlow
- **Bluetooth**: Android `BluetoothLeScanner` & `BluetoothGattCallback`
- **Biểu đồ**: Custom Compose Canvas 2D Renderer

---

## 📁 Cấu Trúc Thư Mục Dự Án

```
c:/Users/DELL/Downloads/app/
├── build.gradle.kts
├── settings.gradle.kts
├── gradle.properties
├── README.md
└── app/
    ├── build.gradle.kts
    └── src/main/
        ├── AndroidManifest.xml
        └── java/com/example/blewearable/
            ├── MainActivity.kt                      # Entry point & Permission Launcher
            ├── ble/
            │   ├── BleConstants.kt                  # Cấu hình mã UUID Dịch vụ BLE
            │   ├── BleDeviceModel.kt                # Model biểu diễn thiết bị BLE
            │   ├── BleConnectionState.kt            # Trạng thái kết nối (Disconnected/Scanning/Connected...)
            │   └── BleManager.kt                    # Quản lý quét, kết nối GATT & giải mã gói tin
            ├── data/
            │   ├── SensorDataEntity.kt              # Thực thể bảng dữ liệu Room DB
            │   ├── SensorDataDao.kt                 # Truy vấn SQL bất đồng bộ
            │   ├── AppDatabase.kt                   # Khởi tạo Room Database Singleton
            │   └── SensorRepository.kt              # Xử lý luồng dữ liệu & thuật toán gom nhóm xu hướng
            ├── ui/
            │   ├── theme/                           # Color, Type, Theme tokens
            │   ├── components/
            │   │   ├── PermissionHandler.kt         # Tự động xin quyền Android 12+ (BLUETOOTH_SCAN/CONNECT)
            │   │   ├── DeviceItem.kt                # Card hiển thị thiết bị BLE kèm chỉ số RSSI
            │   │   └── HistoricalTrendChart.kt      # Biểu đồ xu hướng Canvas tùy biến
            │   └── screens/
            │       ├── ScanScreen.kt                # Màn hình tìm kiếm thiết bị BLE
            │       ├── DashboardScreen.kt           # Màn hình điều khiển, xem chỉ số & biểu đồ
            │       └── MainScreen.kt                # Vỏ ứng dụng chứa Bottom Navigation Bar
            └── viewmodel/
                └── MainViewModel.kt                 # Quản lý StateFlow, UI state & Giả lập Demo
```

---

## ✨ Tính Năng Nổi Bật

1. **Quét & Kết Nối BLE**: Tìm kiếm thiết bị đeo xung quanh, hiển thị cường độ tín hiệu (RSSI) và tự động đăng ký thông báo (GATT Notification).
2. **Giải Mã Gói Dữ Liệu Linh Hoạt**: Đọc các định dạng chuỗi ASCII, số float, số integer hoặc raw byte.
3. **Lưu Trữ Room Database**: Tự động lưu vết dữ liệu vào cơ sở dữ liệu SQLite cục bộ ngay khi nhận gói tin BLE.
4. **Biểu Đồ Xu Hướng Canvas**: Hiển thị biểu đồ dạng cột gradient kết hợp đường cong xu hướng, hỗ trợ bộ lọc mốc thời gian (24 giờ, 7 ngày, 30 ngày).
5. **Bộ Giả Lập Demo Simulator**: Cho phép bấm nút giả lập dữ liệu cảm biến để kiểm tra toàn bộ biểu đồ và DB mà không cần sẵn thiết bị đeo phần cứng.
6. **Cấp Quyền Tự Động**: Tự động kiểm tra và xin quyền Bluetooth runtime theo tiêu chuẩn Android 12+ (`BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT`, `ACCESS_FINE_LOCATION`).

---

## ⚙️ Cấu Hình UUID Thiết Bị Đeo (Custom BLE UUIDs)

Nếu thiết bị đeo của bạn sử dụng mã UUID riêng, bạn chỉ cần thay đổi trong file `BleConstants.kt`:

```kotlin
object BleConstants {
    val CUSTOM_SERVICE_UUID: UUID = UUID.fromString("0000FFE0-0000-1000-8000-00805F9B34FB")
    val CUSTOM_CHARACTERISTIC_UUID: UUID = UUID.fromString("0000FFE1-0000-1000-8000-00805F9B34FB")
}
```

---

## 🚀 Hướng Dẫn Chạy Ứng Dụng Trong Android Studio

1. Mở **Android Studio**.
2. Chọn **File > Open...** và mở thư mục:
   ```
   c:\Users\DELL\Downloads\app
   ```
3. Chờ Android Studio hoàn tất **Gradle Sync**.
4. Cắm điện thoại Android vào PC (đã bật **USB Debugging**) hoặc chọn Android Emulator.
5. Bấm nút green **Run ▶** (hoặc tổ hợp phím `Shift + F10`).
