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

#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

static AVFormatContext *g_ifmt_ctx;
static AVFormatContext *g_ofmt_ctx;

typedef struct StreamContext {
  AVCodecContext *dec_ctx;
  AVCodecContext *enc_ctx;

  AVFrame *dec_frame;
} StreamContext;
static StreamContext *g_stream_ctx;

static int open_input_file(const char *filename, AVDictionary **options) {

  int ret;
  unsigned int i;
  g_ifmt_ctx = NULL;
  if ((ret = avformat_open_input(&g_ifmt_ctx, filename, NULL, options)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
    return ret;
  }

  if ((ret = avformat_find_stream_info(g_ifmt_ctx, NULL)) < 0) {
    av_log(NULL, AV_LOG_ERROR, "Cannot find stream information\n");
    return ret;
  }

  g_stream_ctx = av_calloc(g_ifmt_ctx->nb_streams, sizeof(*g_stream_ctx));
  if (!g_stream_ctx)
    return AVERROR(ENOMEM);

  for (i = 0; i < g_ifmt_ctx->nb_streams; i++) {
    AVStream *stream = g_ifmt_ctx->streams[i];
    const AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *codec_ctx;
    if (!dec) {
      av_log(NULL, AV_LOG_ERROR, "Failed to find decoder for stream #%u\n", i);
      return AVERROR_DECODER_NOT_FOUND;
    }
    codec_ctx = avcodec_alloc_context3(dec);
    if (!codec_ctx) {
      av_log(NULL, AV_LOG_ERROR,
             "Failed to allocate the decoder context for stream #%u\n", i);
      return AVERROR(ENOMEM);
    }
    ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR,
             "Failed to copy decoder parameters to input decoder context "
             "for stream #%u\n",
             i);
      return ret;
    }
    /* Reencode video & audio and remux subtitles etc. */
    if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
        codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {

      if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
        codec_ctx->framerate = av_guess_frame_rate(g_ifmt_ctx, stream, NULL);
      /* Open decoder */
      ret = avcodec_open2(codec_ctx, dec, NULL);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open decoder for stream #%u\n",
               i);
        return ret;
      }
    }

    // 获取layout, 否则默认为0, 导致amr编码报错
    if (!codec_ctx->channel_layout) {
      char layout_name[256];

      codec_ctx->channel_layout =
          av_get_default_channel_layout(codec_ctx->channels);
      if (codec_ctx->channel_layout) {
        av_get_channel_layout_string(layout_name, sizeof(layout_name),
                                     codec_ctx->channels,
                                     codec_ctx->channel_layout);
        av_log(NULL, AV_LOG_WARNING,
               "Guessed Channel Layout for Input Stream "
               "#%d : %s\n",
               i, layout_name);
      }
    }

    g_stream_ctx[i].dec_ctx = codec_ctx;

    g_stream_ctx[i].dec_frame = av_frame_alloc();
    if (!g_stream_ctx[i].dec_frame)
      return AVERROR(ENOMEM);
  }

  av_dump_format(g_ifmt_ctx, 0, filename, 0);
  return 0;
}

