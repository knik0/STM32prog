CC= gcc
CFLAGS= -O2 -Wall
LDFLAGS=
DEFS=
INCLUDE=
TARGET=
OBJS=
LIBS := $(LIBS)

all: STM32prog

clean:
	rm -f $(OBJS) $(TARGET)

%: %.c
	$(CC) -o $@ $(CFLAGS) $(LDFLAGS) $< $(LIBS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)

%.o: %.s
	$(CC) -x assembler-with-cpp -c $(DEFS) $< -o $@

%.o: %.c
	${CC} -c -o $@ $(DEFS) $(INCLUDE) ${CFLAGS} $<

%.o: %.cpp
	${CC} -c -o $@ $(DEFS) $(INCLUDE) ${CFLAGS} $<

dep:
	makedepend $(DEFS) $(INCLUDE) -o.exe -Y *.c

