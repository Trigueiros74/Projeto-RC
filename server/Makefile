CC = gcc
CFLAGS = -Wall -g
TARGET = ES
SRCS = ES.c parser_server.c commands.c
OBJS = $(SRCS:.c=.o)
ESDIR = ESDIR

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
	rm -rf $(ESDIR)

.PHONY: all clean