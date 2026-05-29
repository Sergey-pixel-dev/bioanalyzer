# Minimal scalable Qt6 Makefile
# Project directories
SRC_DIR    = src
INC_DIR    = include
UI_DIR     = ui
BUILD_DIR  = build

# Qt6 tools
CXX      = g++
MOC      = /usr/lib/qt6/moc
UIC      = /usr/lib/qt6/uic
DESIGNER = /usr/lib/qt6/bin/designer

# Compiler / linker flags
CXXFLAGS = -std=c++17 -fPIC $(shell pkg-config --cflags Qt6Widgets) -I$(INC_DIR) -I$(BUILD_DIR)
CXXFLAGS += -fno-diagnostics-show-caret -fdiagnostics-color=never
LDFLAGS  = $(shell pkg-config --libs Qt6Widgets)

TARGET   = $(BUILD_DIR)/app

# Files (recursive search)
SRCS     = $(shell find $(SRC_DIR) -name '*.cpp')
HEADERS  = $(shell find $(INC_DIR) -name '*.h')
UI_FILES = $(shell find $(UI_DIR) -name '*.ui')

# Flat build artifacts (prevents complex path mapping issues)
OBJS     = $(addprefix $(BUILD_DIR)/,$(notdir $(SRCS:.cpp=.o)))
MOC_SRCS = $(addprefix $(BUILD_DIR)/,$(foreach h,$(notdir $(HEADERS)),moc_$(h:.h=.cpp)))
MOC_OBJS = $(MOC_SRCS:.cpp=.o)
UI_HDRS  = $(addprefix $(BUILD_DIR)/,$(foreach f,$(notdir $(UI_FILES)),ui_$(f:.ui=.h)))

# VPATH so Make can find source files by basename
vpath %.cpp $(sort $(dir $(SRCS)))
vpath %.h $(sort $(dir $(HEADERS)))
vpath %.ui $(sort $(dir $(UI_FILES)))

.PHONY: all dirs clean run designer

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS) $(MOC_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.cpp $(UI_HDRS) | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.cpp | dirs
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/moc_%.cpp: %.h | dirs
	$(MOC) $< -o $@

$(BUILD_DIR)/ui_%.h: %.ui | dirs
	$(UIC) $< -o $@

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	$(TARGET)

designer:
	$(DESIGNER) $(UI_FILES) &
