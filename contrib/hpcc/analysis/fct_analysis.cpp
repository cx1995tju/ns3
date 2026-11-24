// Summarize the eight-column HPCC FCT file by flow-size bin.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{
struct Flow
{
    uint64_t size;
    double slowdown;
};

struct Bin
{
    uint64_t size;
    double percent;
    std::vector<double> results;
};

uint64_t
Number(const std::string& text)
{
    uint64_t result;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        throw std::runtime_error("Invalid unsigned integer: " + text);
    }
    return result;
}

std::vector<Flow>
ReadFlows(const std::string& path, unsigned kind, uint64_t cutoff)
{
    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("Cannot open " + path);
    }
    std::vector<Flow> flows;
    for (std::string line; std::getline(file, line);)
    {
        std::istringstream row(line);
        std::string skip, extra;
        std::string portText, sizeText, startText, fctText, standaloneText;
        if (!(row >> skip))
        {
            continue;
        }
        if (!(row >> skip >> skip >> portText >> sizeText >> startText >> fctText >>
              standaloneText) ||
            row >> extra)
        {
            throw std::runtime_error("Invalid FCT record in " + path + ": " + line);
        }
        const auto port = Number(portText);
        const auto size = Number(sizeText);
        const auto start = Number(startText);
        const auto fct = Number(fctText);
        const auto standalone = Number(standaloneText);
        if (standalone == 0)
        {
            throw std::runtime_error("Zero standalone FCT in " + path);
        }
        if (((port == 100 && kind != 1) || (port == 200 && kind != 0)) && start < cutoff &&
            fct < cutoff - start)
        {
            flows.push_back({size, std::max(1.0, double(fct) / standalone)});
        }
    }
    std::stable_sort(flows.begin(), flows.end(), [](auto a, auto b) { return a.size < b.size; });
    return flows;
}

std::vector<Bin>
MakeBins(const std::string& path, unsigned step)
{
    std::vector<Bin> bins;
    if (path.empty())
    {
        for (unsigned p = 0; p < 100; p += step)
        {
            bins.push_back({0, double(std::min(100u, p + step)), {}});
        }
        return bins;
    }
    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("Cannot open " + path);
    }
    for (std::string line; std::getline(file, line);)
    {
        std::istringstream row(line);
        std::string size, extra;
        if (!(row >> size))
        {
            continue;
        }
        Bin bin{Number(size), 0, {}};
        if (!(row >> bin.percent) || row >> extra || !(bin.percent >= 0 && bin.percent <= 100) ||
            (!bins.empty() && (bin.size <= bins.back().size || bin.percent < bins.back().percent)))
        {
            throw std::runtime_error("Invalid bin in " + path + ": " + line);
        }
        bins.push_back(bin);
    }
    if (bins.empty())
    {
        throw std::runtime_error("No bins in " + path);
    }
    return bins;
}
} // namespace

int
main(int argc, char** argv)
try
{
    std::string prefix = "mix/fct_fat", ccList, steps;
    uint64_t step = 5, kind = 0, cutoff = 3'000'000'000;
    for (int opt; (opt = getopt(argc, argv, "hp:s:S:t:T:c:")) != -1;)
    {
        switch (opt)
        {
        case 'p':
            prefix = optarg;
            break;
        case 's':
            step = Number(optarg);
            break;
        case 'S':
            steps = optarg;
            break;
        case 't':
            kind = Number(optarg);
            break;
        case 'T':
            cutoff = Number(optarg);
            break;
        case 'c':
            ccList = optarg;
            break;
        default:
            std::cerr << "Usage: fct_analysis -p PATH_PREFIX -c SCHEME[,SCHEME...] "
                         "[-s PERCENT_STEP | -S BINS_FILE] [-t 0|1|2] [-T CUTOFF_NS]\n";
            return opt == 'h' ? 0 : 1;
        }
    }
    if (optind != argc || step == 0 || step > 100 || kind > 2 || ccList.empty() ||
        ccList.back() == ',')
    {
        throw std::runtime_error("Invalid arguments. See fct_analysis -h");
    }
    auto bins = MakeBins(steps, step);
    std::istringstream schemes(ccList);
    for (std::string cc; std::getline(schemes, cc, ',');)
    {
        if (cc.empty())
        {
            throw std::runtime_error("Empty congestion control name");
        }
        auto flows = ReadFlows(prefix + "_" + cc + ".txt", kind, cutoff);
        size_t left = 0;
        for (auto& bin : bins)
        {
            size_t right = left;
            if (steps.empty())
            {
                right = flows.size() * static_cast<unsigned>(bin.percent) / 100;
                if (bin.results.empty() && right > left)
                {
                    bin.size = flows[right - 1].size;
                }
            }
            else
            {
                while (right < flows.size() && flows[right].size <= bin.size)
                {
                    ++right;
                }
            }
            std::sort(flows.begin() + left, flows.begin() + right, [](auto a, auto b) {
                return a.slowdown < b.slowdown;
            });
            for (unsigned p : {50, 95, 99})
            {
                bin.results.push_back(right == left
                                          ? std::numeric_limits<double>::quiet_NaN()
                                          : flows[left + (right - left) * p / 100].slowdown);
            }
            left = right;
        }
    }
    for (const auto& bin : bins)
    {
        std::cout << std::fixed << std::setprecision(6) << bin.percent / 100 << ' ' << bin.size;
        for (double value : bin.results)
        {
            std::cout << '\t' << std::setprecision(3) << value;
        }
        std::cout << '\n';
    }
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
