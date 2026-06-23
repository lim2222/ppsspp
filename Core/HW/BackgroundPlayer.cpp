#include "Core/HW/BackgroundPlayer.h"
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/DirListing.h"
#include "Common/StringUtils.h"

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
                                                const std::string &whitelist) {
#ifndef USE_FFMPEG
    return false;
#else
    Shutdown();
    playlist_.clear();
    playlistIndex_ = 0;

    std::string folder = memstickRoot;
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += "/";
    folder += "PSP/BACKGROUND/";

    if (!File::Exists(Path(folder))) {
        INFO_LOG(Log::System, "BackgroundPlayer: no folder %s", folder.c_str());
        return false;
    }

    // Parse whitelist into a set of enabled filenames (lowercase).
    // Empty whitelist means all files are enabled.
    std::set<std::string> enabledFiles;
    bool hasWhitelist = !whitelist.empty();
    if (hasWhitelist) {
        std::vector<std::string> parts;
        SplitString(whitelist, ',', parts);
        for (auto &p : parts) {
            std::string lower = p;
            for (auto &c : lower) c = tolower(c);
            enabledFiles.insert(lower);
        }
    }

    const char *exts[] = {".mp4", ".mkv", ".webm", ".mov", ".avi"};
    std::vector<File::FileInfo> files;
    File::GetFilesInDir(Path(folder), &files);

    // Sort by filename for consistent ordering.
    std::sort(files.begin(), files.end(), [](const File::FileInfo &a, const File::FileInfo &b) {
        return a.name < b.name;
    });

    for (auto &f : files) {
        if (f.isDirectory) continue;
        std::string lower = f.name;
        for (auto &c : lower) c = tolower(c);

        bool isVideo = false;
        for (auto e : exts) {
            if (endsWith(lower, e)) { isVideo = true; break; }
        }
        if (!isVideo) continue;

        // Filter by whitelist if one exists.
        if (hasWhitelist && enabledFiles.find(lower) == enabledFiles.end()) continue;

        playlist_.push_back(f.fullName.ToString());
    }

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
    AVFormatContext *fmt = nullptr;
    int ret = avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char err[128]; av_strerror(ret, err, sizeof(err));
        ERROR_LOG(Log::System, "BackgroundPlayer: open %s failed (%s)", path.c_str(), err);
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
    // End of current file — advance to next in playlist.
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
#endif
    swsCtx_ = nullptr; rgbaBuffer_ = nullptr; frame_ = nullptr; frameRGBA_ = nullptr;
    formatCtx_ = nullptr; videoStreamIndex_ = -1; width_ = height_ = 0;
}

void BackgroundPlayer::Shutdown() {
    CloseCodecAndFormat();
    loaded_ = false; hasPendingFrame_ = false;
    playlist_.clear(); playlistIndex_ = 0;
}