# Image Brightness Controller — simple Makefile build
#
#   make            build into ./bin/brightness_controller
#   make run        build, then start the interactive tool
#   make sample     regenerate the images in ./samples
#   make clean      remove build output and generated files

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -isystem third_party

TARGET   := bin/brightness_controller
SOURCES  := src/main.cpp src/Image.cpp src/BrightnessController.cpp
HEADERS  := src/Image.h src/BrightnessController.h \
            third_party/stb_image.h third_party/stb_image_write.h

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SOURCES)

run: $(TARGET)
	./$(TARGET)

sample: $(TARGET)
	@mkdir -p samples out
	python3 tools/make_samples.py samples
	./$(TARGET) samples/gradient.png -b 1.0 -o samples/gradient.jpg
	./$(TARGET) samples/gradient.ppm -b 1.4 -o samples/gradient_bright.jpg
	@echo "Sample images written to ./samples"

clean:
	rm -rf bin out
	rm -f samples/*_bright.*

.PHONY: all run sample clean
