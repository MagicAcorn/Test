# PC test harness build: the game + gxemu (software GX) for headless
# screenshots and automated play-throughs.  Usage: make -C pc
CXX ?= g++
SRC := ../source
BUILD := build
CXXFLAGS := -std=c++17 -O2 -g -DHV_PC -I$(SRC) -Wall -Wno-unused-function -Wno-missing-braces
LDLIBS := -lz -lm

SOURCES := $(wildcard $(SRC)/*.cpp $(SRC)/core/*.cpp $(SRC)/gfx/*.cpp $(SRC)/game/*.cpp $(SRC)/audio/*.cpp $(SRC)/ui/*.cpp $(SRC)/platform/pc/*.cpp)
OBJECTS := $(patsubst $(SRC)/%.cpp,$(BUILD)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

all: hearthvale_pc

hearthvale_pc: $(OBJECTS)
	$(CXX) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD) hearthvale_pc

-include $(DEPS)
.PHONY: all clean
