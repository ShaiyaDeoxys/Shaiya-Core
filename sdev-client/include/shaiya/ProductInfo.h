#pragma once
#include <shaiya/include/common.h>

namespace shaiya
{
    #pragma pack(push, 1)
    struct ProductInfo
    {
        char* productName;     //0x00
        char* description;     //0x04
        PAD(0x88);
        char* productCode;     //0x90
        // 0x94
    };
    #pragma pack(pop)

    static_assert(sizeof(ProductInfo) == 0x94);
}
