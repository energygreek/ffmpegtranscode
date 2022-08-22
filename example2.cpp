// For QT pro file
// INCLUDEPATH += /usr/include/ffmpeg
// QMAKE_CXXFLAGS += -D__STDC_CONSTANT_MACROS
// LIBS += -L/usr/local/lib -lz
// LIBS += -lm -lpthread -lavcodec -lavdevice -lavfilter -lavformat -lavresample -lavutil -lpostproc -lswresample -lswscale

#include <QDebug>
#include <QFile>

extern "C" {

#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
#include <libavformat/avio.h>
#include <libavutil/avassert.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avstring.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

class AudioEncoderSettings {
public:
    static const QString DEF_OUTPUT_FILE;
    static const quint16 DEF_CHANNELS;
    static const quint32 DEF_SAMPLE_RATE;
    static const quint32 DEF_BIT_RATE;
    static const QString DEF_AUDIO_CODEC;

public:
    AudioEncoderSettings(void) = default;
    AudioEncoderSettings& operator=(const AudioEncoderSettings& other) = default;
    AudioEncoderSettings& operator=(AudioEncoderSettings&& other) = default;
    AudioEncoderSettings(const AudioEncoderSettings& other) = default;
    AudioEncoderSettings(AudioEncoderSettings&& other) = default;
    ~AudioEncoderSettings(void) = default;

    bool operator==(const AudioEncoderSettings& other) const;
    bool operator!=(const AudioEncoderSettings& other) const;

    quint32 sampleRate(void) const noexcept;
    quint16 channelCount(void) const noexcept;
    QString audioCodec(void) const noexcept;
    quint32 constBitRate(void) const noexcept;
    QString outputFile(void) const noexcept;

    void setSampleRate(const quint32& val) noexcept;
    void setChannelCount(const quint16& val) noexcept;
    void setAudioCodec(const QString& val) noexcept;
    void setConstBitRate(const quint32& val) noexcept;
    void setOutputFile(const QString& val) noexcept;

private:
    quint32 m_sampleRate{ DEF_SAMPLE_RATE };
    quint16 m_channelCount{ DEF_CHANNELS };
    QString m_audioCodec{ DEF_AUDIO_CODEC };
    quint32 m_constBitRate{ DEF_BIT_RATE };
    QString m_outputFile{ DEF_AUDIO_CODEC };
};

using Settings = AudioEncoderSettings;

const quint32 AudioEncoderSettings::DEF_SAMPLE_RATE = 0x1f40;
const quint16 AudioEncoderSettings::DEF_CHANNELS = 0x0001;
const QString AudioEncoderSettings::DEF_OUTPUT_FILE = QString();
const quint32 AudioEncoderSettings::DEF_BIT_RATE = 0xfa00;
const QString AudioEncoderSettings::DEF_AUDIO_CODEC = "alaw";

quint32 AudioEncoderSettings::sampleRate(void) const noexcept
{
    return m_sampleRate;
}

quint16 AudioEncoderSettings::channelCount(void) const noexcept
{
    return m_channelCount;
}

QString AudioEncoderSettings::audioCodec(void) const noexcept
{
    return m_audioCodec;
}

quint32 AudioEncoderSettings::constBitRate(void) const noexcept
{
    return m_constBitRate;
}

QString AudioEncoderSettings::outputFile(void) const noexcept
{
    return m_outputFile;
}

void AudioEncoderSettings::setSampleRate(const quint32& val) noexcept
{
    m_sampleRate = val;
}

void AudioEncoderSettings::setChannelCount(const quint16& val) noexcept
{
    m_channelCount = val;
}

void AudioEncoderSettings::setAudioCodec(const QString& val) noexcept
{
    m_audioCodec = val;
}

void AudioEncoderSettings::setConstBitRate(const quint32& val) noexcept
{
    m_constBitRate = val;
}

void AudioEncoderSettings::setOutputFile(const QString& val) noexcept
{
    m_outputFile = val;
}

bool AudioEncoderSettings::operator==(const AudioEncoderSettings& other) const
{
    return (m_sampleRate == other.m_sampleRate && m_channelCount == other.m_channelCount && m_audioCodec == other.m_audioCodec && m_constBitRate == other.m_constBitRate && m_outputFile == other.m_outputFile);
}

bool AudioEncoderSettings::operator!=(const AudioEncoderSettings& other) const
{
    return (m_sampleRate != other.m_sampleRate && m_channelCount != other.m_channelCount && m_audioCodec != other.m_audioCodec && m_constBitRate != other.m_constBitRate && m_outputFile != other.m_outputFile);
}

using AudioStr = AVStream;
using AudioCtx = AVIOContext;
using AudioDic = AVDictionary;
using AudioCdc = AVCodecContext;
using AudioFrm = AVFormatContext;

class AudioEncoder {
public:
    AudioEncoder(const Settings& settings);
    AudioEncoder& operator=(const AudioEncoder& rhs) = delete;
    AudioEncoder& operator=(AudioEncoder&& rhs) = delete;
    AudioEncoder(const AudioEncoder& rhs) = delete;
    AudioEncoder(AudioEncoder&& rhs) = delete;
    ~AudioEncoder(void) = default;

    bool init(void) noexcept;
    bool record(const QByteArray& rawData) noexcept;
    bool term(void) noexcept;

private:
    QString getMessageByErrorCode(const qint32& code) noexcept;
    bool proc(void) noexcept;

private:
    class Deleter {
    public:
        static void cleanup(AudioFrm* p);
        static void cleanup(AudioCdc* p);
        static void cleanup(AudioCtx* p);
        static void cleanup(AudioStr* p);
        static void cleanup(Settings* p);
        static void cleanup(AudioDic* p);
    };

    QScopedPointer<Settings, Deleter> p_sets{ nullptr };
    QScopedPointer<AudioStr, Deleter> p_iStr{ nullptr };
    QScopedPointer<AudioStr, Deleter> p_oStr{ nullptr };
    QScopedPointer<AudioCtx, Deleter> p_inIOCtx{ nullptr };
    QScopedPointer<AudioFrm, Deleter> p_iFrmCtx{ nullptr };
    QScopedPointer<AudioFrm, Deleter> p_oFrmCtx{ nullptr };

public:
    qsizetype m_curSize{};
    const uint8_t* p_curData{};
};

QString AudioEncoder::getMessageByErrorCode(const qint32& code) noexcept
{
    if (code != 0) {
        char errorBuffer[255]{ '0' };
        av_strerror(code, errorBuffer, sizeof(errorBuffer));
        return QString(errorBuffer);
    }
    return QString();
}

qint32 readPacket(void* opaque, quint8* buf, qint32 sz)
{
    AudioEncoder* self = static_cast<AudioEncoder*>(opaque);
    if (self->p_curData && self->m_curSize) {
        sz = std::min(sz, (int)self->m_curSize);
        memcpy(buf, self->p_curData, sz);
        self->m_curSize -= sz;
        self->p_curData += sz;
        return sz;
    }
    else {
        return AVERROR(EAGAIN);
    }
}

AudioEncoder::AudioEncoder(const Settings& settings)
    : p_sets(nullptr)
    , p_iStr(nullptr)
    , p_oStr(nullptr)
    , p_inIOCtx(nullptr)
    , p_iFrmCtx(nullptr)
    , p_oFrmCtx(nullptr)
{
    p_sets.reset(new Settings(settings));
}

void AudioEncoder::Deleter::cleanup(AudioFrm* p)
{
    if (p != nullptr)
        avformat_close_input(&p);
}

void AudioEncoder::Deleter::cleanup(AudioCdc* p)
{
    if (p != nullptr)
        avcodec_free_context(&p);
}

void AudioEncoder::Deleter::cleanup(AudioCtx* p)
{
    if (p != nullptr)
        av_freep(&p->buffer);
    avio_context_free(&p);
}

void AudioEncoder::Deleter::cleanup(AudioStr* p)
{
    if (p != nullptr)
        p = nullptr;
}

void AudioEncoder::Deleter::cleanup(Settings* p)
{
    if (p != nullptr)
        delete p;
}

void AudioEncoder::Deleter::cleanup(AudioDic* p)
{
    if (p != nullptr)
        av_dict_free(&p);
}

bool AudioEncoder::init(void) noexcept
{
    if (p_oFrmCtx) {
        return true;
    }
    av_register_all();
    avcodec_register_all();

    AVInputFormat* file_iformat = av_find_input_format(p_sets->audioCodec().toStdString().c_str());
    if (file_iformat == nullptr) {
        qDebug() << QString("Unknown input format: '%1'").arg(p_sets->audioCodec());
        return false;
    }

    AudioDic* format_opts = nullptr;
    const qint32 sampleRateErrorCode = av_dict_set(&format_opts, "sample_rate",
        QString::number(p_sets->sampleRate()).toStdString().c_str(), 0);
    const qint32 bitRateErrorCode = av_dict_set(&format_opts, "bit_rate",
        QString::number(p_sets->constBitRate()).toStdString().c_str(), 0);
    qint32 channelErrorCode = 0;

    // because we set audio_channels based on both the "ac" and
    // "channel_layout" options, we need to check that the specified
    // demuxer actually has the "channels" option before setting it
    if (file_iformat && file_iformat->priv_class && av_opt_find(&file_iformat->priv_class, "channels", NULL, 0, AV_OPT_SEARCH_FAKE_OBJ)) {
        channelErrorCode = av_dict_set(&format_opts, "channels",
            QString::number(p_sets->channelCount()).toStdString().c_str(), 0);
    }

    if ((bitRateErrorCode < 0) || (sampleRateErrorCode < 0) || (channelErrorCode < 0)) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        return false;
    }

    AVFormatContext* ic;
    /* get default parameters from command line */
    ic = avformat_alloc_context();
    if (!ic) {
        qDebug() << "Error: " << __LINE__;
        return false;
    }

    const qint32 iBufSize = 4096;
    quint8* iCtxBuffer = static_cast<quint8*>(av_malloc(iBufSize));
    if (!iCtxBuffer) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        return false;
    }

    p_inIOCtx.reset(avio_alloc_context(
        iCtxBuffer, iBufSize, 0, this, &readPacket, nullptr, nullptr));
    if (!p_inIOCtx) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        return false;
    }

    ic->pb = p_inIOCtx.get();
    int errorCode = 0;
    if ((errorCode = avformat_open_input(&ic,
             p_sets->outputFile().toStdString().c_str(), file_iformat, &format_opts))
        < 0) {
        ic = nullptr;
        qDebug() << QString("Could not open output file: %1 (error: %2)").arg(p_sets->outputFile()).arg(getMessageByErrorCode(errorCode));
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        return false;
    }
    p_iFrmCtx.reset(ic);
    if (p_iFrmCtx->nb_streams != 1) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        return false;
    }

    p_iStr.reset(p_iFrmCtx->streams[0]);
    AVCodec* iCdc = avcodec_find_decoder(p_iStr->codecpar->codec_id);
    if (!iCdc) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        return false;
    }
    qDebug() << "Decoder found: " << iCdc->name;

    AudioCdc* iCdcCtx = avcodec_alloc_context3(iCdc);
    if (!iCdcCtx) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        return false;
    }

    avcodec_parameters_to_context(iCdcCtx, p_iStr->codecpar);
    if (avcodec_open2(iCdcCtx, iCdc, &format_opts) < 0) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        return false;
    }

    int ret = avcodec_parameters_from_context(p_iStr->codecpar, iCdcCtx);
    if (ret < 0) {
        qDebug() << "Error initializing the decoder context";
        return false;
    }

    // Open output file ........
    AVDictionary* opts = nullptr;
    av_dict_copy(&opts, format_opts, 0);

    AudioFrm* f = nullptr;
    if (avformat_alloc_output_context2(
            &f,
            nullptr,
            nullptr,
            p_sets->outputFile().toStdString().c_str())
        < 0) {

        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        avcodec_free_context(&iCdcCtx);
        return false;
    }

    p_oFrmCtx.reset(f);
    if (!(p_oFrmCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&p_oFrmCtx->pb,
                p_sets->outputFile().toStdString().c_str(), AVIO_FLAG_WRITE)
            < 0) {
            if (format_opts != nullptr)
                av_dict_free(&format_opts);
            av_free(iCtxBuffer);
            avcodec_free_context(&iCdcCtx);
            return false;
        }
    }

    p_oStr.reset(avformat_new_stream(p_oFrmCtx.get(), NULL));
    if (!p_oStr) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        avcodec_free_context(&iCdcCtx);
        return false;
    }

    if (avcodec_parameters_copy(p_oStr->codecpar, p_iStr->codecpar) < 0) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);
        avcodec_free_context(&iCdcCtx);
        return false;
    }

    p_oStr->codecpar->codec_tag = 0;
    av_dict_free(&format_opts);
    if (avformat_write_header(p_oFrmCtx.get(), 0) < 0) {
        if (format_opts != nullptr)
            av_dict_free(&format_opts);
        av_free(iCtxBuffer);

        avcodec_free_context(&iCdcCtx);
        return false;
    }
    avcodec_free_context(&iCdcCtx);
    return true;
}

