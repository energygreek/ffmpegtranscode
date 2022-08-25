/*
 * Copyright (c) 2010 Nicolas George
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2014 Andrey Utkin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * API example for demuxing, decoding, filtering, encoding and muxing
 * @example transcoding.c
 */

#include <unistd.h>

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/dict.h"
#include "libavutil/log.h"
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

static AVFormatContext *ifmt_ctx;
static AVFormatContext *ofmt_ctx;
typedef struct FilteringContext {
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;

  AVPacket *enc_pkt;
  AVFrame *filtered_frame;
} FilteringContext;
static FilteringContext filter_ctx;

typedef struct StreamContext {
  AVCodecContext *dec_ctx;
  AVCodecContext *enc_ctx;
  AVAudioFifo *fifo;

  AVFrame *dec_frame;
} StreamContext;
static StreamContext stream_ctx;

static int open_input_file(const char *filename, AVDictionary **options) {
  int ret;

  ifmt_ctx = NULL;
  if ((ret = avformat_open_input(&ifmt_ctx, filename, NULL, options)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
    return ret;
  }

  av_assert0(ifmt_ctx->nb_streams == 1);

  AVStream *stream = ifmt_ctx->streams[0];
  const AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
  AVCodecContext *codec_ctx;
  if (!dec) {
    av_log(NULL, AV_LOG_ERROR, "Failed to find decoder for stream\n");
    return AVERROR_DECODER_NOT_FOUND;
  }
  codec_ctx = avcodec_alloc_context3(dec);
  if (!codec_ctx) {
    av_log(NULL, AV_LOG_ERROR,
           "Failed to allocate the decoder context for stream\n");
    return AVERROR(ENOMEM);
  }
  ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR,
           "Failed to copy decoder parameters to input decoder context "
           "for stream\n");
    return ret;
  }

  /* Open decoder */
  ret = avcodec_open2(codec_ctx, dec, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream\n");
    return ret;
  }

  stream_ctx.dec_ctx = codec_ctx;

  stream_ctx.dec_frame = av_frame_alloc();
  if (!stream_ctx.dec_frame)
    return AVERROR(ENOMEM);

  av_dump_format(ifmt_ctx, 0, filename, 0);
  return 0;
}

static int open_output_file(const char *filename, const AVCodec *encoder,
                            int sample_rate, int bit_rate,
                            AVDictionary **options) {
  AVStream *out_stream;
  AVCodecContext *dec_ctx, *enc_ctx;
  int ret;

  ofmt_ctx = NULL;
  avformat_alloc_output_context2(&ofmt_ctx, NULL, "rtp", filename);
  if (!ofmt_ctx) {
    av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
    return AVERROR_UNKNOWN;
  }

  out_stream = avformat_new_stream(ofmt_ctx, NULL);
  if (!out_stream) {
    av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
    return AVERROR_UNKNOWN;
  }

  dec_ctx = stream_ctx.dec_ctx;
  enc_ctx = avcodec_alloc_context3(encoder);
  if (!enc_ctx) {
    av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
    return AVERROR(ENOMEM);
  }

  /* In this example, we transcode to same properties (picture size,
   * sample rate etc.). These properties can be changed for output
   * streams easily using filters */
  // enc_ctx->sample_rate = dec_ctx->sample_rate;
  enc_ctx->sample_rate = sample_rate;
  ret = av_channel_layout_copy(&enc_ctx->ch_layout, &dec_ctx->ch_layout);
  if (ret < 0)
    return ret;
  /* take first format from list of supported formats */
  enc_ctx->sample_fmt = encoder->sample_fmts[0];
  enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
  if (bit_rate != 0) {
    enc_ctx->bit_rate = bit_rate;
  }

  if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  /* Third parameter can be used to pass settings to encoder */
  ret = avcodec_open2(enc_ctx, encoder, options);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open encoder\n");
    return ret;
  }

  // manual set frame size for pcm
  if (encoder->id == AV_CODEC_ID_PCM_ALAW ||
      encoder->id == AV_CODEC_ID_PCM_MULAW ||
      encoder->id == AV_CODEC_ID_PCM_S16BE) {
    enc_ctx->frame_size = 160;
  }

  ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR,
           "Failed to copy encoder parameters to output stream\n");
    return ret;
  }

  out_stream->time_base = enc_ctx->time_base;
  stream_ctx.enc_ctx = enc_ctx;

  av_dump_format(ofmt_ctx, 0, filename, 1);

  if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
      return ret;
    }
  }

  /* init muxer, write output file header */
  ret = avformat_write_header(ofmt_ctx, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
    return ret;
  }

  return 0;
}

