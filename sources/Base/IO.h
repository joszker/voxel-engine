#pragma once
#include "Base.h"

#include <istream>
#include <ostream>


namespace IO {


inline bool ReadU8(std::istream& is, u8 *dst)
{
    u8 value = is.get();
    if (is.fail()) return false;

    *dst = value;
    return true;
}


inline bool ReadChar(std::istream& is, char *dst)
{
    char value = is.get();
    if (is.fail()) return false;

    *dst = value;
    return true;
}


inline bool ReadU16(std::istream& is, u16 *dst)
{
    u16 value;
    is.read((char*)&value, 2);
    if (is.fail()) return false;

    *dst = value;
    return true;
}


inline bool ReadU32(std::istream& is, u32 *dst)
{
    u32 value;
    is.read((char*)&value, 4);
    if (is.fail()) return false;

    *dst = value;
    return true;
}


inline bool ReadU64(std::istream& is, u64 *dst)
{
    u64 value;
    is.read((char*)&value, 8);
    if (is.fail()) return false;

    *dst = value;
    return true;
}


inline bool ReadArray(std::istream& is, void *dst, u64 count, size_t stride)
{
    ASSERT(count > 0);

    is.read((char*)dst, count * stride);
    if (is.fail()) return false;
    
    return true;
}


inline void WriteU32(std::ostream& os, u32 value)
{
    os.write((char*)&value, 4);
}


inline void WriteU64(std::ostream& os, u64 value)
{
    os.write((char*)&value, 8);
}

}