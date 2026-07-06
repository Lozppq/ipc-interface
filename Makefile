# ipc-interface 构建
#
# 本地编译:
#   make
#
# 交叉编译 (示例):
#   make CROSS_COMPILE=aarch64-linux-gnu-
#   make CROSS_COMPILE=arm-linux-gnueabihf-
#
# 输出:
#   build/include/   公开头文件 (保留 src 下子目录结构)
#   build/lib/libipc-interface.so
#   build/bin/       demo 目录下每个 .cpp 对应一个可执行文件

CROSS_COMPILE ?=
CXX      := $(CROSS_COMPILE)g++

BUILD_DIR := build
BUILD_INC := $(BUILD_DIR)/include
BUILD_LIB := $(BUILD_DIR)/lib
BUILD_BIN := $(BUILD_DIR)/bin
BUILD_OBJ := $(BUILD_DIR)/obj

LIB_NAME := ipc-interface
LIB_SO   := $(BUILD_LIB)/lib$(LIB_NAME).so

CXXFLAGS := -std=c++14 -Wall -O2 -fPIC -Isrc
LDFLAGS  := -shared -Wl,-soname,lib$(LIB_NAME).so
LDLIBS   := -lrt -lpthread

LIB_SRCS := \
	src/model/ThreadBase.cpp \
	src/model/MessageThread.cpp \
	src/mul_process/ShmManager.cpp \
	src/mul_process/ReceiveWork.cpp \
	src/mul_process/MessageService.cpp

LIB_OBJS := $(patsubst src/%.cpp,$(BUILD_OBJ)/%.o,$(LIB_SRCS))

# src 下所有 .h；.inl 为模板实现，随头文件一并安装
HEADERS := $(shell find src -name '*.h')
INL_FILES := $(shell find src -name '*.inl')

DEMO_SRCS := $(wildcard demo/*.cpp)
DEMO_BINS := $(patsubst demo/%.cpp,$(BUILD_BIN)/%,$(DEMO_SRCS))

.PHONY: all clean install-headers

all: install-headers $(LIB_SO) $(DEMO_BINS)

install-headers: | $(BUILD_INC)
	@for f in $(HEADERS); do \
		dir=$$(dirname $$f | sed 's|^src/||'); \
		mkdir -p $(BUILD_INC)/$$dir; \
		cp -f $$f $(BUILD_INC)/$$dir/; \
	done
	@for f in $(INL_FILES); do \
		dir=$$(dirname $$f | sed 's|^src/||'); \
		mkdir -p $(BUILD_INC)/$$dir; \
		cp -f $$f $(BUILD_INC)/$$dir/; \
	done

$(LIB_SO): $(LIB_OBJS) | $(BUILD_LIB)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_OBJ)/%.o: src/%.cpp | $(BUILD_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_INC):
	@mkdir -p $(BUILD_INC)/model $(BUILD_INC)/mul_process

$(BUILD_LIB):
	@mkdir -p $(BUILD_LIB)

$(BUILD_OBJ):
	@mkdir -p $(BUILD_OBJ)/model $(BUILD_OBJ)/mul_process

$(BUILD_BIN):
	@mkdir -p $(BUILD_BIN)

# ---------------------------------------------------------------------------
# demo: demo/*.cpp 每个源文件 -> build/bin/<name>
# ---------------------------------------------------------------------------

DEMO_CXXFLAGS := -std=c++14 -Wall -O2 -I$(BUILD_INC)
DEMO_LDFLAGS  := -L$(BUILD_LIB) -l$(LIB_NAME) -Wl,-rpath,'$$ORIGIN/../lib' -lrt -lpthread

$(BUILD_BIN)/%: demo/%.cpp $(LIB_SO) | $(BUILD_BIN)
	$(CXX) $(DEMO_CXXFLAGS) -o $@ $< $(DEMO_LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
	rm -f /dev/shm/shm_lockfree_ring
