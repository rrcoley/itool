#CFLAGS 	= -Wall -Wextra -O2 `pkg-config --cflags sqlite3 openssl`
#LDFLAGS = `pkg-config --libs sqlite3 openssl`

CC 	= gcc
CFLAGS 	= -g -Wall -Wextra -O2 -I/opt/homebrew/Cellar/openssl@3/3.5.0/include
LDFLAGS = -g -lsqlite3 -L/opt/homebrew/Cellar/openssl@3/3.5.0/lib -lssl -lcrypto

HDRS	= hash.h itool.h registry.h
SRCS	= main.c registry.c hash.c
OBJS 	= $(SRCS:.c=.o)
BIN	= itool

all: $(BIN)

itool: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

main.c: itool.h
registry.c: registry.h

clobber: clean
	-rm -f $(BIN) itool.zip

clean:
	-rm -f $(OBJS)

zip:
	zip itool.zip $(SRCS) $(HDRS) Makefile
