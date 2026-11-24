CXXFLAGS ?= -O2
OUT ?= .

all: $(OUT)/trace_reader $(OUT)/fct_analysis

$(OUT)/trace_reader: trace_reader.cpp trace_filter.hpp utils.hpp ../model/trace-format.h ../helper/sim-setting.h
	@mkdir -p "$(OUT)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 $< $(LDFLAGS) -o "$@"

$(OUT)/fct_analysis: fct_analysis.cpp
	@mkdir -p "$(OUT)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 $< $(LDFLAGS) -o "$@"

.PHONY: all
