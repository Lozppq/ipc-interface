# ipc-interface 构建
#
# 本地编译（Linux）:
#   make
#
# 交叉编译示例:
#   make CROSS_COMPILE=aarch64-buildroot-linux-gnu-
#
# 输出:
#   build/include/                 公开头文件
#   build/lib/libipc-interface.so
#   build/bin/daemon               守护进程
#   build/bin/process_1  build/bin/process_2  demo 进程

CROSS_COMPILE ?=
CXX      := $(CROSS_COMPILE)g++

BUILD_DIR := build
BUILD_INC := $(BUILD_DIR)/include
BUILD_LIB := $(BUILD_DIR)/lib
BUILD_BIN := $(BUILD_DIR)/bin
BUILD_OBJ := $(BUILD_DIR)/obj

LIB_NAME := ipc-interface
LIB_SO   := $(BUILD_LIB)/lib$(LIB_NAME).so
DAEMON   := $(BUILD_BIN)/daemon

CXXFLAGS := -std=c++14 -Wall -O2 -fPIC -Isrc
LDFLAGS  := -shared -Wl,-soname,lib$(LIB_NAME).so
LDLIBS   := -lrt -lpthread

LIB_SRCS := \
	src/model/ThreadBase.cpp \
	src/model/MessageThread.cpp \
	src/mul_process/ShmManager.cpp \
	src/mul_process/StreamShmCreator.cpp \
	src/mul_process/ReceiveWork.cpp \
	src/mul_process/SendWork.cpp \
	src/mul_process/ProcessManager.cpp \
	src/log/Log_Print.cpp

LIB_OBJS := $(patsubst src/%.cpp,$(BUILD_OBJ)/%.o,$(LIB_SRCS))

DAEMON_SRC := src/daemon/Daemon.cpp
DAEMON_OBJ := $(BUILD_OBJ)/daemon/Daemon.o

HEADERS := $(shell find src -name '*.h' 2>/dev/null)
INL_FILES := $(shell find src -name '*.inl' 2>/dev/null)

DEMO_SRCS := $(wildcard demo/*.cpp)
DEMO_BINS := $(patsubst demo/%.cpp,$(BUILD_BIN)/%,$(DEMO_SRCS))

APP_CXXFLAGS := -std=c++14 -Wall -O2 -I$(BUILD_INC) -Isrc
APP_LDFLAGS  := -L$(BUILD_LIB) -l$(LIB_NAME) -Wl,-rpath,'$$ORIGIN/../lib' $(LDLIBS)

.PHONY: all clean lib daemon demos install-headers

all: lib daemon demos

lib: install-headers $(LIB_SO)

daemon: $(DAEMON)

demos: $(DEMO_BINS)

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

$(DAEMON): $(DAEMON_OBJ) $(LIB_SO) | $(BUILD_BIN)
	$(CXX) $(APP_CXXFLAGS) -o $@ $(DAEMON_OBJ) $(APP_LDFLAGS)

$(BUILD_OBJ)/%.o: src/%.cpp | $(BUILD_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_BIN)/%: demo/%.cpp $(LIB_SO) | $(BUILD_BIN)
	$(CXX) $(APP_CXXFLAGS) -o $@ $< $(APP_LDFLAGS)

$(BUILD_INC) $(BUILD_LIB) $(BUILD_BIN):
	@mkdir -p $@

$(BUILD_OBJ):
	@mkdir -p $(BUILD_OBJ)/model $(BUILD_OBJ)/mul_process $(BUILD_OBJ)/daemon $(BUILD_OBJ)/log

clean:
	rm -rf $(BUILD_DIR)
