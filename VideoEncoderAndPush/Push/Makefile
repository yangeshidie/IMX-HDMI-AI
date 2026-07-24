-include ../../config.mk

CC = aarch64-linux-gnu-gcc
INC_DIR = ./include
SRC_DIR = ./src

BUILD_DIR = build


CFLAGS = -I$(INC_DIR) -Wall -O2 -g -pthread
CFLAGS += --sysroot=$(SYSROOT)
CFLAGS += -I$(SYSROOT)/usr/include
CFLAGS += -I$(SYSROOT)/usr/include/libdrm   
CFLAGS += -I$(SYSROOT)/usr/include/drm      

LDFLAGS = --sysroot=$(SYSROOT)
LDFLAGS += -L$(SYSROOT)/usr/lib/aarch64-linux-gnu

LIBS = -ldrm -lrga -lrockchip_mpp

TARGET = $(BUILD_DIR)/TEST
ALL_TARGETS = $(TARGET)

REMOTE_PATH_SUB = encoderAndPush
REMOTE_PATH1 = $(REMOTE_PATH)/$(REMOTE_PATH_SUB)

SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/camera.c \
       $(SRC_DIR)/display.c \
       $(SRC_DIR)/rga_process.c \
       $(SRC_DIR)/frame_queue.c \
       $(SRC_DIR)/mpp_encoder.c

OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean deploy run

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

deploy: all
	@NEWEST_BIN=$$(ls -t $(ALL_TARGETS) 2>/dev/null | head -n 1); \
	if [ -z "$$NEWEST_BIN" ]; then \
		echo "错误：没有找到已编译的目标文件，请先执行 make"; \
		exit 1; \
	fi; \
	echo "检测到最新的编译文件为: $$NEWEST_BIN"; \
	echo "正在传输到 $(REMOTE_IP)..."; \
	scp $$NEWEST_BIN $(REMOTE_USER)@$(REMOTE_IP):$(REMOTE_PATH1); \
	echo "传输完成！"

run: deploy
	@NEWEST_BIN=$$(ls -t $(ALL_TARGETS) 2>/dev/null | head -n 1); \
	BIN_NAME=$$(basename $$NEWEST_BIN); \
	echo "正在远程执行: $$BIN_NAME ..."; \
	ssh $(REMOTE_USER)@$(REMOTE_IP) "chmod +x $(REMOTE_PATH1)/$$BIN_NAME && $(REMOTE_PATH1)/$$BIN_NAME"