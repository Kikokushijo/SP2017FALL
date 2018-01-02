all: server.c
	gcc server.c -o server
	gcc file_reader.c -o file_reader
	gcc file_reader2.c -o file_reader2
clean:
	rm -rf server
	rm -rf file_reader
