CPPFLAGS =  -D_DEFAULT_SOURCE
CFLAGS   = -std=c99 -pedantic -Wall -Wextra -O0 -g
CC = gcc
LDFLAGS  = -lpthread 
COMPONENTS = connection buffer http queue_impl srv mysock util

all: misha_server
connection.o: connection.c  configuration.h connection.h buffer.h http.h srv.h mysock.h util.h 
buffer.o: buffer.c  configuration.h buffer.h http.h srv.h util.h 
http.o: http.c  configuration.h http.h srv.h util.h 
main.o: main.c configuration.h srv.h mysock.h util.h 
srv.o: srv.c  configuration.h connection.h http.h queue.h srv.h util.h queue_select.c queue_epoll.c 
mysock.o: mysock.c  configuration.h mysock.h util.h 
util.o: util.c  configuration.h util.h 

misha_server:  configuration.h $(COMPONENTS:=.o) $(COMPONENTS:=.h) main.o 
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $(COMPONENTS:=.o) main.o $(LDFLAGS)


clean:
	rm -f misha_server main.o $(COMPONENTS:=.o)