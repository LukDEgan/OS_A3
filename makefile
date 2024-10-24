# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -pthread

# Executable name
TARGET = assignment3

# Source files
SRC = assignment3.c

# Build the target
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Run the server with specified arguments (change as needed)
run: $(TARGET)
	./$(TARGET) -l 12345 -p "happy"  # Example command; modify arguments as needed

# Clean up build artifacts
clean:
	rm -f $(TARGET) *.o ./*.txt

.PHONY: all clean run
