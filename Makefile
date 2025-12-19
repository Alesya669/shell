# Компилятор и флаги
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra  -lreadline
READLINE_FLAGS = -lreadline -lhistory
FUSE_FLAGS = -I/usr/include/fuse3 -lfuse3 -L/usr/lib/x86_64-linux-gnu
TARGET = kubsh

# Версия пакета
VERSION = 1.0.0
PACKAGE_NAME = kubsh
BUILD_DIR = build
DEB_DIR = $(BUILD_DIR)/$(PACKAGE_NAME)_$(VERSION)_amd64
DEB_FILE := $(PWD)/kubsh.deb

# Исходные файлы
SRCS = main.cpp vfs.cpp
OBJS = $(SRCS:.cpp=.o)

# Основные цели
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(FUSE_FLAGS) $(READLINE_FLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Запуск шелла
run: $(TARGET)
	./$(TARGET)

# Подготовка структуры для deb-пакета
prepare-deb: $(TARGET)
	@echo "Подготовка структуры для deb-пакета..."
	@mkdir -p $(DEB_DIR)/DEBIAN
	@mkdir -p $(DEB_DIR)/usr/local/bin
	@cp $(TARGET) $(DEB_DIR)/usr/local/bin/
	@chmod +x $(DEB_DIR)/usr/local/bin/$(TARGET)
	
	@echo "Создание control файла..."
	@echo "Package: $(PACKAGE_NAME)" > $(DEB_DIR)/DEBIAN/control
	@echo "Version: $(VERSION)" >> $(DEB_DIR)/DEBIAN/control
	@echo "Section: utils" >> $(DEB_DIR)/DEBIAN/control
	@echo "Priority: optional" >> $(DEB_DIR)/DEBIAN/control
	@echo "Architecture: amd64" >> $(DEB_DIR)/DEBIAN/control
	@echo "Maintainer: Your Name <your.email@example.com>" >> $(DEB_DIR)/DEBIAN/control
	@echo "Depends: fuse3, libfuse3-4, libreadline8" >> $(DEB_DIR)/DEBIAN/control
	@echo "Description: Simple custom shell" >> $(DEB_DIR)/DEBIAN/control
	@echo " A simple custom shell implementation for learning purposes." >> $(DEB_DIR)/DEBIAN/control

# Сборка deb-пакета
deb: prepare-deb
	@echo "Сборка deb-пакета..."
	dpkg-deb --build $(DEB_DIR)
	@mv $(BUILD_DIR)/$(PACKAGE_NAME)_$(VERSION)_amd64.deb $(DEB_FILE)
	@echo "Пакет создан: $(DEB_FILE)"

# Установка пакета (требует sudo)
install: deb
	sudo dpkg -i $(DEB_FILE)

# Удаление пакета
uninstall:
	sudo dpkg -r $(PACKAGE_NAME)

# Тестирование в Docker контейнере
test: deb
	@echo "Запуск теста в Docker контейнере..."
	@-docker run --rm \
		-v $(DEB_FILE):/mnt/kubsh.deb \
		--device /dev/fuse \
		--cap-add SYS_ADMIN \
		--security-opt apparmor:unconfined \
		ghcr.io/xardb/kubshfuse:master 2>/dev/null || true

# Очистка
clean:
	rm -rf $(BUILD_DIR) $(TARGET) *.deb $(OBJS)

# Показать справку
help:
	@echo "Доступные команды:"
	@echo "  make all      - собрать программу"
	@echo "  make deb      - создать deb-пакет"
	@echo "  make install  - установить пакет"
	@echo "  make uninstall - удалить пакет"
	@echo "  make clean    - очистить проект"
	@echo "  make run      - запустить шелл"
	@echo "  make test     - собрать и запустить тест в Docker"
	@echo "  make help     - показать эту справку"

.PHONY: all deb install uninstall clean help prepare-deb run test
