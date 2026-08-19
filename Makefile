# Builds both programs and leaves ES and user in this directory, as required
# by section 6.1 of the project statement. Each program can also be built on
# its own from inside server/ or client/.

CC     = gcc
CFLAGS = -std=gnu99 -Wall -Wextra -O2 -g

SERVER_SRCS = server/ES.c server/parser_server.c server/commands.c server/storage.c
USER_SRCS   = client/user.c client/parser_user.c client/commands.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
USER_OBJS   = $(USER_SRCS:.c=.o)

all: ES user

ES: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

user: $(USER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

server/ES.o:            server/ES.c server/commands.h server/parser_server.h server/storage.h
server/parser_server.o: server/parser_server.c server/parser_server.h server/storage.h
server/commands.o:      server/commands.c server/commands.h server/storage.h
server/storage.o:       server/storage.c server/storage.h
client/user.o:          client/user.c client/commands.h client/parser_user.h
client/parser_user.o:   client/parser_user.c client/parser_user.h
client/commands.o:      client/commands.c client/commands.h client/parser_user.h

clean:
	rm -f ES user $(SERVER_OBJS) $(USER_OBJS)

# Also removes the server's persistent store.
distclean: clean
	rm -rf ESDIR server/ESDIR client/user server/ES

.PHONY: all clean distclean
