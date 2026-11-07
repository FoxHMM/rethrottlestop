# ReThrottleStop 构建
# 用法：make          → 构建到 build/rethrottlestop
#       make install  → 安装到 ~/.local/bin + setcap

CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter \
            -DUV_VERSION='"1.0"' \
            -Iinclude -Isrc/gui
PKGS     := gtk4 gio-2.0
CFLAGS_PKG := $(shell pkg-config --cflags $(PKGS))
LIBS_PKG   := $(shell pkg-config --libs $(PKGS))

SRC := src/gui/main_gui.c \
       src/gui/window.c src/gui/dialogs.c src/gui/dialogs_fivr.c \
       src/gui/dialogs_tpl.c src/gui/dialogs_vf.c src/gui/dialogs_percore.c \
       src/gui/dialogs_cstates.c src/gui/dialogs_options.c \
       src/gui/tray.c src/gui/i18n.c \
       src/msr.cpp src/algorithm.cpp src/controller.cpp src/controller_ts.cpp \
       src/monitor.cpp src/mchbar.cpp src/settings.cpp src/msr_daemon.cpp \
       src/powerprofile.cpp

OUT := build/rethrottlestop

all: $(OUT)

$(OUT): $(SRC) $(wildcard include/*.hpp src/gui/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(CFLAGS_PKG) -o $(OUT) $(SRC) -lpthread $(LIBS_PKG)
	@echo "构建完成: $(OUT)"

# 用户级安装（开发用）：GUI 无特权，所有 MSR 操作走守护进程
install: $(OUT)
	install -Dm755 $(OUT) $(HOME)/.local/bin/rethrottlestop
	@echo "已安装到 ~/.local/bin/rethrottlestop"
	@echo "启用守护进程：systemctl enable --now rethrottlestop-daemon.service"

# 打包用安装（DESTDIR 支持）
PREFIX ?= /usr
install-staging: $(OUT)
	install -Dm755 $(OUT) $(DESTDIR)$(PREFIX)/bin/rethrottlestop
	install -Dm644 packaging/rethrottlestop-daemon.service \
		$(DESTDIR)$(PREFIX)/lib/systemd/system/rethrottlestop-daemon.service
	install -Dm644 packaging/99-rethrottlestop.rules \
		$(DESTDIR)$(PREFIX)/lib/udev/rules.d/99-rethrottlestop.rules
	install -Dm644 packaging/rethrottlestop.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/rethrottlestop.desktop
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/rethrottlestop/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/licenses/rethrottlestop/LICENSE

clean:
	rm -rf build

.PHONY: all install install-staging clean