static int open_output_file(const char *filename) {
  AVStream *out_stream;
  AVStream *in_stream;
  AVCodecContext *dec_ctx, *enc_ctx;
  const AVCodec *encoder;
  int ret;
  unsigned int i;

  g_ofmt_ctx = NULL;
  avformat_alloc_output_context2(&g_ofmt_ctx, NULL, "rtp", filename);
  if (!g_ofmt_ctx) {
    av_log(NULL, AV_LOG_ERROR, "Could not create output context\n");
    return AVERROR_UNKNOWN;
  }

  for (i = 0; i < g_ifmt_ctx->nb_streams; i++) {
    out_stream = avformat_new_stream(g_ofmt_ctx, NULL);
    if (!out_stream) {
      av_log(NULL, AV_LOG_ERROR, "Failed allocating output stream\n");
      return AVERROR_UNKNOWN;
    }

    in_stream = g_ifmt_ctx->streams[i];
    dec_ctx = g_stream_ctx[i].dec_ctx;

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO ||
        dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
      /* in this example, we choose transcoding to same codec */
      encoder = avcodec_find_encoder(AV_CODEC_ID_AMR_NB);
    //   encoder = avcodec_find_encoder(in_stream->codecpar->codec_id);
      if (!encoder) {
        av_log(NULL, AV_LOG_FATAL, "Necessary encoder not found\n");
        return AVERROR_INVALIDDATA;
      }
      enc_ctx = avcodec_alloc_context3(encoder);
      if (!enc_ctx) {
        av_log(NULL, AV_LOG_FATAL, "Failed to allocate the encoder context\n");
        return AVERROR(ENOMEM);
      }

      /* In this example, we transcode to same properties (picture size,
       * sample rate etc.). These properties can be changed for output
       * streams easily using filters */
      if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        enc_ctx->height = dec_ctx->height;
        enc_ctx->width = dec_ctx->width;
        enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
        /* take first format from list of supported formats */
        if (encoder->pix_fmts)
          enc_ctx->pix_fmt = encoder->pix_fmts[0];
        else
          enc_ctx->pix_fmt = dec_ctx->pix_fmt;
        /* video time_base can be set to whatever is handy and supported by
         * encoder */
        enc_ctx->time_base = av_inv_q(dec_ctx->framerate);
      } else {
        enc_ctx->sample_rate = dec_ctx->sample_rate;
        enc_ctx->channel_layout = dec_ctx->channel_layout;
        enc_ctx->channels =
            av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
        /* take first format from list of supported formats */
        enc_ctx->sample_fmt = encoder->sample_fmts[0];
        
        // bit rate
        enc_ctx->bit_rate = dec_ctx->bit_rate;
        
        enc_ctx->time_base = (AVRational){1, enc_ctx->sample_rate};
      }

      if (g_ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

      // 加速
      // g_ofmt_ctx->flags |= AVFMT_FLAG_NOBUFFER | AVFMT_FLAG_FLUSH_PACKETS;

      /* Third parameter can be used to pass settings to encoder */
      ret = avcodec_open2(enc_ctx, encoder, NULL);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open video encoder for stream #%u\n",
               i);
        return ret;
      }
      ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Failed to copy encoder parameters to output stream #%u\n", i);
        return ret;
      }
      /* Set the sample rate for the container. */
      out_stream->time_base.den = dec_ctx->sample_rate;
      out_stream->time_base.num = 1;
      out_stream->time_base = enc_ctx->time_base;
      g_stream_ctx[i].enc_ctx = enc_ctx;
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
      av_log(NULL, AV_LOG_FATAL,
             "Elementary stream #%d is of unknown type, cannot proceed\n", i);
      return AVERROR_INVALIDDATA;
    } else {
      /* if this stream must be remuxed */
      ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
      if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Copying parameters for stream #%u failed\n",
               i);
        return ret;
      }
      out_stream->time_base = in_stream->time_base;
    }
  }
  av_dump_format(g_ofmt_ctx, 0, filename, 1);

  if (!(g_ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&g_ofmt_ctx->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
      return ret;
    }
  }

  /* init muxer, write output file header */
  ret = avformat_write_header(g_ofmt_ctx, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
    return ret;
  }

  return 0;
}

