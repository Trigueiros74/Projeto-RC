CC      = gcc
CFLAGS  = -std=gnu99 -Wall -Wextra -O2 -g
TARGET  = ES
SRCS    = ES.c parser_server.c commands.c storage.c
OBJS    = $(SRCS:.c=.o)
ESDIR   = ESDIR

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

ES.o:            ES.c commands.h parser_server.h storage.h
parser_server.o: parser_server.c parser_server.h storage.h
commands.o:      commands.c commands.h storage.h
storage.o:       storage.c storage.h

clean:
	rm -f $(TARGET) $(OBJS)

# Also throws away every event, user and reservation stored on disk.
distclean: clean
	rm -rf $(ESDIR)

.PHONY: all clean distclean