static int init_filter(FilteringContext *fctx, AVCodecContext *dec_ctx,
                       AVCodecContext *enc_ctx, const char *filter_spec) {
  char args[512];
  int ret = 0;
  const AVFilter *buffersrc = NULL;
  const AVFilter *buffersink = NULL;
  AVFilterContext *buffersrc_ctx = NULL;
  AVFilterContext *buffersink_ctx = NULL;
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterInOut *inputs = avfilter_inout_alloc();
  AVFilterGraph *filter_graph = avfilter_graph_alloc();

  if (!outputs || !inputs || !filter_graph) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  char buf[64];
  buffersrc = avfilter_get_by_name("abuffer");
  buffersink = avfilter_get_by_name("abuffersink");
  if (!buffersrc || !buffersink) {
    av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
    ret = AVERROR_UNKNOWN;
    goto end;
  }

  if (dec_ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
    av_channel_layout_default(&dec_ctx->ch_layout,
                              dec_ctx->ch_layout.nb_channels);
  }
  av_channel_layout_describe(&dec_ctx->ch_layout, buf, sizeof(buf));
  snprintf(args, sizeof(args),
           "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
           dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
           av_get_sample_fmt_name(dec_ctx->sample_fmt), buf);
  ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args,
                                     NULL, filter_graph);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
    goto end;
  }

  ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL,
                                     NULL, filter_graph);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
    goto end;
  }

  ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                       (uint8_t *)&enc_ctx->sample_fmt,
                       sizeof(enc_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
    goto end;
  }

  av_channel_layout_describe(&enc_ctx->ch_layout, buf, sizeof(buf));
  ret = av_opt_set(buffersink_ctx, "ch_layouts", buf, AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
    goto end;
  }

  ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                       (uint8_t *)&enc_ctx->sample_rate,
                       sizeof(enc_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
    goto end;
  }

  /* Endpoints for the filter graph. */
  outputs->name = av_strdup("in");
  outputs->filter_ctx = buffersrc_ctx;
  outputs->pad_idx = 0;
  outputs->next = NULL;

  inputs->name = av_strdup("out");
  inputs->filter_ctx = buffersink_ctx;
  inputs->pad_idx = 0;
  inputs->next = NULL;

  if (!outputs->name || !inputs->name) {
    ret = AVERROR(ENOMEM);
    goto end;
  }

  if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs,
                                      &outputs, NULL)) < 0)
    goto end;

  if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
    goto end;

  /* Fill FilteringContext */
  fctx->buffersrc_ctx = buffersrc_ctx;
  fctx->buffersink_ctx = buffersink_ctx;
  fctx->filter_graph = filter_graph;

end:
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);

  return ret;
}

static int init_filters(void) {
  const char *filter_spec;
  int ret;

  filter_ctx.buffersrc_ctx = NULL;
  filter_ctx.buffersink_ctx = NULL;
  filter_ctx.filter_graph = NULL;

  filter_spec = "anull"; /* passthrough (dummy) filter for audio */
  ret = init_filter(&filter_ctx, stream_ctx.dec_ctx, stream_ctx.enc_ctx,
                    filter_spec);
  if (ret)
    return ret;

  filter_ctx.enc_pkt = av_packet_alloc();
  if (!filter_ctx.enc_pkt)
    return AVERROR(ENOMEM);

  filter_ctx.filtered_frame = av_frame_alloc();
  if (!filter_ctx.filtered_frame)
    return AVERROR(ENOMEM);
  return 0;
}

