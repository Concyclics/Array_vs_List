CC := gcc
CFLAGS := -O2 -std=c11 -Wall -Wextra -pedantic
LDFLAGS :=

TARGET := bench_insert
SRC_DIR := src
SRC := $(SRC_DIR)/bench_insert.c $(SRC_DIR)/array_impl.c $(SRC_DIR)/list_impl.c $(SRC_DIR)/blocklist_impl.c $(SRC_DIR)/utils.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o $(SRC_DIR)/*.o
