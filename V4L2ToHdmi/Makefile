CC = aarch64-linux-gnu-gcc
CFLAGS = -I/usr/include/libdrm -Wall -O2
LDFLAGS = -L/usr/lib/aarch64-linux-gnu
LIBS = -ldrm

TARGET1 = TEST_PURE_GREEN
TARGET2 = TEST_A_FRAME

SRC1 = drm_green.c
SRC2 = drm_Aframe.c

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(SRC1)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

$(TARGET2): $(SRC2)
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET1) $(TARGET2)

.PHONY: all clean