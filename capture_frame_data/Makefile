
CFLAGS_utils = -O2 -Wall -Wextra -pedantic -std=c99 

.PHONY: all
all: app
# -ljpeg is important
app: capture.c vdev2.h
	$(CC) -o $@ $< -ljpeg