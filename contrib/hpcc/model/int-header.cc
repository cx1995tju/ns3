#include "int-header.h"

#include "ns3/abort.h"

#include <algorithm>

namespace ns3
{

const uint64_t IntHop::lineRateValues[8] =
    {25000000000lu, 50000000000lu, 100000000000lu, 200000000000lu, 400000000000lu, 0, 0, 0};
uint32_t IntHop::multi = 1;

IntHeader::Mode IntHeader::mode = NONE;
int IntHeader::pint_bytes = 2;

void
IntHop::Set(uint64_t time, uint64_t bytes, uint32_t qlen, uint64_t rate)
{
    auto it = std::find(lineRateValues, lineRateValues + 5, rate);
    NS_ABORT_MSG_IF(it == lineRateValues + 5 || multi == 0, "Unsupported INT link rate or scale");
    word = (it - lineRateValues) | ((time & Mask(timeWidth)) << rateWidth) |
           (((bytes / byteUnit / multi) & Mask(bytesWidth)) << (rateWidth + timeWidth)) |
           (((static_cast<uint64_t>(qlen) / qlenUnit / multi) & Mask(qlenWidth))
            << (rateWidth + timeWidth + bytesWidth));
}

uint32_t
IntHeader::GetStaticSize()
{
    if (mode == NORMAL)
    {
        return sizeof(hop) + sizeof(nhop);
    }
    else if (mode == TS)
    {
        return sizeof(ts);
    }
    else if (mode == PINT)
    {
        return pint_bytes;
    }
    else
    {
        return 0;
    }
}

void
IntHeader::PushHop(uint64_t time, uint64_t bytes, uint32_t qlen, uint64_t rate)
{
    // only do this in INT mode
    if (mode == NORMAL)
    {
        uint32_t idx = nhop % maxHop;
        hop[idx].Set(time, bytes, qlen, rate);
        nhop++;
    }
}

void
IntHeader::Serialize(Buffer::Iterator start) const
{
    Buffer::Iterator i = start;
    if (mode == NORMAL)
    {
        for (uint32_t j = 0; j < maxHop; j++)
        {
            i.WriteU64(hop[j].word);
        }
        i.WriteU16(nhop);
    }
    else if (mode == TS)
    {
        i.WriteU64(ts);
    }
    else if (mode == PINT)
    {
        if (pint_bytes == 1)
        {
            i.WriteU8(power);
        }
        else if (pint_bytes == 2)
        {
            i.WriteU16(power);
        }
    }
}

uint32_t
IntHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    if (mode == NORMAL)
    {
        for (uint32_t j = 0; j < maxHop; j++)
        {
            hop[j].word = i.ReadU64();
        }
        nhop = i.ReadU16();
    }
    else if (mode == TS)
    {
        ts = i.ReadU64();
    }
    else if (mode == PINT)
    {
        if (pint_bytes == 1)
        {
            power = i.ReadU8();
        }
        else if (pint_bytes == 2)
        {
            power = i.ReadU16();
        }
    }
    return GetStaticSize();
}

uint64_t
IntHeader::GetTs(void)
{
    if (mode == TS)
    {
        return ts;
    }
    return 0;
}

uint16_t
IntHeader::GetPower(void)
{
    if (mode == PINT)
    {
        return power;
    }
    return 0;
}

void
IntHeader::SetPower(uint16_t value)
{
    if (mode == PINT)
    {
        power = (pint_bytes == 1) ? static_cast<uint8_t>(value) : value;
    }
}
} // namespace ns3
