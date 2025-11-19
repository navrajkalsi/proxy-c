#
# Saves the compiled binary in: '/usr/local/bin'
#
# ?= sets a var if not set already
# := does not look ahead for vars not defined yet
#
# Pass in DESTDIR to do a custom install

SHELL = /bin/sh

# Conventions followed: https://www.gnu.org/prep/standards/html_node/Directory-Variables.html
prefix ?= /usr/local
exec_prefix ?= $(prefix)
bindir ?= $(exec_prefix)/bin
includedir ?= $(prefix)/include

INSTALL ?= install

# Project Specific
NAME := proxy-c
SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
# CFLAGS ?= -Wall -Werror -Wextra -Iinclude
# Dev Flags
CFLAGS ?= -Wall -Werror -Wextra -Wconversion -g -fsanitize=address,undefined -Iinclude
# LDFLAGS ?= -lssl -lcrypto

# custom domain certificate & private key
# TO BE PASSED WHILE COMPILATION!
ifdef DOMAIN_CERT
	CFLAGS += -DDOMAIN_CERT="\"$(DOMAIN_CERT)\""
endif

ifdef PRIVATE_KEY
	CFLAGS += -DPRIVATE_KEY="\"$(PRIVATE_KEY)\""
endif

CC = gcc

# Defines that the labels are commands and not files to run
.PHONY: all clean install uninstall

# Build the binary
all: $(NAME)

# Building the binary
# Looks for definition of every .o file
# $@ is for target
# $^ is for all the dependencies
# $< is for input src file
$(NAME): $(OBJ)
	@echo "Building..."
	@$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete!"

# Compiling each .c file to .o
src/%.o: src/%.c
	@$(CC) $(CFLAGS) -c -o $@ $<

# Builds first
install: all
	@mkdir -p $(DESTDIR)$(bindir)
	@$(INSTALL) -m 0755 $(NAME) $(DESTDIR)$(bindir)/$(NAME)
	@echo "You are ready to PROXY!"

uninstall:
	@rm -r $(DESTDIR)$(bindir)/$(NAME)
	@echo "Uninstalled Proxy-C"

clean:
	@rm -f $(NAME) $(OBJ)
	@echo "Removed build files"

