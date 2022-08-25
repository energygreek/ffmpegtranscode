# use pkg-config for getting CFLAGS and LDLIBS
FFMPEG_LIBS=    libavdevice                        \
                libavformat                        \
                libavfilter                        \
                libavcodec                         \
                libswresample                      \
                libswscale                         \
                libavutil                          \
                libpostproc                        \



CFLAGS += -Wall -g
CFLAGS := $(shell pkg-config --cflags $(FFMPEG_LIBS)) $(CFLAGS)
LDLIBS := $(shell pkg-config --libs $(FFMPEG_LIBS)) $(LDLIBS) -lm
LDFLAGS := -Wl,-rpath=/home/husongtao/.local/lib,--disable-new-dtags

EXAMPLES=  transcoding transcode_amr  transcode_amr_swresample

OBJS=$(addsuffix .o,$(EXAMPLES))

.phony: all clean

all: $(OBJS) $(EXAMPLES)

clean:
	$(RM) $(EXAMPLES) $(OBJS)