static int64_t pts = 0;
static int encode_write_frame(unsigned int stream_index, AVFrame *frame) {
  StreamContext *stream = &g_stream_ctx[stream_index];
  int ret;
  AVPacket *enc_pkt = av_packet_alloc();

  av_log(NULL, AV_LOG_INFO, "Encoding frame\n");

  if (frame) {
    frame->pts = pts;
    pts += frame->nb_samples;
  }
  /* Send the audio frame stored in the temporary packet to the encoder.
   * The output audio stream encoder is used to do this. */
  ret = avcodec_send_frame(stream->enc_ctx, frame);

  if (ret < 0)
    return ret;

  ret = avcodec_receive_packet(stream->enc_ctx, enc_pkt);

  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    goto cleanup;
  else if (ret < 0) {
    fprintf(stderr, "Could not encode frame (error '%s')\n", av_err2str(ret));
    goto cleanup;
  }
  /* prepare packet for muxing */
  enc_pkt->stream_index = stream_index;

  av_log(NULL, AV_LOG_DEBUG, "Muxing frame\n");
  /* mux encoded frame */
  //   ret = av_interleaved_write_frame(g_ofmt_ctx, enc_pkt);
  ret = av_write_frame(g_ofmt_ctx, enc_pkt);

cleanup:
  av_packet_free(&enc_pkt);
  return ret;
}

static int flush_encoder(unsigned int stream_index) {
  if (!(g_stream_ctx[stream_index].enc_ctx->codec->capabilities &
        AV_CODEC_CAP_DELAY))
    return 0;

  av_log(NULL, AV_LOG_INFO, "Flushing stream #%u encoder\n", stream_index);
  return encode_write_frame(stream_index, NULL);
}

int main(int argc, char **argv) {
  int ret;
  AVPacket *packet = NULL;
  unsigned int stream_index;
  unsigned int i;

  if (argc != 3) {
    av_log(NULL, AV_LOG_ERROR, "Usage: %s <input file> <output file>\n",
           argv[0]);
    return 1;
  }

  AVDictionary *options = NULL;
  av_dict_set(&options, "protocol_whitelist", "udp,rtp,file", 0);

  if ((ret = open_input_file(argv[1], &options)) < 0)
    goto end;
  if ((ret = open_output_file(argv[2])) < 0)
    goto end;
  if (!(packet = av_packet_alloc()))
    goto end;

  /* read all packets */
  while (1) {
    if ((ret = av_read_frame(g_ifmt_ctx, packet)) < 0)
      break;
    stream_index = packet->stream_index;
    av_log(NULL, AV_LOG_DEBUG, "Demuxer gave frame of stream_index %u\n",
           stream_index);

    StreamContext *stream = &g_stream_ctx[stream_index];
    av_log(NULL, AV_LOG_DEBUG, "Going to reencode&filter the frame\n");
    ret = avcodec_send_packet(stream->dec_ctx, packet);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
      break;
    }

    ret = avcodec_receive_frame(stream->dec_ctx, stream->dec_frame);
    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
      break;
    else if (ret < 0)
      goto end;

    stream->dec_frame->pts = stream->dec_frame->best_effort_timestamp;
    ret = encode_write_frame(stream_index, stream->dec_frame);
    if (ret < 0)
      goto end;
    av_packet_free(&packet);
  }

  /* flush filters and encoders */
  for (i = 0; i < g_ifmt_ctx->nb_streams; i++) {
    /* flush encoder */
    ret = flush_encoder(i);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Flushing encoder failed\n");
      goto end;
    }
  }

  av_write_trailer(g_ofmt_ctx);
end:
  av_packet_free(&packet);
  for (i = 0; i < g_ifmt_ctx->nb_streams; i++) {
    avcodec_free_context(&g_stream_ctx[i].dec_ctx);
    if (g_ofmt_ctx && g_ofmt_ctx->nb_streams > i && g_ofmt_ctx->streams[i] &&
        g_stream_ctx[i].enc_ctx)
      avcodec_free_context(&g_stream_ctx[i].enc_ctx);
    av_frame_free(&g_stream_ctx[i].dec_frame);
  }
  av_free(g_stream_ctx);
  avformat_close_input(&g_ifmt_ctx);
  if (g_ofmt_ctx && !(g_ofmt_ctx->oformat->flags & AVFMT_NOFILE))
    avio_closep(&g_ofmt_ctx->pb);
  avformat_free_context(g_ofmt_ctx);

  if (ret < 0)
    av_log(NULL, AV_LOG_ERROR, "Error occurred: %s\n", av_err2str(ret));

  return ret ? 1 : 0;
}
