# ARM转码

MRFP在X86平台上的第三方软件提供ARM转码功能，但ARM平台上这个厂家可能没有相关软件，需要我们用开源的方法提供解决方案。请在原来ARM平台FFmpeg的视频relay的应用场景上增加下面的功能，然后与MRFP集成：
* G.711流（ALaw或ULaw） --> FFmpeg --> ARM NB 流
* G.711流（ALaw或ULaw） --> FFmpeg --> ARM WB 流

## 编译ffmpeg

ffmpeg本身具备ALaw和ULaw编解码的能力。编译ffmpeg时链接'opencore-amr'和'vo-amrwbenc'，可支持amr的编解码。在arm 64位主机上最小化静态编译ffmpeg的配置命令如下
```
../configure --enable-gpl --enable-version3  --enable-libopencore-amrnb \
--enable-libopencore-amrwb --enable-libvo-amrwbenc --enable-x86asm \
--extra-cflags=-I/home/husongtao/.local/include  \
--extra-ldflags=-L/home/husongtao/.local/lib \
--extra-libs='-lm -lpthread'  --prefix=$HOME/.local
```
**注意**： 需提前静态编译依赖库'opencore-amr'和'vo-amrwbenc'

验证ffmpeg，可见支持amr_nb和amr_wb的编解码
```
ffmpeg -hide_banner  -codecs | grep "alaw\|ulaw\|amr"
 DEAIL. amr_nb               AMR-NB (Adaptive Multi-Rate NarrowBand) (decoders: amrnb libopencore_amrnb ) (encoders: libopencore_amrnb )
 DEAIL. amr_wb               AMR-WB (Adaptive Multi-Rate WideBand) (decoders: amrwb libopencore_amrwb ) (encoders: libvo_amrwbenc )
 DEAIL. pcm_alaw             PCM A-law / G.711 A-law
 DEAIL. pcm_mulaw            PCM mu-law / G.711 mu-law
```

## 测试

### 模拟音频输入

使用rtp协议推流来模拟打电话，输出音频格式为ALaw和ULaw。
```sh
ffmpeg -stream_loop -1 -re -i xiaoqinge.mp3  -ac 1  \
-c:a pcm_alaw -f rtp rtp://localhost:10018 -sdp_file source.sdp

ffmpeg -stream_loop -1 -re -i xiaoqinge.mp3  -ac 1  \
-c:a pcm_mulaw -f rtp rtp://localhost:10018 -sdp_file source.sdp
```

### 使用真实麦克风

如下使用电脑的麦克风录用并推流到Arm服务器
```
ffmpeg -f alsa -i hw:0 -ac 1 -c:a pcm_alaw -f rtp \
rtp://10.9.12.240:10018 -sdp_file source.sdp

ffmpeg -f alsa -i hw:0 -ac 1 -c:a pcm_mulaw -f rtp \
rtp://10.9.12.240:10018 -sdp_file source.sdp
```

### mrfp上转码推流使用ffmpeg的方法

如下命令接收音频流，然后转码再推流到'10.8.10.103:10018'。输出音频编码分别为amr_nb和amr_wb，生成客户端使用的client.sdp文件。
```sh
ffmpeg -protocol_whitelist 'file,udp,rtp' -i source.sdp \
 -c:a amr_nb -ar 8000 -f rtp rtp://10.8.10.103:10018  -sdp_file client.sdp

ffmpeg -protocol_whitelist 'file,udp,rtp' -i source.sdp  \
-c:a amr_wb -ar 16000 -f rtp rtp://10.8.10.103:10018  -sdp_file client.sdp
```

## 验证测试音频输出

最后在客户端使用ffplay打开 client.sdp, 能听到音频。 使用麦克风测试时，主观感觉延迟在1S左右。
```sh
ffplay -protocol_whitelist 'file,udp,rtp' -i client.sdp
```

## 延迟测试

本地录音，分别推送rtp和本地播放
```
ffmpeg -f alsa -channel_layout stereo  -i hw:0  -map_channel 0.0.0  -ar 8000  -c:a pcm_alaw -f rtp rtp://docker57:10018  -map_channel 0.0.1  -c:a pcm_alaw  -f wav pipe:1 | ffplay -fflags nobuffer -flags low_delay  -threads 0 - 
```

服务端转码，推流到本地
```
ffmpeg -protocol_whitelist 'file,udp,rtp'  -i alaw.sdp  -c:a amr_nb  -f rtp rtp://10.8.10.103:10018
```

本地打开流
```
ffplay -fflags nobuffer -flags low_delay  -threads 0  -protocol_whitelist 'udp,rtp,file' -i client.sdp
```

录双路的声音
```
ffmpeg-old -f pulse -i alsa_output.pci-0000_00_1f.3.analog-stereo.monitor record.wav
```