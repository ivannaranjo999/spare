CC     = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -g
LIBS   = -lz -pthread
SRC    = src/main.c src/pack.c src/unpack.c src/grab.c src/list.c src/insert.c \
         src/compression.c src/decompression.c src/helpers.c
OBJ    = $(patsubst src/%.c, build/%.o, $(SRC))
TARGET = sar
PREFIX = /usr/local/bin

all: $(TARGET) clean

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.c src/sar.h | build
	$(CC) $(CFLAGS) -c -o $@ $< $(LIBS)

build:
	mkdir -p build

install: $(TARGET)
	install -m 755 $(TARGET) $(PREFIX)/$(TARGET)

uninstall:
	rm -f $(PREFIX)/$(TARGET)

clean:
	rm -rf build

veryclean:
	rm -rf build
	rm -f $(TARGET)

test: $(TARGET)
	bash test/run_all.sh

.PHONY: all clean install uninstall veryclean test
