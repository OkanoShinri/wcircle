# -------- 設定 --------
CC = gcc
CFLAGS = -O2 -Wall
TARGET = wcircle
SRC = wcircle.c

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
ETCDIR = /etc/wcircle
SYSTEMD_DIR = /etc/systemd/system

SERVICE_FILE = wcircle.service
CONFIG_FILE = config.ini

# -------- ルール --------

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	# バイナリをインストール
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)

	# 設定ディレクトリを作成
	mkdir -p $(ETCDIR)

	# config.ini をインストール（上書き）
	install -m 644 $(CONFIG_FILE) $(ETCDIR)/$(CONFIG_FILE)

	# systemd ユニットファイルをインストール
	install -m 644 $(SERVICE_FILE) $(SYSTEMD_DIR)/$(SERVICE_FILE)

	# systemd に反映
	systemctl daemon-reload
	systemctl enable $(SERVICE_FILE)

uninstall:
	# systemd 停止＆無効化
	-systemctl stop $(SERVICE_FILE)
	-systemctl disable $(SERVICE_FILE)
	-rm -f $(SYSTEMD_DIR)/$(SERVICE_FILE)
	-systemctl daemon-reload

	# バイナリの削除
	-rm -f $(BINDIR)/$(TARGET)

	# 設定ファイルの削除（必要に応じてコメントアウト可能）
	-rm -f $(ETCDIR)/$(CONFIG_FILE)
	-rmdir --ignore-fail-on-non-empty $(ETCDIR)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