bool AudioEncoder::proc(void) noexcept
{
    AVPacket pkt{};
    while (true) {
        const qint32 rc = av_read_frame(p_iFrmCtx.get(), &pkt);
        if (rc < 0) {
            return false;
        }
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            break;
        }
        if (pkt.stream_index == p_iStr->index) {

            pkt.pts = av_rescale_q_rnd(pkt.pts, p_iStr->time_base, p_oStr->time_base,
                static_cast<enum AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, p_iStr->time_base, p_oStr->time_base,
                static_cast<enum AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

            pkt.duration = av_rescale_q(pkt.duration, p_iStr->time_base, p_oStr->time_base);
            pkt.pos = -1;
            if (av_interleaved_write_frame(
                    p_oFrmCtx.get(), &pkt)
                < 0) {
                av_packet_unref(&pkt);
                return false;
            }
            av_packet_unref(&pkt);
        }
    }
    return m_curSize == 0;
}

bool AudioEncoder::record(const QByteArray& rawData) noexcept
{
    if (p_oFrmCtx) {
        if (!rawData.isEmpty()) {
            if (p_inIOCtx->error == AVERROR(EAGAIN)) {
                p_inIOCtx->eof_reached = 0;
            }
            p_curData = reinterpret_cast<const uint8_t*>(rawData.data());
            m_curSize = rawData.size();
            return proc();
        }
    }
    return false;
}

bool AudioEncoder::term(void) noexcept
{
    if (p_oFrmCtx) {
        proc();
        qint32 error = 0;
        if ((error = av_write_trailer(p_oFrmCtx.get())) < 0) {
            qDebug() << QString("Could not write output file "
                                "trailer (error '%1')")
                            .arg(getMessageByErrorCode(error));
            return false;
        }
        p_iStr.reset();
        p_oStr.reset();
        p_inIOCtx.reset();
        p_iFrmCtx.reset();
        p_oFrmCtx.reset();
        return true;
    }
    return false;
}

int main()
{
    AudioEncoderSettings settings;
    settings.setAudioCodec("alaw");
    settings.setOutputFile("/home/test/result.mka");
    settings.setSampleRate(8000);
    settings.setChannelCount(1);
    settings.setConstBitRate(64000);

    AudioEncoder encoder(settings);
    if (encoder.init()) {
        QFile file("/home/test/rawAlawRtpPayloadData.bin");
        file.open(QIODevice::ReadOnly);
        QByteArray arr(file.readAll());
        if (encoder.record(arr)) {
            return encoder.term();
        }
    }
    return encoder.term();
}