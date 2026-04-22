-include ../config.mk

CC = aarch64-linux-gnu-gcc
CFLAGS = -I/usr/include/libdrm -Wall -O2

LDFLAGS = -L/usr/lib/aarch64-linux-gnu
LIBS = -ldrm -lrga

TARGET1 = TEST_BUFFER_YNC
TARGET2 = TEST_A_FRAME
TARGET3 = TEST_DEVICE_CAMERA
TARGET4 = TEST_A_FRAME_RGA
TARGET5 = FINAL

ALL_TARGETS = $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) $(TARGET5)
REMOTE_PATH_SUB = encoder

REMOTE_PATH1 = $(REMOTE_PATH)/$(REMOTE_PATH_SUB)

SRC1 = buffer_YNC.c
SRC2 = drm_Aframe.c
SRC3 = camera_test.c
SRC4 = drm_Aframe_RGA.c
SRC5 = final_video_test.c

all: $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) $(TARGET5)

$(TARGET1): $(SRC1)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

$(TARGET2): $(SRC2)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

$(TARGET3): $(SRC3)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

$(TARGET4): $(SRC4)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

$(TARGET5): $(SRC5)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) $(TARGET5)


deploy:
	@# 使用 ls -t 将目标文件按时间排序，取第一个（最新的）
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