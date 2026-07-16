-include ../../config.mk

CC = aarch64-linux-gnu-gcc
INC_DIR = ./include
SRC_DIR = ./src
SYSROOT = /home/yanmobile/my_software/RKSDK/TaishanPi-3-Linux/debian/binary

CFLAGS = -I$(INC_DIR) -Wall -O2 -g -pthread
CFLAGS += --sysroot=$(SYSROOT)
CFLAGS += -I$(SYSROOT)/usr/include
CFLAGS += -I$(SYSROOT)/usr/include/libdrm   
CFLAGS += -I$(SYSROOT)/usr/include/drm      

LDFLAGS = --sysroot=$(SYSROOT)
LDFLAGS += -L$(SYSROOT)/usr/lib/aarch64-linux-gnu

LIBS = -ldrm -lrga

# 2. 目标文件定义（最终只输出一个 TEST）
TARGET = TEST
ALL_TARGETS = $(TARGET)

# 3. 远程部署路径
REMOTE_PATH_SUB = MultiThread
REMOTE_PATH1 = $(REMOTE_PATH)/$(REMOTE_PATH_SUB)

# 4. 源文件列表（把 src 目录下的所有核心文件都包进来）
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/camera.c \
       $(SRC_DIR)/display.c \
       $(SRC_DIR)/rga_process.c \
       $(SRC_DIR)/frame_queue.c

# 5. 伪目标声明
.PHONY: all clean deploy run

# 6. 构建规则
all: $(TARGET)

# 将所有源文件一起编译并链接成唯一的 TARGET (TEST)
$(TARGET): $(SRCS)
	$(CC) $(SRCS) -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

# 7. 清理规则
clean:
	rm -f $(TARGET)

# 8. 部署规则（完全保留你喜欢的高级 Shell 语法形式）
deploy:
	@NEWEST_BIN=$$(ls -t $(ALL_TARGETS) 2>/dev/null | head -n 1); \
	if [ -z "$$NEWEST_BIN" ]; then \
		echo "错误：没有找到已编译的目标文件，请先执行 make"; \
		exit 1; \
	fi; \
	echo "检测到最新的编译文件为: $$NEWEST_BIN"; \
	echo "正在传输到 $(REMOTE_IP)..."; \
	scp $$NEWEST_BIN $(REMOTE_USER)@$(REMOTE_IP):$(REMOTE_PATH1); \
	echo "传输完成！"

# 9. 远程运行规则
run: deploy
	@NEWEST_BIN=$$(ls -t $(ALL_TARGETS) 2>/dev/null | head -n 1); \
	echo "正在远程执行: $$NEWEST_BIN ..."; \
	ssh $(REMOTE_USER)@$(REMOTE_IP) "chmod +x $(REMOTE_PATH1)/$$NEWEST_BIN && $(REMOTE_PATH1)/$$NEWEST_BIN"