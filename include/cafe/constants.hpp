#ifndef CAFE_CONSTANTS_HPP
#define CAFE_CONSTANTS_HPP

#include <cstdint>

namespace cafe {
namespace access_flag {
constexpr uint16_t acc_public = 0x0001;
constexpr uint16_t acc_private = 0x0002;
constexpr uint16_t acc_protected = 0x0004;
constexpr uint16_t acc_static = 0x0008;
constexpr uint16_t acc_final = 0x0010;
constexpr uint16_t acc_super = 0x0020;
constexpr uint16_t acc_synchronized = 0x0020;
constexpr uint16_t acc_open = 0x0020;
constexpr uint16_t acc_transitive = 0x0020;
constexpr uint16_t acc_volatile = 0x0040;
constexpr uint16_t acc_bridge = 0x0040;
constexpr uint16_t acc_static_phase = 0x0040;
constexpr uint16_t acc_varargs = 0x0080;
constexpr uint16_t acc_transient = 0x0080;
constexpr uint16_t acc_native = 0x0100;
constexpr uint16_t acc_interface = 0x0200;
constexpr uint16_t acc_abstract = 0x0400;
constexpr uint16_t acc_strict = 0x0800;
constexpr uint16_t acc_synthetic = 0x1000;
constexpr uint16_t acc_annotation = 0x2000;
constexpr uint16_t acc_enum = 0x4000;
constexpr uint16_t acc_mandated = 0x8000;
constexpr uint16_t acc_module = 0x8000;
}

namespace class_version {
constexpr uint16_t v1_1 = 45;
constexpr uint16_t v1_2 = 46;
constexpr uint16_t v1_3 = 47;
constexpr uint16_t v1_4 = 48;
constexpr uint16_t v5 = 49;
constexpr uint16_t v6 = 50;
constexpr uint16_t v7 = 51;
constexpr uint16_t v8 = 52;
constexpr uint16_t v9 = 53;
constexpr uint16_t v10 = 54;
constexpr uint16_t v11 = 55;
constexpr uint16_t v12 = 56;
constexpr uint16_t v13 = 57;
constexpr uint16_t v14 = 58;
constexpr uint16_t v15 = 59;
constexpr uint16_t v16 = 60;
constexpr uint16_t v17 = 61;
constexpr uint16_t v18 = 62;
constexpr uint16_t v19 = 63;
constexpr uint16_t v20 = 64;
constexpr uint16_t v21 = 65;
}
}

#endif //CAFE_CONSTANTS_HPP
