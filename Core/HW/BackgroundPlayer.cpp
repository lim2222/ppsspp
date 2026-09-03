#include "Core/HW/BackgroundPlayer.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/AndroidContentURI.h"
#include "Common/StringUtils.h"
#include "android/jni/app-android.h"

#include <algorithm>
#include <set>

#ifdef USE_FFMPEG
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/imgutils.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
}
#endif

BackgroundPlayer *g_BackgroundPlayer = nullptr;

BackgroundPlayer::BackgroundPlayer() {}
BackgroundPlayer::~BackgroundPlayer() { Shutdown(); }

bool BackgroundPlayer::LoadFromBackgroundFolder(const std::string &memstickRoot,
                                                const std::string &whitelistStr) {
#ifndef USE_FFMPEG
    return false;
#else
    Shutdown();
    playlist_.clear();
    playlistIndex_ = 0;
    nextFramePts_ = 0.0;
    lastFramePts_ = 0.0;
    hasPendingFrame_ = false;
    startTime_ = 0.0;

    std::vector<std::string> whitelist;
    SplitString(whitelistStr, ',', whitelist);
    std::set<std::string> whitelistSet;
    for (const auto &s : whitelist) {
        if (!s.empty()) whitelistSet.insert(s);
    }

    std::string folder = memstickRoot;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += "/";
    folder += "PSP/BACKGROUND/";

#ifdef __ANDROID__
    std::string dirUri;
    if (startsWith(memstickRoot, "content://")) {
        AndroidContentURI uri(memstickRoot);
        if (uri.IsTreeURI()) {
            // If the user picked a folder that already is or contains PSP/BACKGROUND,
            // we should try to be smart.
            if (endsWithNoCase(uri.RootPath(), "PSP/BACKGROUND") || endsWithNoCase(uri.RootPath(), "PSP/BACKGROUND/")) {
                dirUri = uri.ToString();
            } else {
                dirUri = uri.WithRootFilePath("PSP/BACKGROUND").ToString();
            }
        } else {
            dirUri = memstickRoot;
        }
    } else {
        if (endsWithNoCase(memstickRoot, "PSP/BACKGROUND") || endsWithNoCase(memstickRoot, "PSP/BACKGROUND/")) {
            dirUri = memstickRoot;
        } else {
            dirUri = folder;
        }
    }
    
    INFO_LOG(Log::System, "BackgroundPlayer: listing directory: %s", dirUri.c_str());

    bool exists = false;
    std::vector<File::FileInfo> fileList;
    if (startsWith(dirUri, "content://")) {
        fileList = Android_ListContentUri(dirUri, "", &exists);
    } else {
        exists = File::Exists(Path(dirUri));
        if (exists) {
            File::GetFilesInDir(Path(dirUri), &fileList);
        }
    }

    if (!exists) {
        INFO_LOG(Log::System, "BackgroundPlayer: directory not found: %s", dirUri.c_str());
        return false;
    }

    const char *exts[] = {".mp4", ".mkv", ".webm", ".mov", ".avi"};
    for (auto &f : fileList) {
        if (f.isDirectory) continue;
        
        std::string lower = f.name;
        for (auto &c : lower) c = tolower(c);
        
        bool isVideo = false;
        for (auto e : exts) {
            if (endsWith(lower, e)) { isVideo = true; break; }
        }
        if (!isVideo) continue;
        
        if (!whitelistSet.empty() && whitelistSet.find(f.name) == whitelistSet.end()) {
            continue;
        }

        // fullName 已经是完整的 content:// URI
        playlist_.push_back(f.fullName.ToString());
    }
#else
    // 非 Android 平台使用传统 opendir
    DIR *dir = opendir(folder.c_str());
    if (!dir) {
        INFO_LOG(Log::System, "BackgroundPlayer: no folder %s", folder.c_str());
        return false;
    }

    const char *exts[] = {".mp4", ".mkv", ".webm", ".mov", ".avi"};
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char *name = entry->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        std::string lower = name;
        for (auto &c : lower) c = tolower(c);

        bool isVideo = false;
        for (auto e : exts) {
            if (endsWith(lower, e)) { isVideo = true; break; }
        }
        if (!isVideo) continue;

        if (!whitelistSet.empty() && whitelistSet.find(name) == whitelistSet.end()) {
            continue;
        }

        File::FileInfo info;
        info.name = name;
        info.fullName = Path(folder + info.name);
        info.isDirectory = (entry->d_type == DT_DIR);
        info.exists = true;
        playlist_.push_back(info.fullName.ToString());
    }
    closedir(dir);