/**
 * Initialize a FIFO buffer for the audio samples to be encoded.
 * @param[out] fifo                 Sample buffer
 * @param      output_codec_context Codec context of the output file
 * @return Error code (0 if successful)
 */
static int init_fifo(AVAudioFifo **fifo, AVCodecContext *output_codec_context) {
  /* Create the FIFO buffer based on the specified output sample format. */
  if (!(*fifo = av_audio_fifo_alloc(output_codec_context->sample_fmt,
                                    output_codec_context->ch_layout.nb_channels,
                                    1))) {
    fprintf(stderr, "Could not allocate FIFO\n");
    return AVERROR(ENOMEM);
  }
  return 0;
}

/**
 * Initialize one input frame for writing to the output file.
 * The frame will be exactly frame_size samples large.
 * @param[out] frame                Frame to be initialized
 * @param      output_codec_context Codec context of the output file
 * @param      frame_size           Size of the frame
 * @return Error code (0 if successful)
 */
static int init_output_frame(AVFrame **frame,
                             AVCodecContext *output_codec_context,
                             int frame_size) {
  int error;

  /* Create a new frame to store the audio samples. */
  if (!(*frame = av_frame_alloc())) {
    fprintf(stderr, "Could not allocate output frame\n");
    return AVERROR_EXIT;
  }

  /* Set the frame's parameters, especially its size and format.
   * av_frame_get_buffer needs this to allocate memory for the
   * audio samples of the frame.
   * Default channel layouts based on the number of channels
   * are assumed for simplicity. */
  (*frame)->nb_samples = frame_size;
  (*frame)->ch_layout.nb_channels = output_codec_context->ch_layout.nb_channels;
  (*frame)->format = output_codec_context->sample_fmt;
  (*frame)->sample_rate = output_codec_context->sample_rate;

  /* Allocate the samples of the created frame. This call will make
   * sure that the audio frame can hold as many samples as specified. */
  if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
    fprintf(stderr, "Could not allocate output frame samples (error '%s')\n",
            av_err2str(error));
    av_frame_free(frame);
    return error;
  }

  return 0;
}

/**
 * Initialize one data packet for reading or writing.
 * @param[out] packet Packet to be initialized
 * @return Error code (0 if successful)
 */
static int init_packet(AVPacket **packet) {
  if (!(*packet = av_packet_alloc())) {
    fprintf(stderr, "Could not allocate packet\n");
    return AVERROR(ENOMEM);
  }
  return 0;
}

/* Global timestamp for the audio frames. */
static int64_t pts = 0;

/**
 * Encode one frame worth of audio to the output file.
 * @param      frame                 Samples to be encoded
 * @param      output_format_context Format context of the output file
 * @param      output_codec_context  Codec context of the output file
 * @param[out] data_present          Indicates whether data has been
 *                                   encoded
 * @return Error code (0 if successful)
 */
static int encode_audio_frame(AVFrame *frame,
                              AVFormatContext *output_format_context,
                              AVCodecContext *output_codec_context,
                              int *data_present) {
  /* Packet used for temporary storage. */
  AVPacket *output_packet;
  int error;

  error = init_packet(&output_packet);
  if (error < 0)
    return error;

  /* Set a timestamp based on the sample rate for the container. */
  if (frame) {
    frame->pts = pts;
    pts += frame->nb_samples;
  }

  /* Send the audio frame stored in the temporary packet to the encoder.
   * The output audio stream encoder is used to do this. */
  error = avcodec_send_frame(output_codec_context, frame);
  /* The encoder signals that it has nothing more to encode. */
  if (error == AVERROR_EOF) {
    error = 0;
    goto cleanup;
  } else if (error < 0) {
    fprintf(stderr, "Could not send packet for encoding (error '%s')\n",
            av_err2str(error));
    goto cleanup;
  }

  /* Receive one encoded frame from the encoder. */
  error = avcodec_receive_packet(output_codec_context, output_packet);
  /* If the encoder asks for more data to be able to provide an
   * encoded frame, return indicating that no data is present. */
  if (error == AVERROR(EAGAIN)) {
    error = 0;
    goto cleanup;
    /* If the last frame has been encoded, stop encoding. */
  } else if (error == AVERROR_EOF) {
    error = 0;
    goto cleanup;
  } else if (error < 0) {
    fprintf(stderr, "Could not encode frame (error '%s')\n", av_err2str(error));
    goto cleanup;
    /* Default case: Return encoded data. */
  } else {
    *data_present = 1;
  }

  /* Write one audio frame from the temporary packet to the output file. */
  if (*data_present &&
      (error = av_write_frame(output_format_context, output_packet)) < 0) {
    fprintf(stderr, "Could not write frame (error '%s')\n", av_err2str(error));
    goto cleanup;
  }

cleanup:
  av_packet_free(&output_packet);
  return error;
}

