CC = gcc
TARGET = wcircle.bin
SRC = wcircle/wcircle.c

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
ETCDIR = /etc/wcircle
SYSTEMD_DIR = /etc/systemd/system

SERVICE_FILE = wcircle.service
CONFIG_FILE = config.ini


all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(SRC) inih/ini.c -o $(TARGET) $(pkg-config --cflags --libs libevdev) -lm

install: $(TARGET)
	mkdir -p $(BINDIR)/$(TARGET)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	mkdir -p $(ETCDIR)
	install -m 644 $(CONFIG_FILE) $(ETCDIR)/$(CONFIG_FILE)
	install -m 644 $(SERVICE_FILE) $(SYSTEMD_DIR)/$(SERVICE_FILE)

	systemctl daemon-reload
	systemctl enable $(SERVICE_FILE)
	systemctl start $(SERVICE_FILE)
	
uninstall:
	-systemctl stop $(SERVICE_FILE)
	-systemctl disable $(SERVICE_FILE)
	-rm -f $(SYSTEMD_DIR)/$(SERVICE_FILE)
	-systemctl daemon-reload

	-rm -f $(BINDIR)/$(TARGET)
	-rm -f $(ETCDIR)/$(CONFIG_FILE)
	-rmdir --ignore-fail-on-non-empty $(ETCDIR)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