#endif

    std::sort(playlist_.begin(), playlist_.end());

    if (playlist_.empty()) {
        INFO_LOG(Log::System, "BackgroundPlayer: no enabled videos in %s", folder.c_str());
        return false;
    }

    INFO_LOG(Log::System, "BackgroundPlayer: playlist has %d file(s)", (int)playlist_.size());
    return OpenFile(playlist_[0]);
#endif
}

bool BackgroundPlayer::AdvanceToNextFile() {
#ifdef USE_FFMPEG
    if (playlist_.empty()) return false;
    CloseCodecAndFormat();
    playlistIndex_ = (playlistIndex_ + 1) % (int)playlist_.size();
    startTime_ = 0.0;
    nextFramePts_ = 0.0;
    lastFramePts_ = 0.0;
    hasPendingFrame_ = false;
    return OpenFile(playlist_[playlistIndex_]);
#else
    return false;
#endif
}

bool BackgroundPlayer::OpenFile(const std::string &path) {
#ifdef USE_FFMPEG
    Path filePath(path);
    AVFormatContext *fmt = nullptr;
    int ret = -1;

    if (filePath.Type() == PathType::CONTENT_URI) {
        // Android content URI
        ioFile_ = File::OpenCFile(filePath, "rb");
        if (!ioFile_) {
            ERROR_LOG(Log::System, "BackgroundPlayer: failed to open via OpenCFile: %s", path.c_str());
            return false;
        }
        const int bufSize = 32768;
        ioBuffer_ = (uint8_t *)av_malloc(bufSize);
        avioCtx_ = avio_alloc_context(ioBuffer_, bufSize, 0, this,
            &BackgroundPlayer::IOReadPacket, nullptr, &BackgroundPlayer::IOSeek);
        fmt = avformat_alloc_context();
        fmt->pb = avioCtx_;
        ret = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
    } else {
        // 普通本地文件路径
        ret = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    }

    if (ret < 0) {
        char err[128]; av_strerror(ret, err, sizeof(err));
        ERROR_LOG(Log::System, "BackgroundPlayer: open %s failed (%s)", path.c_str(), err);
        if (ioFile_) { fclose(ioFile_); ioFile_ = nullptr; }
        if (avioCtx_) {
            av_free(avioCtx_->buffer);
            av_free(avioCtx_);
            avioCtx_ = nullptr;
        }
        ioBuffer_ = nullptr;
        return false;
    }
    formatCtx_ = fmt;
    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) { CloseCodecAndFormat(); return false; }

    videoStreamIndex_ = av_find_best_stream(formatCtx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex_ < 0) { CloseCodecAndFormat(); return false; }

    AVStream *st = formatCtx_->streams[videoStreamIndex_];
    codecCtx_ = st->codec;
    AVCodec *dec = avcodec_find_decoder(codecCtx_->codec_id);
    if (!dec || avcodec_open2(codecCtx_, dec, nullptr) < 0) { CloseCodecAndFormat(); return false; }

    width_ = codecCtx_->width; height_ = codecCtx_->height;
    videoTimeBase_ = av_q2d(st->time_base);

    frame_ = av_frame_alloc(); frameRGBA_ = av_frame_alloc();
    rgbaBufferSize_ = av_image_get_buffer_size(AV_PIX_FMT_RGBA, width_, height_, 1);
    rgbaBuffer_ = (uint8_t *)av_malloc(rgbaBufferSize_);
    av_image_fill_arrays(frameRGBA_->data, frameRGBA_->linesize, rgbaBuffer_,
                         AV_PIX_FMT_RGBA, width_, height_, 1);

    swsCtx_ = sws_getContext(width_, height_, codecCtx_->pix_fmt,
                              width_, height_, AV_PIX_FMT_RGBA,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    loaded_ = true; hasPendingFrame_ = false; startTime_ = 0.0;
    INFO_LOG(Log::System, "BackgroundPlayer: loaded %s (%dx%d)", path.c_str(), width_, height_);
    return true;
#else
    return false;
#endif
}

