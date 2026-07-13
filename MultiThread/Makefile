-include ../config.mk

# 修改这里：使用 g++ 替代 gcc
CC = aarch64-linux-gnu-g++
# 加上 -pthread 参数，这是 Linux 多线程编译的核心标志
CFLAGS = -I/usr/include/libdrm -Wall -O2 -pthread

LDFLAGS = -L/usr/lib/aarch64-linux-gnu
# 加上 -lpthread 链接线程库
LIBS = -ldrm -lrga -lpthread

TARGET1 = TEST_MODEL

ALL_TARGETS = $(TARGET1)

REMOTE_PATH_SUB = MultiThread
REMOTE_PATH1 = $(REMOTE_PATH)/$(REMOTE_PATH_SUB)

SRC1 = MultiThread_model.cpp

all: $(TARGET1)

$(TARGET1): $(SRC1)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET1)

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

run: deploy
	@NEWEST_BIN=$$(ls -t $(ALL_TARGETS) 2>/dev/null | head -n 1); \
	echo "正在远程执行: $$NEWEST_BIN ..."; \
	ssh $(REMOTE_USER)@$(REMOTE_IP) "chmod +x $(REMOTE_PATH1)/$$NEWEST_BIN && $(REMOTE_PATH1)/$$NEWEST_BIN"

.PHONY: all clean deploy run