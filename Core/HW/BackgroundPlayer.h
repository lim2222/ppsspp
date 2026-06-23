#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

class BackgroundPlayer {
public:
    BackgroundPlayer();
    ~BackgroundPlayer();

    // Scans folder, filters by whitelist (empty whitelist = all files enabled).
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

    // Playlist
    std::vector<std::string> playlist_;
    int playlistIndex_ = 0;

    AVFormatContext *formatCtx_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    AVFrame *frame_ = nullptr;
    AVFrame *frameRGBA_ = nullptr;
    SwsContext *swsCtx_ = nullptr;
    uint8_t *rgbaBuffer_ = nullptr;
    int rgbaBufferSize_ = 0;

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