bool BackgroundPlayer::DecodeNextFrame() {
#ifdef USE_FFMPEG
    AVPacket pkt; av_init_packet(&pkt); pkt.data = nullptr; pkt.size = 0;
    while (av_read_frame(formatCtx_, &pkt) >= 0) {
        if (pkt.stream_index == videoStreamIndex_) {
            int got = 0;
            avcodec_decode_video2(codecCtx_, frame_, &got, &pkt);
            if (got) {
                sws_scale(swsCtx_, frame_->data, frame_->linesize, 0, height_,
                          frameRGBA_->data, frameRGBA_->linesize);
                int64_t pts = frame_->best_effort_timestamp;
                nextFramePts_ = (pts == AV_NOPTS_VALUE ? 0 : pts * videoTimeBase_);
                av_packet_unref(&pkt);
                return true;
            }
        }
        av_packet_unref(&pkt);
    }
    AdvanceToNextFile();
    return false;
#else
    return false;
#endif
}

bool BackgroundPlayer::Update(double now) {
    if (!loaded_) return false;
    if (startTime_ == 0) startTime_ = now;
    double elapsed = now - startTime_;
    bool updated = false;
    int safety = 0;
    while ((elapsed >= nextFramePts_ || !hasPendingFrame_) && safety++ < 100) {
        if (!DecodeNextFrame()) break;
        hasPendingFrame_ = true; lastFramePts_ = nextFramePts_; updated = true;
        if (lastFramePts_ < 0.1 && elapsed > 1.0) {
            startTime_ = now - lastFramePts_; elapsed = now - startTime_;
        }
        if (elapsed < nextFramePts_) break;
    }
    return updated;
}

const uint8_t *BackgroundPlayer::GetFrameRGBA(int *w, int *h) const {
    if (!loaded_ || !hasPendingFrame_) return nullptr;
    if (w) *w = width_; if (h) *h = height_;
    return rgbaBuffer_;
}

void BackgroundPlayer::CloseCodecAndFormat() {
#ifdef USE_FFMPEG
    if (swsCtx_) sws_freeContext(swsCtx_);
    if (rgbaBuffer_) av_free(rgbaBuffer_);
    if (frameRGBA_) av_frame_free(&frameRGBA_);
    if (frame_) av_frame_free(&frame_);
    if (codecCtx_) { avcodec_close(codecCtx_); codecCtx_ = nullptr; }
    if (formatCtx_) avformat_close_input(&formatCtx_);
    if (avioCtx_) {
        av_free(avioCtx_->buffer);
        av_free(avioCtx_);
        avioCtx_ = nullptr;
    }
    ioBuffer_ = nullptr;
    if (ioFile_) {
        fclose(ioFile_);
        ioFile_ = nullptr;
    }
#endif
    swsCtx_ = nullptr; rgbaBuffer_ = nullptr; frame_ = nullptr; frameRGBA_ = nullptr;
    formatCtx_ = nullptr; videoStreamIndex_ = -1; width_ = height_ = 0;
}

void BackgroundPlayer::Shutdown() {
    CloseCodecAndFormat();
    loaded_ = false; hasPendingFrame_ = false;
    playlist_.clear(); playlistIndex_ = 0;
}

int BackgroundPlayer::IOReadPacket(void *opaque, uint8_t *buf, int bufSize) {
#ifdef USE_FFMPEG
    BackgroundPlayer *self = reinterpret_cast<BackgroundPlayer*>(opaque);
    if (!self->ioFile_) return AVERROR_EOF;
    size_t n = fread(buf, 1, (size_t)bufSize, self->ioFile_);
    return n == 0 ? AVERROR_EOF : (int)n;
#else
    return -1;
#endif
}

int64_t BackgroundPlayer::IOSeek(void *opaque, int64_t offset, int whence) {
#ifdef USE_FFMPEG
    BackgroundPlayer *self = reinterpret_cast<BackgroundPlayer*>(opaque);
    if (!self->ioFile_) return -1;
    if (whence == AVSEEK_SIZE) {
        int64_t cur = ftell(self->ioFile_);
        fseek(self->ioFile_, 0, SEEK_END);
        int64_t size = ftell(self->ioFile_);
        fseek(self->ioFile_, cur, SEEK_SET);
        return size;
    }
    int origin = (whence == SEEK_CUR) ? SEEK_CUR : (whence == SEEK_END) ? SEEK_END : SEEK_SET;
    return fseek(self->ioFile_, (long)offset, origin) == 0 ? ftell(self->ioFile_) : -1;
#else
    return -1;
#endif
}

