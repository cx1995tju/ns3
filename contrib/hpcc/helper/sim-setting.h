#ifndef SIM_SETTING_H
#define SIM_SETTING_H

#include <cstdio>
#include <stdexcept>
#include <stdint.h>
#include <unordered_map>
#include <vector>

class SimSetting
{
  public:
    std::unordered_map<uint16_t, std::unordered_map<uint8_t, uint64_t>>
        port_speed; // port_speed[i][j] is node i's j-th port's speed
    uint32_t win{}; // window bound

    void Serialize(FILE* file) const
    {
        // write port_speed
        uint32_t len = 0;
        for (const auto& i : port_speed)
        {
            len += i.second.size();
        }
        fwrite(&len, sizeof(len), 1, file);
        for (const auto& i : port_speed)
        {
            for (const auto& j : i.second)
            {
                fwrite(&i.first, sizeof(i.first), 1, file);
                fwrite(&j.first, sizeof(j.first), 1, file);
                fwrite(&j.second, sizeof(j.second), 1, file);
            }
        }
        // write win
        fwrite(&win, sizeof(win), 1, file);
    }

    void Deserialize(FILE* file)
    {
        decltype(port_speed) ports;
        uint32_t len;
        if (fread(&len, sizeof(len), 1, file) != 1)
        {
            throw std::runtime_error("Missing port count in HPCC trace");
        }
        for (uint32_t i = 0; i < len; ++i)
        {
            uint16_t node;
            uint8_t intf;
            uint64_t bps;
            if (fread(&node, sizeof(node), 1, file) != 1 ||
                fread(&intf, sizeof(intf), 1, file) != 1 || fread(&bps, sizeof(bps), 1, file) != 1)
            {
                throw std::runtime_error("Truncated port table in HPCC trace");
            }
            ports[node][intf] = bps;
        }
        uint32_t window;
        if (fread(&window, sizeof(window), 1, file) != 1)
        {
            throw std::runtime_error("Missing window in HPCC trace");
        }
        win = window;
        port_speed.swap(ports);
    }
};

#endif
