CC     = gcc
CFLAGS = -std=gnu99 -Wall -Wextra -O2 -g
TARGET = user
SRCS   = user.c parser_user.c commands.c
OBJS   = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

user.o:        user.c commands.h parser_user.h
parser_user.o: parser_user.c parser_user.h
commands.o:    commands.c commands.h parser_user.h

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
