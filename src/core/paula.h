#pragma once

#include <QByteArray>

#include <cstdint>
#include <vector>

// Paula, the Amiga sound chip, as far as the music players use it: the four
// DMA channels (AUDxLC, AUDxLEN, AUDxPER, AUDxVOL, the audio bits of DMACON).
// When DMA starts a channel it plays LEN words from LC at 3546895 / PER Hz
// (PAL), then restarts from the latched LC/LEN, which is how the players loop
// samples. Channels 0 and 3 go left, 1 and 2 right.
class PaulaMixer
{
public:
    explicit PaulaMixer(int sampleRate = 44100);
    void write(uint32_t reg, uint32_t value);
    // Mixes the given number of stereo frames, reading the samples from
    // memory (chip RAM), and appends them as signed 16-bit little-endian PCM.
    // gain scales the output (1 = full scale for two channels at volume 64).
    void mix(const std::vector<uint8_t> &memory, int frames, QByteArray &pcm, double gain = 1.0);
    bool silent() const;

private:
    struct Channel
    {
        uint32_t lc = 0;
        uint16_t len = 0;
        uint16_t per = 0;
        uint16_t vol = 0;
        bool on = false;
        uint32_t ptr = 0;
        uint32_t left = 0;
        double phase = 0;
        int8_t cur = 0;
    };
    void start(Channel &c);
    void setDmacon(uint16_t v);

    Channel m_ch[4];
    uint16_t m_dmacon = 0x03F0;   // AmigaOS runs with DMA enabled (DMAEN) and the display on
    int m_rate;
};
