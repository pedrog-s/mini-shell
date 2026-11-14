CC      = gcc
CFLAGS  = -g -Wall -Wextra -std=c17
NIVELES = nivel1 nivel2 nivel3 nivel4 nivel5 nivel6

# Shell final
all: my_shell

my_shell: src/my_shell.c
	$(CC) $(CFLAGS) $< -o $@

# Versiones intermedias (una por etapa de desarrollo)
niveles: $(NIVELES)

nivel%: niveles/nivel%.c
	$(CC) -g -Wall -std=c17 $< -o $@

clean:
	rm -f my_shell $(NIVELES) *.o

.PHONY: all niveles clean
