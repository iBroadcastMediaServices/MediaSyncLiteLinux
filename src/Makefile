
PRJDIR = ..
BINDIR = $(PRJDIR)
OBJDIR = $(PRJDIR)/src
INCDIR = $(PRJDIR)/src
BIN_NAME = mediasynclite

CFLAGS = -Wall -Wextra -Wundef -Wshadow -Wpointer-arith -Wbad-function-cast \
         -Wcast-align -Wwrite-strings -Wredundant-decls -Wformat-nonliteral \
         -Wnested-externs -pedantic -std=gnu99 -O3 -pthread \
         -D_REENTERANT `pkg-config --cflags gtk+-3.0 libcurl libssl libcrypto`

LDFLAGS = `pkg-config --libs gtk+-3.0 libcurl libssl libcrypto` -ljansson -lpthread -rdynamic

SRCDIR = $(PRJDIR)/src

APP_OBJS = tools.o ibmsl.o

all:  directories $(BINDIR)/$(BIN_NAME)

directories:
	@echo  $@ " requested"
	if [ ! -d $(BINDIR) ] ; then mkdir $(BINDIR) ; fi;
	@echo  $@  "done"

$(BINDIR)/$(BIN_NAME): $(APP_OBJS)
	@echo  $@ " requested"
	$(CC) $(APP_OBJS) -o $@ $(LDFLAGS)
	@echo  $@  "done"

clean:
	@echo  $@ "requested"
	rm -f $(OBJDIR)/*.o
	rm -f $(BINDIR)/$(BIN_NAME)
	@echo  $@ "done"

%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@
