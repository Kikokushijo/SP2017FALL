all: server.c
	gcc server.c -o server
	gcc file_reader.c -o file_reader
clean:
	rm -rf server
	rm -rf file_reader
