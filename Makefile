# Image Brightness Controller — simple Makefile build
#
#   make             build ./bin/brightness_controller (CLI)
#                    and ./bin/brightness_server (web UI on $PORT)
#   make run         build, then start the interactive CLI
#   make serve       build, then start the web server on $PORT (default 10000)
#   make sample      regenerate the images in ./samples
#   make clean       remove build output and generated files

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -isystem third_party
LDLIBS   += -pthread

TARGET   := bin/brightness_controller
SERVER   := bin/brightness_server
OBJECTS  := bin/Image.o bin/BrightnessController.o
HEADERS  := src/Image.h src/BrightnessController.h \
            third_party/stb_image.h third_party/stb_image_write.h
HTTPLIB  := third_party/httplib.h

all: $(TARGET) $(SERVER)

bin/%.o: src/%.cpp $(HEADERS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -pthread -c -o $@ $<

$(TARGET): src/main.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -pthread -o $@ src/main.cpp $(OBJECTS) $(LDLIBS)

$(SERVER): src/server.cpp $(OBJECTS) $(HTTPLIB)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -pthread -o $@ src/server.cpp $(OBJECTS) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

serve: $(SERVER)
	./$(SERVER)

sample: $(TARGET)
	@mkdir -p samples out
	python3 tools/make_samples.py samples
	./$(TARGET) samples/gradient.png -b 1.0 -o samples/gradient.jpg
	./$(TARGET) samples/gradient.ppm -b 1.4 -o samples/gradient_bright.jpg
	@echo "Sample images written to ./samples"

clean:
	rm -rf bin out
	rm -f samples/*_bright.*

.PHONY: all run serve sample clean
