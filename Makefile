# Компилятор и флаги
CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -g
FUSE_FLAGS = -lfuse3 -pthread
TARGET = kubsh

# Версия пакета
VERSION = 1.0.0
PACKAGE_NAME = kubsh
BUILD_DIR = build
DEB_DIR = $(BUILD_DIR)/$(PACKAGE_NAME)_$(VERSION)_amd64

# Исходные файлы
SRC_DIR = src
SOURCES = $(SRC_DIR)/main.cpp $(SRC_DIR)/vfs.cpp
HEADERS = $(SRC_DIR)/vfs.h

# Основные цели
all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(FUSE_FLAGS)

# Запуск шелла
run: $(TARGET)
	./$(TARGET)

# Подготовка структуры для deb-пакета
prepare-deb: $(TARGET)
	@echo "Подготовка структуры для deb-пакета..."
	@mkdir -p $(DEB_DIR)/DEBIAN
	@mkdir -p $(DEB_DIR)/usr/local/bin
	@mkdir -p $(DEB_DIR)/usr/share/man/man1
	@mkdir -p $(DEB_DIR)/usr/share/doc/$(PACKAGE_NAME)
	
	@cp $(TARGET) $(DEB_DIR)/usr/local/bin/
	@chmod +x $(DEB_DIR)/usr/local/bin/$(TARGET)
	
	@echo "Создание man страницы..."
	@echo '.TH KUBSH 1 "$(VERSION)" "Custom Shell"' > $(DEB_DIR)/usr/share/man/man1/$(PACKAGE_NAME).1
	@echo '.SH NAME' >> $(DEB_DIR)/usr/share/man/man1/$(PACKAGE_NAME).1
	@echo 'kubsh \- custom shell implementation' >> $(DEB_DIR)/usr/share/man/man1/$(PACKAGE_NAME).1
	@echo '.SH SYNOPSIS' >> $(DEB_DIR)/usr/share/man/man1/$(PACKAGE_NAME).1
	@echo 'kubsh' >> $(DEB_DIR)/usr/share/man/man1/$(PACKAGE_NAME).1
	@gzip -f $(DEB_DIR)/usr/share/man/man1/$(PACKAGE_NAME).1
	
	@echo "Создание документации..."
	@echo "Kubsh $(VERSION)" > $(DEB_DIR)/usr/share/doc/$(PACKAGE_NAME)/README
	@echo "Custom shell implementation for learning purposes." >> $(DEB_DIR)/usr/share/doc/$(PACKAGE_NAME)/README
	@chmod 644 $(DEB_DIR)/usr/share/doc/$(PACKAGE_NAME)/README
	
	@echo "Создание control файла..."
	@echo "Package: $(PACKAGE_NAME)" > $(DEB_DIR)/DEBIAN/control
	@echo "Version: $(VERSION)" >> $(DEB_DIR)/DEBIAN/control
	@echo "Section: utils" >> $(DEB_DIR)/DEBIAN/control
	@echo "Priority: optional" >> $(DEB_DIR)/DEBIAN/control
	@echo "Architecture: amd64" >> $(DEB_DIR)/DEBIAN/control
	@echo "Maintainer: Kubsh Developer <developer@example.com>" >> $(DEB_DIR)/DEBIAN/control
	@echo "Depends: libc6 (>= 2.31), libfuse3-3" >> $(DEB_DIR)/DEBIAN/control
	@echo "Description: Custom shell implementation" >> $(DEB_DIR)/DEBIAN/control
	@echo " kubsh is a custom shell with VFS support for user management." >> $(DEB_DIR)/DEBIAN/control
	
	@echo "#!/bin/bash" > $(DEB_DIR)/DEBIAN/postinst
	@echo "set -e" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "# Создаем директорию для VFS если не существует" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "if [ ! -d /opt/users ]; then" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "    mkdir -p /opt/users" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "    chmod 755 /opt/users" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "fi" >> $(DEB_DIR)/DEBIAN/postinst
	@echo "echo 'Kubsh $(VERSION) успешно установлен!'" >> $(DEB_DIR)/DEBIAN/postinst
	@chmod 755 $(DEB_DIR)/DEBIAN/postinst

# Сборка deb-пакета
deb: prepare-deb
	@echo "Сборка deb-пакета..."
	dpkg-deb --build $(DEB_DIR)
	@mv $(BUILD_DIR)/$(PACKAGE_NAME)_$(VERSION)_amd64.deb ./
	@echo "Пакет создан: $(PACKAGE_NAME)_$(VERSION)_amd64.deb"
	@echo "Для установки выполните: sudo dpkg -i $(PACKAGE_NAME)_$(VERSION)_amd64.deb"

# Установка пакета (требует sudo)
install: deb
	@sudo dpkg -i $(PACKAGE_NAME)_$(VERSION)_amd64.deb || true
	@echo "Установка завершена. Запустите 'kubsh' для использования."

# Удаление пакета
uninstall:
	@sudo dpkg -r $(PACKAGE_NAME) || true
	@echo "Удаление завершено."

# Тестирование
test: $(TARGET)
	@echo "Тестирование основных команд..."
	@echo "Тест 1: Проверка выхода..."
	@echo "\\q" | ./$(TARGET) || true
	@echo "Тест 2: Проверка echo..."
	@echo "echo Hello World" | ./$(TARGET) | grep -q "Hello World" && echo "✓ test_echo passed" || echo "✗ test_echo failed"
	@echo "Тесты завершены."

# Очистка
clean:
	@echo "Очистка..."
	@rm -rf $(BUILD_DIR) $(TARGET) *.deb 2>/dev/null || true
	@echo "Очистка завершена."

# Показать справку
help:
	@echo "Доступные команды:"
	@echo "  make all      - собрать программу"
	@echo "  make run      - запустить шелл"
	@echo "  make deb      - создать deb-пакет"
	@echo "  make install  - установить пакет"
	@echo "  make uninstall - удалить пакет"
	@echo "  make test     - запустить базовые тесты"
	@echo "  make clean    - очистить проект"
	@echo "  make help     - показать эту справку"

.PHONY: all run deb install uninstall clean help prepare-deb test
