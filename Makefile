CC      = gcc
FILES   = WebServer.c
OUT_EXE = http_server
LDFLAGS = -pthread -lz
build:
	$(CC) $(FILES) -o $(OUT_EXE) $(LDFLAGS)

clean:
	rm $(OUT_EXE) 

rebuild: clean build
