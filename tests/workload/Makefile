TARGET = remaster_fixture
OBJS = main.o

CFLAGS = -O2 -G0 -Wall -Wextra -std=c99
LIBS = -lpspgum -lpspgu -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = AI Remaster Synthetic Texture Fixture

PSPSDK := $(shell psp-config --pspsdk-path)
ifeq ($(PSPSDK),)
$(error psp-config is required; install PSPSDK and add its bin directory to PATH)
endif
include $(PSPSDK)/lib/build.mak
