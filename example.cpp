#include <math.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#define AUDIO_INBUF_SIZE 20480

#define ERRBUFFLEN 200
char errbuf[ERRBUFFLEN];
#define av_err2str(ret) av_strerror(ret, errbuf, ERRBUFFLEN)
const int samp_rate = 16000;
int count = 0;
// Callback function
int _ffmpeg_interrupt_fcn(void *ptr) {
  int &r = *((int *)ptr);
  // double &r = *((double*)ptr);
  r += 1;
  printf("Interrupted! %d\n", r);
  if (r > 30)
    return 1;
  return 0;
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base,
                       AVStream *st, AVPacket *pkt) {
  /* rescale output packet timestamp values from codec to stream
timebase */
  av_packet_rescale_ts(pkt, *time_base, st->time_base);
  pkt->stream_index = st->index;

  /* Write the compressed frame to the media file. */
#ifdef DEBUG_PACKET
  log_packet(fmt_ctx, pkt);
#endif
  return av_interleaved_write_frame(fmt_ctx, pkt);
}

/*
 * Audio decoding.
 */
static void audio_decode_example(const char *outfilename,
                                 const char *filename) {
  int len;
  FILE *f, *outfile;
  uint8_t inbuf[AUDIO_INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
  AVPacket inpkt, outpkt;

  AVCodec *inCodec = NULL;
  AVCodecContext *inCodecCtx = NULL;
  AVFrame *decoded_frame = NULL;
  AVFormatContext *inFormatCtx = NULL;

  AVCodec *outCodec = NULL;
  AVCodecContext *outCodecCtx = NULL;
  AVFormatContext *outFormatCtx = NULL;
  AVStream *outAudioStream = NULL;

  int ret;

  av_init_packet(&inpkt);

  AVDictionary *d = NULL; // "create" an empty dictionary
  av_dict_set(&d, "protocol_whitelist", "file,udp,rtp", 0); // add an entry

  // Open video file
  ret = avformat_open_input(&inFormatCtx, filename, NULL, &d);
  if (ret < 0) {
    printf_s("Failed: cannot open input.\n");
    av_strerror(ret, errbuf, ERRBUFFLEN);
    fprintf(stderr, "avformat_open_input() fail: %s\n", errbuf);
    exit(1);
  }

  printf_s("Retrieve stream information.\n");
  ret = avformat_find_stream_info(inFormatCtx, NULL);
  if (ret < 0) {
    printf_s("Failed: cannot find stream.\n");
    av_strerror(ret, errbuf, ERRBUFFLEN);
    fprintf(stderr, "avformat_find_stream_info() fail: %s\n", errbuf);
    exit(1);
  }

  av_dump_format(inFormatCtx, 0, filename, 0);

  int stream_idx = -1;

  for (int i = 0; i < inFormatCtx->nb_streams; i++)
    if (inFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
      stream_idx = i;
      break;
    }
  if (stream_idx == -1) {
    fprintf(stderr, "Video stream not found\n");
    exit(1);
  }

  inCodec =
      avcodec_find_decoder(inFormatCtx->streams[stream_idx]->codec->codec_id);
  if (!inCodec) {
    fprintf(stderr, "Codec not found\n");
    exit(1);
  }

  inCodecCtx = avcodec_alloc_context3(inCodec);
  if (!inCodecCtx) {
    fprintf(stderr, "Could not allocate audio codec context\n");
    exit(1);
  }

  inCodecCtx->channels = 1;

  ret = avcodec_open2(inCodecCtx, inCodec, NULL);
  if (ret < 0) {
    fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
    exit(1);
  }

  // Set output
  ret = avformat_alloc_output_context2(&outFormatCtx, NULL, NULL, outfilename);
  if (!outFormatCtx || ret < 0) {
    fprintf(stderr, "Could not allocate output context");
  }
  outFormatCtx->flags |= AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;
  outFormatCtx->audio_codec_id = AV_CODEC_ID_FLAC;

  // Find the codec.
  outCodec = avcodec_find_encoder(outFormatCtx->oformat->audio_codec);
  if (outCodec == NULL) {
    fprintf(stderr, "Codec not found\n");
    return;
  }

  // Add stream to pFormatCtx
  outAudioStream = avformat_new_stream(outFormatCtx, outCodec);
  if (!outAudioStream) {
    fprintf(stderr, "Cannot add new video stream\n");
    return;
  }

  outAudioStream->id = outFormatCtx->nb_streams - 1;
  outCodecCtx = outAudioStream->codec;
  outCodecCtx->sample_rate = samp_rate;
  outCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
  outCodecCtx->channels = 1;
  outCodecCtx->frame_size = 1152;

  if (outFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
    outCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;

  // Open the codec.
  if (avcodec_open2(outCodecCtx, outCodec, NULL) < 0) {
    fprintf(stderr, "Cannot open audio codec\n");
    return;
  }

  if (avio_open2(&outFormatCtx->pb, outfilename, AVIO_FLAG_WRITE, NULL, NULL) <
      0) {
    av_strerror(ret, errbuf, ERRBUFFLEN);
    fprintf(stderr, "avio_open2 fail: %s\n", errbuf);
    return;
  }

  // Write file header.
  ret = avformat_write_header(outFormatCtx, NULL);
  if (ret < 0) {
    fprintf(stderr, "error writing header");
    return;
  }

  // Set callback
  inFormatCtx->interrupt_callback.callback = _ffmpeg_interrupt_fcn;
  inFormatCtx->interrupt_callback.opaque = &count;

  /**************************************************************
   *    Input Code Ctx Frame size: 1152
   *    Output Code Ctx Frame size: 64
   **************************************************************/
  printf("Input Code Ctx Frame size: %d\n", inCodecCtx->frame_size);
  printf("Output Code Ctx Frame size: %d\n", inCodecCtx->frame_size);

  int got_packet = 0;

  while (av_read_frame(inFormatCtx, &inpkt) == 0) {
    int i, ch;
    int got_frame = 0;

    if (!decoded_frame) {
      if (!(decoded_frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
      }
    }
    len = avcodec_decode_audio4(inCodecCtx, decoded_frame, &got_frame, &inpkt);
    if (len < 0) {
      fprintf(stderr, "Error while decoding\n");
      exit(1);
    }
    if (got_frame) {
      printf("Packet size: %d\n", decoded_frame->pkt_size);
      printf("Frame samples: %d\n", decoded_frame->nb_samples);
      got_packet = 0;
      av_init_packet(&outpkt);
      int error = avcodec_encode_audio2(outCodecCtx, &outpkt, decoded_frame,
                                        &got_packet);

      if (!error && got_packet > 0) {
        // Write packet with frame.
        ret = write_frame(outFormatCtx, &outCodecCtx->time_base, outAudioStream,
                          &outpkt);
      }
      av_packet_unref(&outpkt);
    }
    count = 0;
  }

  // Flush remaining encoded data
  while (1) {
    got_packet = 0;
    // Encode frame to packet.
    int error = avcodec_encode_audio2(outCodecCtx, &outpkt, NULL, &got_packet);

    if (!error && got_packet > 0) {
      av_init_packet(&outpkt);
      // Write packet with frame.
      ret = write_frame(outFormatCtx, &outCodecCtx->time_base, outAudioStream,
                        &outpkt);

      av_packet_unref(&outpkt);
    } else {
      break;
    }
  }

  av_write_trailer(outFormatCtx);
  avcodec_close(outAudioStream->codec); // codecCtx
  avio_close(outFormatCtx->pb);
  avformat_free_context(outFormatCtx);

  avcodec_close(inCodecCtx);
  av_free(inCodecCtx);
  av_frame_free(&decoded_frame);
}

int main(int argc, char **argv) {
  const char *output_type;

  av_register_all();
  avformat_network_init(); // for network streaming

  audio_decode_example("test.flac", "test.sdp");

  return 0;
}