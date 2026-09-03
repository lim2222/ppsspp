#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;
struct AVIOContext;

class BackgroundPlayer {
public:
    BackgroundPlayer();
    ~BackgroundPlayer();

    bool LoadFromBackgroundFolder(const std::string &memstickRoot,
                                  const std::string &whitelist);
    bool Update(double nowSeconds);
    const uint8_t *GetFrameRGBA(int *width, int *height) const;
    bool IsLoaded() const { return loaded_; }
    void Shutdown();

private:
    bool OpenFile(const std::string &path);
    bool DecodeNextFrame();
    void CloseCodecAndFormat();
    bool AdvanceToNextFile();

    static int IOReadPacket(void *opaque, uint8_t *buf, int bufSize);
    static int64_t IOSeek(void *opaque, int64_t offset, int whence);

    std::vector<std::string> playlist_;
    int playlistIndex_ = 0;

    FILE *ioFile_ = nullptr;
    AVIOContext *avioCtx_ = nullptr;
    uint8_t *rgbaBuffer_ = nullptr;
    int rgbaBufferSize_ = 0;

    AVFormatContext *formatCtx_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    AVFrame *frame_ = nullptr;
    AVFrame *frameRGBA_ = nullptr;
    SwsContext *swsCtx_ = nullptr;
    uint8_t *ioBuffer_ = nullptr;

    int videoStreamIndex_ = -1;
    int width_ = 0, height_ = 0;
    double videoTimeBase_ = 0.0;
    double startTime_ = 0.0;
    double lastFramePts_ = 0.0;
    double nextFramePts_ = 0.0;

    bool loaded_ = false;
    bool hasPendingFrame_ = false;
};

extern BackgroundPlayer *g_BackgroundPlayer;