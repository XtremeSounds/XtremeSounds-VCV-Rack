RACK_DIR ?= ../Rack-SDK

FLAGS +=
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res README.md LICENSE.txt NOTICE.txt CHANGELOG.md THIRD_PARTY_NOTICES.txt docs

include $(RACK_DIR)/plugin.mk