/**
 * Load one audio frame from the FIFO buffer, encode and write it to the
 * output file.
 * @param fifo                  Buffer used for temporary storage
 * @param output_format_context Format context of the output file
 * @param output_codec_context  Codec context of the output file
 * @return Error code (0 if successful)
 */
static int load_encode_and_write(AVAudioFifo *fifo,
                                 AVFormatContext *output_format_context,
                                 AVCodecContext *output_codec_context) {
  /* Temporary storage of the output samples of the frame written to the file.
   */
  AVFrame *output_frame;
  /* Use the maximum number of possible samples per frame.
   * If there is less than the maximum possible frame size in the FIFO
   * buffer use this number. Otherwise, use the maximum possible frame size. */
  const int frame_size =
      FFMIN(av_audio_fifo_size(fifo), output_codec_context->frame_size);
  int data_written;

  /* Initialize temporary storage for one output frame. */
  if (init_output_frame(&output_frame, output_codec_context, frame_size))
    return AVERROR_EXIT;

  /* Read as many samples from the FIFO buffer as required to fill the frame.
   * The samples are stored in the frame temporarily. */
  if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) <
      frame_size) {
    fprintf(stderr, "Could not read data from FIFO\n");
    av_frame_free(&output_frame);
    return AVERROR_EXIT;
  }

  /* Encode one frame worth of audio samples. */
  if (encode_audio_frame(output_frame, output_format_context,
                         output_codec_context, &data_written)) {
    av_frame_free(&output_frame);
    return AVERROR_EXIT;
  }
  av_frame_free(&output_frame);
  return 0;
}

/**
 * Add converted input audio samples to the FIFO buffer for later processing.
 * @param fifo                    Buffer to add the samples to
 * @param converted_input_samples Samples to be added. The dimensions are
 * channel (for multi-channel audio), sample.
 * @param frame_size              Number of samples to be converted
 * @return Error code (0 if successful)
 */
static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size) {
  int error;

  /* Make the FIFO as large as it needs to be to hold both,
   * the old and the new samples. */
  if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) +
                                               frame_size)) < 0) {
    fprintf(stderr, "Could not reallocate FIFO\n");
    return error;
  }

  /* Store the new samples in the FIFO buffer. */
  if (av_audio_fifo_write(fifo, (void **)converted_input_samples, frame_size) <
      frame_size) {
    fprintf(stderr, "Could not write data to FIFO\n");
    return AVERROR_EXIT;
  }
  return 0;
}

static int encode_write_frame(int flush) {
  StreamContext *stream = &stream_ctx;
  FilteringContext *filter = &filter_ctx;
  AVFrame *filt_frame = flush ? NULL : filter->filtered_frame;
  AVPacket *enc_pkt = filter->enc_pkt;
  int ret;

  av_log(NULL, AV_LOG_INFO, "Encoding frame\n");
  /* encode filtered frame */
  av_packet_unref(enc_pkt);

  ret = avcodec_send_frame(stream->enc_ctx, filt_frame);

  if (ret < 0)
    return ret;

  while (ret >= 0) {
    ret = avcodec_receive_packet(stream->enc_ctx, enc_pkt);

    if (ret == AVERROR(EAGAIN)) {
      return 0;
    } else if (ret == AVERROR_EOF) {
      return ret;
    }
    /* prepare packet for muxing */
    enc_pkt->stream_index = 0;
    av_packet_rescale_ts(enc_pkt, stream->enc_ctx->time_base,
                         ofmt_ctx->streams[0]->time_base);

    av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
    /* mux encoded frame */
    ret = av_interleaved_write_frame(ofmt_ctx, enc_pkt);
  }

  return ret;
}

