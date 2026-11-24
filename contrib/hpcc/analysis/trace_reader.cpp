#include "../helper/sim-setting.h"
#include "trace_filter.hpp"
#include "utils.hpp"

#include <iostream>
#include <memory>

int
main(int argc, char** argv)
try
{
    if (argc == 2 && std::string_view(argv[1]) == "-h")
    {
        std::cout << "Usage: trace_reader TRACE_FILE [FILTER]\n";
        return 0;
    }
    if (argc < 2 || argc > 3)
    {
        throw std::invalid_argument("Usage: trace_reader TRACE_FILE [FILTER]");
    }
    TracePredicate filter = argc == 3 ? ParseTraceFilter(argv[2]) : TracePredicate{};
    auto close = [](FILE* file) { fclose(file); };
    std::unique_ptr<FILE, decltype(close)> file(fopen(argv[1], "rb"), close);
    if (!file)
    {
        throw std::runtime_error(std::string("Cannot open trace: ") + argv[1]);
    }
    SimSetting settings;
    settings.Deserialize(file.get());
    ns3::TraceFormat record;
    while (size_t read = fread(&record, 1, sizeof(record), file.get()))
    {
        if (read != sizeof(record))
        {
            throw std::runtime_error("Truncated HPCC trace record");
        }
        if (!filter || filter(record))
        {
            print_trace(record);
        }
    }
    if (ferror(file.get()))
    {
        throw std::runtime_error("Error reading HPCC trace");
    }
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
