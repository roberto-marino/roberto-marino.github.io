CC = gcc
CFLAGS = -g -Wall
TARGET = programma
OBJS = main.o file1.o file2.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

check:
	@echo "\n=== Simboli file1.o ==="
	nm file1.o
	@echo "\n=== Simboli file2.o ==="
	nm file2.o
	@echo "\n=== Simboli main.o ==="
	nm main.o

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean check