static int filter_encode_write_frame(AVFrame *frame) {
  FilteringContext *filter = &filter_ctx;
  int ret;

  av_log(NULL, AV_LOG_INFO, "Pushing decoded frame to filters\n");
  /* push the decoded frame into the filtergraph */
  ret = av_buffersrc_add_frame_flags(filter->buffersrc_ctx, frame, 0);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
    return ret;
  }

  /* pull filtered frames from the filtergraph */
  while (1) {
    av_log(NULL, AV_LOG_INFO, "Pulling filtered frame from filters\n");
    ret =
        av_buffersink_get_frame(filter->buffersink_ctx, filter->filtered_frame);
    if (ret < 0) {
      /* if no more frames for output - returns AVERROR(EAGAIN)
       * if flushed and no more frames for output - returns AVERROR_EOF
       * rewrite retcode to 0 to show it as normal procedure completion
       */
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        ret = 0;
      }
      break;
    }

    filter->filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
    add_samples_to_fifo(stream_ctx.fifo, filter->filtered_frame->data,
                        filter->filtered_frame->nb_samples);
    av_frame_unref(filter->filtered_frame);

    while ((av_audio_fifo_size(stream_ctx.fifo) >=
            stream_ctx.enc_ctx->frame_size)) {
      av_log(NULL, AV_LOG_DEBUG, "Get enough samples to encode one frame\n");
      
      /* If we have enough samples for the encoder, we encode them.
       * At the end of the file, we pass the remaining samples to
       * the encoder. */

      /* Take one frame worth of audio samples from the FIFO buffer,
       * encode it and write it to the output file. */
      if ((ret = load_encode_and_write(stream_ctx.fifo, ofmt_ctx,
                                       stream_ctx.enc_ctx)) < 0) {
        return ret;
      }
    }
    if (ret < 0)
      break;
  }

  return ret;
}

static int flush_encoder() {
  if (!(stream_ctx.enc_ctx->codec->capabilities & AV_CODEC_CAP_DELAY))
    return 0;

  av_log(NULL, AV_LOG_INFO, "Flushing stream encoder\n");
  return encode_write_frame(1);
}

static void usage(void) {
  printf("transcode audio of rtp stream\n");
  printf("usage: transcode_amr SDPFILE CODEC OUTPUT [Other Options]\n");
  printf("\n"
         "Other Options:\n"
         "-h                print this help\n"
         "-r sample rate    set output sample rate\n"
         "-b bit rate       set output bit rate\n"
         "-p payload type   set output payload type of rtp\n"
         "-v verbose        set allow verbose log\n");
}

