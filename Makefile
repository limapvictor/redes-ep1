TARGET=mqtt-server
CXX=gcc
LD=gcc
CFLAGS=-Wall
OBJS=mac0352-servidor-exemplo-ep1.c

mqtt-server:$(OBJS)
	$(LD) $(CFLAGS) -o $(TARGET) $(OBJS)
install:
	@install nomedoprograma /usr/local/bin/nomedoprograma

clean:
	rm -f $(TARGET)