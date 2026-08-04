# Hướng dẫn Tra cứu Mã Lỗi (Error Codes) trong Silicon Labs Simplicity SDK

Tài liệu này hướng dẫn chi tiết quy trình tra cứu và giải mã các mã lỗi (ví dụ: `status: 0x0014`, `0x000F`, `0x1002`) khi lập trình dòng chip EFR32 / BGM220 với Simplicity SDK.

---

## 1. Tra cứu bằng IDE (VS Code / Simplicity Studio)

### Cách A: Tìm kiếm toàn bộ dự án (Global Search)
1. Trong VS Code / Simplicity Studio, nhấn tổ hợp phím **`Ctrl + Shift + F`** (hoặc `Ctrl + H`).
2. Nhập mã lỗi định dạng Hex thu được từ Serial Log (ví dụ: `0x0014` hoặc `0x14`).
3. IDE sẽ quét qua các file header SDK và chỉ ra dòng định nghĩa nguyên nhân lỗi:
   ```c
   #define SL_STATUS_ISR ((sl_status_t)0x0014) ///< Illegal call from ISR.
   ```

### Cách B: Đi tới File định nghĩa (Go to Definition)
1. Trong file mã nguồn C (`app.c`), giữ phím **`Ctrl` và click chuột** (hoặc nhấn **`F12`**) vào kiểu dữ liệu trả về `sl_status_t` của bất kỳ hàm API nào.
2. IDE sẽ tự động mở file quản lý mã lỗi trung tâm: [sl_status.h](file:///c:/Users/DELL/Documents/_PROJECTS/GitHub/icc-26-FF/projects/bt_soc_empty/simplicity_sdk_2026.6.1/platform_core/platform/common/inc/sl_status.h).

---

## 2. Cấu trúc Quản lý Mã Lỗi của Simplicity SDK

Tất cả các mã lỗi chuẩn của Silicon Labs đều được định nghĩa tại duy nhất một file:
* **Đường dẫn trong SDK**: `platform/common/inc/sl_status.h`
* **Đường dẫn dự án**: [sl_status.h](file:///c:/Users/DELL/Documents/_PROJECTS/GitHub/icc-26-FF/projects/bt_soc_empty/simplicity_sdk_2026.6.1/platform_core/platform/common/inc/sl_status.h)

### Các Dải Mã Lỗi (Error Ranges):

| Dải mã Hex | Nhóm chức năng | Các mã phổ biến & Ý nghĩa |
| :--- | :--- | :--- |
| **`0x0000` - `0x00FF`** | **Mã lỗi hệ thống chung** | • `0x0000` (`SL_STATUS_OK`): Thành công<br>• `0x0002` (`SL_STATUS_FAIL`): Lỗi chung<br>• `0x000F` (`SL_STATUS_INVALID_PARAMETER`): Tham số truyền vào hàm sai<br>• `0x0014` (`SL_STATUS_ISR`): Gọi hàm bị cấm bên trong ngắt ISR |
| **`0x0200` - `0x02FF`** | **Ngoại vi / HAL** | Lỗi liên quan đến SPI, I2C, USART, GPIO... |
| **`0x1000` - `0x13FF`** | **Bluetooth Stack** | Lỗi kết nối BLE, Controller, GATT, Mesh... |

---

## 3. Tra cứu Tài liệu Trực tuyến (Online Docs)

1. **Trang Docs chính thức**: Truy cập [Silicon Labs Platform Status Codes](https://docs.silabs.com/gecko-platform/latest/platform-common/sl-status).
2. **Tìm kiếm Google**: Gõ từ khóa dạng: `Silicon Labs 0x0014` hoặc `Silicon Labs status 0x0014`.
