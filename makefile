CC           = gcc
CFLAGS       = -Wall -Wextra -std=gnu11 -g
LIBS         = -lzstd -pthread
SRC          = src/main.c src/pack.c src/unpack.c src/grab.c src/list.c src/insert.c \
               src/compression.c src/decompression.c src/helpers.c
OBJ          = $(patsubst src/%.c, build/%.o, $(SRC))
TARGET       = spare
PREFIX       = /usr/local/bin
MANDIR       = /usr/local/share/man/man1
BASH_COMPDIR = /usr/share/bash-completion/completions
ZSH_COMPDIR  = /usr/share/zsh/site-functions
FISH_COMPDIR = /usr/share/fish/vendor_completions.d

all: $(TARGET) clean

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.c src/spare.h | build
	$(CC) $(CFLAGS) -c -o $@ $< $(LIBS)

build:
	mkdir -p build

install: $(TARGET)
	install -m 755 $(TARGET) $(PREFIX)/$(TARGET)
	install -Dm 644 man/man1/spare.1 $(MANDIR)/spare.1
	@if [ -d "$(BASH_COMPDIR)" ] && [ -w "$(BASH_COMPDIR)" ]; then install -Dm 644 completions/spare.bash $(BASH_COMPDIR)/spare     && echo "installed bash completion"; fi
	@if [ -d "$(ZSH_COMPDIR)"  ] && [ -w "$(ZSH_COMPDIR)"  ]; then install -Dm 644 completions/spare.zsh  $(ZSH_COMPDIR)/_spare     && echo "installed zsh completion";  fi
	@if [ -d "$(FISH_COMPDIR)" ] && [ -w "$(FISH_COMPDIR)" ]; then install -Dm 644 completions/spare.fish $(FISH_COMPDIR)/spare.fish && echo "installed fish completion"; fi

uninstall:
	rm -f $(PREFIX)/$(TARGET)
	rm -f $(MANDIR)/spare.1
	@if [ -d "$(BASH_COMPDIR)" ] && [ -w "$(BASH_COMPDIR)" ]; then rm -f $(BASH_COMPDIR)/spare;      fi
	@if [ -d "$(ZSH_COMPDIR)"  ] && [ -w "$(ZSH_COMPDIR)"  ]; then rm -f $(ZSH_COMPDIR)/_spare;      fi
	@if [ -d "$(FISH_COMPDIR)" ] && [ -w "$(FISH_COMPDIR)" ]; then rm -f $(FISH_COMPDIR)/spare.fish; fi

clean:
	rm -rf build

veryclean:
	rm -rf build
	rm -f $(TARGET)
	rm -rf bench/linux* bench/tmp

test: $(TARGET)
	bash test/run_all.sh

list:
	@grep '^[^#[:space:]\.].*:' makefile

.PHONY: all clean install uninstall veryclean test list
