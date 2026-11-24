#ifndef HPCC_TRACE_FILTER_HPP
#define HPCC_TRACE_FILTER_HPP

#include "../model/trace-format.h"

#include <charconv>
#include <cstring>
#include <functional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using TracePredicate = std::function<bool(const ns3::TraceFormat&)>;

inline uint64_t
TraceNumber(std::string_view value)
{
    int base = 10;
    if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
    {
        value.remove_prefix(2);
        base = 16;
    }
    uint64_t number{};
    auto [next, error] = std::from_chars(value.data(), value.data() + value.size(), number, base);
    if (error != std::errc{} || next != value.data() + value.size())
    {
        throw std::invalid_argument("Invalid filter number: " + std::string(value));
    }
    return number;
}

inline TracePredicate
TraceTuple(const std::string& field, const std::string& op, const std::string& input)
{
    std::istringstream values(input);
    std::vector<uint64_t> parts;
    for (std::string part; std::getline(values, part, ',');)
    {
        parts.push_back(TraceNumber(part));
    }
    if (op != "=" || input.back() == ',')
    {
        throw std::invalid_argument("Invalid " + field + " filter");
    }
    if (field == "queue" && parts.size() == 3)
    {
        return [parts](const auto& tr) {
            return tr.node == parts[0] && tr.intf == parts[1] && tr.qidx == parts[2];
        };
    }
    if (field == "queue" || parts.size() != 4)
    {
        throw std::invalid_argument("Wrong number of fields in " + field);
    }
    return [field, parts](const auto& tr) {
        uint16_t sport, dport;
        switch (tr.l3Prot)
        {
        case 0x11:
            sport = tr.data.sport;
            dport = tr.data.dport;
            break;
        case 0xfc:
        case 0xfd:
            sport = tr.ack.sport;
            dport = tr.ack.dport;
            break;
        case 0:
            sport = tr.qp.sport;
            dport = tr.qp.dport;
            break;
        default:
            return false;
        }
        return (field != "rflow" && tr.sip == parts[0] && tr.dip == parts[1] && sport == parts[2] &&
                dport == parts[3]) ||
               (field != "flow" && tr.sip == parts[1] && tr.dip == parts[0] && sport == parts[3] &&
                dport == parts[2]);
    };
}

inline TracePredicate
TraceComparison(const std::string& expr)
{
    static const std::regex compare(R"(([a-zA-Z0-9.]+)\s*(>=|<=|!=|=|>|<)\s*([a-fA-F0-9xX,]+))");
    std::smatch match;
    if (!std::regex_match(expr, match, compare))
    {
        throw std::invalid_argument("Invalid filter expression: " + expr);
    }
    const std::string field = match[1], op = match[2], value = match[3];
    if (field == "queue" || field == "flow" || field == "rflow" || field == "biflow")
    {
        return TraceTuple(field, op, value);
    }
    // The trace union overlays packet fields. Read their bytes just as they appear on disk.
    using Getter = uint64_t (*)(const ns3::TraceFormat&);
#define TRACE_FIELD(member)                                                                        \
    {                                                                                              \
        #member, [](const ns3::TraceFormat& tr) -> uint64_t {                                      \
            decltype(tr.member) number;                                                            \
            std::memcpy(&number, &tr.member, sizeof(number));                                      \
            return number;                                                                         \
        }                                                                                          \
    }
    static const std::unordered_map<std::string, Getter> fields{
        TRACE_FIELD(time),       TRACE_FIELD(node),       TRACE_FIELD(nodeType),
        TRACE_FIELD(intf),       TRACE_FIELD(qidx),       TRACE_FIELD(qlen),
        TRACE_FIELD(sip),        TRACE_FIELD(dip),        TRACE_FIELD(size),
        TRACE_FIELD(l3Prot),     TRACE_FIELD(event),      TRACE_FIELD(ecn),
        TRACE_FIELD(data.sport), TRACE_FIELD(data.dport), TRACE_FIELD(data.seq),
        TRACE_FIELD(ack.sport),  TRACE_FIELD(ack.dport),  TRACE_FIELD(ack.flags),
        TRACE_FIELD(qp.sport),   TRACE_FIELD(qp.dport)};
#undef TRACE_FIELD
    auto fieldIt = fields.find(field);
    if (fieldIt == fields.end())
    {
        throw std::invalid_argument("Unknown trace field: " + field);
    }
    using Compare = bool (*)(uint64_t, uint64_t);
    static const std::unordered_map<std::string, Compare> operators{
        {"=", [](auto a, auto b) { return a == b; }},
        {"!=", [](auto a, auto b) { return a != b; }},
        {"<", [](auto a, auto b) { return a < b; }},
        {"<=", [](auto a, auto b) { return a <= b; }},
        {">", [](auto a, auto b) { return a > b; }},
        {">=", [](auto a, auto b) { return a >= b; }}};
    return [get = fieldIt->second, cmp = operators.at(op), n = TraceNumber(value)](const auto& tr) {
        return cmp(get(tr), n);
    };
}

// Both operators use the upstream right-associative grammar. Parentheses override grouping.
inline TracePredicate
ParseTraceFilter(std::string_view expr)
{
    constexpr std::string_view space = " \t\n\r";
    auto start = expr.find_first_not_of(space);
    if (start == expr.npos)
    {
        throw std::invalid_argument("Empty trace filter");
    }
    expr = expr.substr(start, expr.find_last_not_of(space) - start + 1);
    unsigned depth = 0;
    for (size_t i = 0; i < expr.size(); ++i)
    {
        switch (expr[i])
        {
        case '(':
            ++depth;
            break;
        case ')':
            if (depth == 0)
            {
                throw std::invalid_argument("Unexpected closing parenthesis");
            }
            --depth;
            break;
        case '&':
        case '|':
            if (depth == 0)
            {
                auto left = ParseTraceFilter(expr.substr(0, i));
                auto right = ParseTraceFilter(expr.substr(i + 1));
                if (expr[i] == '&')
                {
                    return [left, right](const auto& tr) { return left(tr) && right(tr); };
                }
                return [left, right](const auto& tr) { return left(tr) || right(tr); };
            }
        }
    }
    if (depth != 0)
    {
        throw std::invalid_argument("Missing closing parenthesis");
    }
    if (expr.front() == '(' && expr.back() == ')')
    {
        return ParseTraceFilter(expr.substr(1, expr.size() - 2));
    }
    return TraceComparison(std::string(expr));
}
#endif
