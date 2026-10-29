#ifndef _ENDIAN_UTILS_H_
#define _ENDIAN_UTILS_H_

#include <cstdint>

namespace EndianUtils {

    inline bool is_big_endian()
    {
        const uint32_t value = 0x01020304;
        return reinterpret_cast<const unsigned char*>(&value)[0] == 0x01;
    }

    void big_16(uint16_t &value);    // Only swaps if sys is little endian
    void big_32(uint32_t &value);    // Only swaps if sys is little endian
    void little_16(uint16_t &value); // Only swaps if sys is big endian
    void little_32(uint32_t &value); // Only swaps if sys is big endian
    void swap_endian(uint32_t &value);
    void swap_endian(uint16_t &value);
    
};

#endif // _ENDIAN_UTILS_H_