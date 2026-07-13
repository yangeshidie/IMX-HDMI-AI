# 1. 指定本地编译器
CC = gcc

# 2. 编译选项：开启所有警告、加入调试信息，并启用核心的 pthread 线程库
CFLAGS = -Wall -g -pthread

# 3. 本地输出的目标可执行文件名
TARGET = COND_VAR

# 4. 源文件
SRC = main.c

# 默认规则：编译生成目标
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# 本地清理规则
clean:
	rm -f $(TARGET)

# 本地直接运行规则（替代之前的远程 run）
run: all
	@echo "================ 正在本地运行 ================"
	./$(TARGET)

.PHONY: all clean run