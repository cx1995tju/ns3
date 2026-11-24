#ifndef INT_HEADER_H
#define INT_HEADER_H

#include "ns3/buffer.h"

#include <cstdio>
#include <stdint.h>

namespace ns3
{

class IntHop
{
  public:
    static const uint32_t timeWidth = 24;
    static const uint32_t bytesWidth = 20;
    static const uint32_t qlenWidth = 17;
    static const uint64_t lineRateValues[8];
    // The original wire word stores rate, time, transmitted bytes and queue length from LSB to MSB.
    uint64_t word{};
    static constexpr unsigned rateWidth = 3;

    static constexpr uint64_t Mask(unsigned width)
    {
        return (uint64_t{1} << width) - 1;
    }

    static const uint32_t byteUnit = 128;
    static const uint32_t qlenUnit = 80;
    static uint32_t multi;

    uint64_t GetLineRate() const
    {
        return lineRateValues[word & Mask(rateWidth)];
    }

    uint64_t GetTime() const
    {
        return (word >> rateWidth) & Mask(timeWidth);
    }

    uint64_t ByteCounter() const
    {
        return (word >> (rateWidth + timeWidth)) & Mask(bytesWidth);
    }

    uint64_t GetBytes() const
    {
        return ByteCounter() * byteUnit * multi;
    }

    uint32_t GetQlen() const
    {
        return (word >> (rateWidth + timeWidth + bytesWidth)) * qlenUnit * multi;
    }

    void Set(uint64_t time, uint64_t bytes, uint32_t qlen, uint64_t rate);

    uint64_t GetBytesDelta(const IntHop& prev) const
    {
        return ((ByteCounter() - prev.ByteCounter()) & Mask(bytesWidth)) * byteUnit * multi;
    }

    uint64_t GetTimeDelta(const IntHop& prev) const
    {
        return (GetTime() - prev.GetTime()) & Mask(timeWidth);
    }
};

class IntHeader
{
  public:
    static const uint32_t maxHop = 5;

    enum Mode
    {
        NORMAL = 0,
        TS = 1,
        PINT = 2,
        NONE
    };

    static Mode mode;
    static int pint_bytes;

    IntHop hop[maxHop]{};
    uint16_t nhop{};
    uint64_t ts{};
    uint16_t power{};
    static uint32_t GetStaticSize();
    void PushHop(uint64_t time, uint64_t bytes, uint32_t qlen, uint64_t rate);
    void Serialize(Buffer::Iterator start) const;
    uint32_t Deserialize(Buffer::Iterator start);
    uint64_t GetTs(void);
    uint16_t GetPower(void);
    void SetPower(uint16_t);
};

} // namespace ns3

#endif /* INT_HEADER_H */