int main(int argc, char **argv) {
  int ret = AVERROR_EXIT;
  AVPacket *packet = NULL;
  AVDictionary *options = NULL;

  int opt = 0, sample_rate = 0, bit_rate = 0;
  char codec_name[20] = {0};
  int payload_type = -1;
  int verbose = 0;

  while ((opt = getopt(argc, argv, "vhr:b:")) != -1) {
    switch (opt) {
    case 'r':
      sample_rate = atoi(optarg);
      break;
    case 'b':
      bit_rate = atoi(optarg);
      break;
    case 'v':
      verbose = 1;
      break;
    case 'h':
      usage();
      return 0;
    default: /* '?' */
      fprintf(stderr, "unknown argument %c\n", opt);
      usage();
      exit(EXIT_FAILURE);
    }
  }

  if (argc - optind != 4) {
    fprintf(stderr, "missing required argument\n");
    usage();
    exit(EXIT_FAILURE);
  }

  // name madentory args
  const char *input_url_arg = argv[optind];
  const char *output_codec_arg = argv[optind + 1];
  const char *payload_type_arg = argv[optind + 2];
  const char *output_url_arg = argv[optind + 3];

  if (!strcmp(output_codec_arg, "amr_nb")) {
    snprintf(codec_name, 20, "libopencore_amrnb");
  } else if (!strcmp(output_codec_arg, "amr_wb")) {
    snprintf(codec_name, 20, "libvo_amrwbenc");
  } else {
    snprintf(codec_name, 20, "%s", output_codec_arg);
  }

  payload_type = atoi(payload_type_arg);

  /* find the encoder */
  const AVCodec *codec = avcodec_find_encoder_by_name(codec_name);
  if (!codec) {
    fprintf(stderr,
            "Codec '%s' not found, choose from "
            "<pcm_alaw|pcm_mulaw|amr_nb|amr_wb>\n",
            output_codec_arg);
    exit(1);
  }

  if (verbose) {
    av_log_set_level(AV_LOG_DEBUG);
  } else {
    av_log_set_level(AV_LOG_WARNING);
  }

  av_dict_set(&options, "protocol_whitelist", "udp,rtp,file", 0);
  av_dict_set_int(&options, "payload_type", payload_type, 0);

  if ((ret = open_input_file(input_url_arg, &options)) < 0)
    goto end;
  if ((ret = open_output_file(output_url_arg, codec, sample_rate, bit_rate,
                              &options)) < 0)
    goto end;
  if ((ret = init_filters()) < 0)
    goto end;
  if (!(packet = av_packet_alloc()))
    goto end;

  /* Initialize the FIFO buffer to store audio samples to be encoded. */
  if (init_fifo(&stream_ctx.fifo, stream_ctx.enc_ctx))
    goto end;

  /* read all packets */
  while (1) {
    if ((ret = av_read_frame(ifmt_ctx, packet)) < 0)
      break;
    av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame\n");

    if (filter_ctx.filter_graph) {
      StreamContext *stream = &stream_ctx;

      av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");

      av_packet_rescale_ts(packet, ifmt_ctx->streams[0]->time_base,
                           stream->dec_ctx->time_base);
      ret = avcodec_send_packet(stream->dec_ctx, packet);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
        break;
      }

      while (ret >= 0) {
        ret = avcodec_receive_frame(stream->dec_ctx, stream->dec_frame);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
          break;
        else if (ret < 0)
          goto end;

        stream->dec_frame->pts = stream->dec_frame->best_effort_timestamp;
        ret = filter_encode_write_frame(stream->dec_frame);
        if (ret == AVERROR_EOF) {
          break;
        }
        if (ret < 0)
          goto end;
      }
    } else {
      /* remux this frame without reencoding */
      av_packet_rescale_ts(packet, ifmt_ctx->streams[0]->time_base,
                           ofmt_ctx->streams[0]->time_base);

      ret = av_interleaved_write_frame(ofmt_ctx, packet);
      if (ret < 0)
        goto end;
    }
    av_packet_unref(packet);
  }

  /* flush filters and encoders */
  /* flush filter */
  if (!filter_ctx.filter_graph)
    goto end;
  filter_encode_write_frame(NULL);
  /* flush encoder */
  ret = flush_encoder();
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
    goto end;
  }

  av_write_trailer(ofmt_ctx);
end:
  av_packet_free(&packet);
  avcodec_free_context(&stream_ctx.dec_ctx);
  if (ofmt_ctx && ofmt_ctx->streams[0] && stream_ctx.enc_ctx)
    avcodec_free_context(&stream_ctx.enc_ctx);
  if (filter_ctx.filter_graph) {
    avfilter_graph_free(&filter_ctx.filter_graph);
    av_packet_free(&filter_ctx.enc_pkt);
    av_frame_free(&filter_ctx.filtered_frame);
  }

  av_frame_free(&stream_ctx.dec_frame);
  avformat_close_input(&ifmt_ctx);
  if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&ofmt_ctx->pb);
  avformat_free_context(ofmt_ctx);

  if (ret < 0)
    av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

  return ret ? 1 : 0;
}
