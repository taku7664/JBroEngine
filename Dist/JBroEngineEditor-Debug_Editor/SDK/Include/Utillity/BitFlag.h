#pragma once

class BitFlag
{
public:
    BitFlag() : mBits(0) {}
    BitFlag(unsigned int flags) : mBits(flags) {}

public:
    inline bool Empty() const { return mBits == 0; }
    inline void Clear() { mBits = 0; }
    inline void Set(unsigned int flags) { mBits = flags; }
    inline void Add(unsigned int flags) { mBits |= flags; }
    inline void Remove(unsigned int flags) { mBits &= ~flags; }
    inline void Toggle(unsigned int flags) { mBits ^= flags; }

    inline unsigned int Get() const { return mBits; }
    inline bool Has(unsigned int flags) const { return (mBits & flags) == flags; }

    explicit operator unsigned int() const { return mBits; }
    explicit operator int() const { return static_cast<int>(mBits); }
    explicit operator bool() const { return mBits != 0; }

    BitFlag operator|(unsigned int rhs) const { return BitFlag(mBits | rhs); }
    BitFlag operator&(unsigned int rhs) const { return BitFlag(mBits & rhs); }
    BitFlag operator^(unsigned int rhs) const { return BitFlag(mBits ^ rhs); }
    BitFlag operator~() const { return BitFlag(~mBits); }

    BitFlag operator|(const BitFlag& rhs) const { return BitFlag(mBits | rhs.mBits); }
    BitFlag operator&(const BitFlag& rhs) const { return BitFlag(mBits & rhs.mBits); }
    BitFlag operator^(const BitFlag& rhs) const { return BitFlag(mBits ^ rhs.mBits); }

    BitFlag& operator|=(unsigned int rhs) { mBits |= rhs; return *this; }
    BitFlag& operator&=(unsigned int rhs) { mBits &= rhs; return *this; }
    BitFlag& operator^=(unsigned int rhs) { mBits ^= rhs; return *this; }

    BitFlag& operator|=(const BitFlag& rhs) { mBits |= rhs.mBits; return *this; }
    BitFlag& operator&=(const BitFlag& rhs) { mBits &= rhs.mBits; return *this; }
    BitFlag& operator^=(const BitFlag& rhs) { mBits ^= rhs.mBits; return *this; }

    bool operator==(const BitFlag& rhs) const { return mBits == rhs.mBits; }
    bool operator!=(const BitFlag& rhs) const { return mBits != rhs.mBits; }
	bool operator[](unsigned int rhs) const { return Has(rhs); }

    BitFlag& operator+=(unsigned int rhs) { Add(rhs); return *this; }
    BitFlag& operator-=(unsigned int rhs) { Remove(rhs); return *this; }
    BitFlag& operator=(unsigned int rhs) { Set(rhs); return *this; }

private:
    unsigned int mBits;

};