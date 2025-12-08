CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)

# Server objects
FTPSERVER_OBJS = ftpd.o ftp_common.o
FTPSERVER_UI_OBJS = ftpd_ui.o ftpd.o ftp_common.o

# Client objects
FTPCLIENT_OBJS = ftp_client.o ftp_common.o
FTPCLIENT_UI_OBJS = ftp_client_ui.o ftp_client.o ftp_common.o

# Default target
all: ftpd_ui ftp_client_ui

# FTP Server with UI
ftpd_ui: $(FTPSERVER_UI_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(GTK_LIBS) -pthread

ftpd_ui.o: ftpd_ui.c ftp_common.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $<

ftpd.o: ftpd.c ftp_common.h
	$(CC) $(CFLAGS) -c $<

# FTP Client with UI
ftp_client_ui: $(FTPCLIENT_UI_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(GTK_LIBS) -pthread

ftp_client_ui.o: ftp_client_ui.c ftp_client.h ftp_common.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $<

ftp_client.o: ftp_client.c ftp_client.h ftp_common.h
	$(CC) $(CFLAGS) -c $<

# Common objects
ftp_common.o: ftp_common.c ftp_common.h
	$(CC) $(CFLAGS) -c $<

# Clean build artifacts
clean:
	rm -f *.o ftpd_ui ftp_client_ui

# Rebuild everything
rebuild: clean all

.PHONY: all clean rebuild

