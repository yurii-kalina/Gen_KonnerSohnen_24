#pragma once
// Parser for the KS 24VS-DC inverter -> panel-display serial frame.
//
// Bus facts (logic-analyzer capture + two full runs, 2026-09-08): 2400 baud
// 8N1, non-inverted, line idles LOW, ~4 ms HIGH preamble, then 33 bytes
// back-to-back (141 ms) once per second, ~860 ms of silence in between:
//   AA 55 18 <29 bytes payload> 55 AA
// Field map confirmed against the panel displays (0 / 28.6 / 28.4 A, 26.6 V):
//   bytes 6-7  big-endian, 0.1 V  -> bus voltage   (26.5 V idle, 27.2 V loaded)
//   bytes 8-9  big-endian, 0.1 A  -> charge current (0.0 A, 28.7 A)
//   bytes 10-11 big-endian        -> RPM: 0 with engine off, ~2850 idle, ~3660 loaded
//   byte 13    4, 5, 6 over one evening; steady within a run (slow counter?)
//   byte 18    0x20 engine running (commanded), 0x10 stopped -> status bits
//   byte 29    22..27, drifts slowly with run time (temperature?)
//   byte 30    checksum, formula NOT found (not sum/XOR/CRC-8/common CRC-16)
// The sender keeps transmitting with the engine off (powered from the bank).
//
// INTEGRITY: because the checksum is unknown, "Accepted" means the envelope,
// the constant bytes and the value ranges all look right. A bit flip that
// lands inside a value field passes unnoticed. Consumers that ACT on this data
// (start/stop logic) must require two consecutive frames to agree on a state
// change and should median-filter values. This header only frames.
//
// Feed bytes one at a time with push(); reset() when the line has been silent
// for longer than a byte gap (frames never contain gaps, so a pause mid-frame
// means the rest of that frame is lost).
#include <stdint.h>
#include <string.h>

namespace ks24
{
    constexpr uint8_t FRAME_LEN = 33;
    constexpr uint8_t HDR0 = 0xAA, HDR1 = 0x55, TYPE = 0x18;
    constexpr uint8_t TRL0 = 0x55, TRL1 = 0xAA;
    constexpr uint8_t STATUS_RUN = 0x20, STATUS_STOP = 0x10;

    // Bytes constant in every frame seen so far (three sessions, 0..28.7 A,
    // engine off/cranking/idle/loaded). A poor man's checksum: a frame whose
    // "constant" bytes differ is far more likely corrupted than a new state.
    // Rejected frames are reported, never silently dropped, so a genuinely
    // new state shows up as a rejection and this table gets updated.
    // 0xFFFF = varies, not checked.
    constexpr uint16_t TEMPLATE[FRAME_LEN] = {
        0xAA, 0x55, 0x18, 0x00, 0x01, 0x01,      // 0-5 header + constants
        0xFFFF, 0xFFFF,                          // 6-7  voltage
        0xFFFF, 0xFFFF,                          // 8-9  current
        0xFFFF, 0xFFFF,                          // 10-11 rpm
        0x00, 0xFFFF,                            // 12 const, 13 varies
        0x1B, 0x3D, 0x30, 0x05,                  // 14-17
        0xFFFF,                                  // 18 status
        0x01, 0x00, 0x01, 0x19, 0x00, 0x00, 0x14, 0x2F, 0xD3, 0x64, // 19-28
        0xFFFF,                                  // 29 temperature?
        0xFFFF,                                  // 30 checksum
        0x55, 0xAA};                             // 31-32 trailer

    struct Frame
    {
        uint8_t raw[FRAME_LEN];
        uint16_t be16(uint8_t i) const { return (uint16_t)raw[i] << 8 | raw[i + 1]; }
        float voltage() const { return be16(6) / 10.0f; }   // V
        float current() const { return be16(8) / 10.0f; }   // A (unsigned as far as seen)
        uint16_t rpm() const { return be16(10); }
        uint8_t status() const { return raw[18]; }
        bool running() const { return raw[18] == STATUS_RUN; }
        // Only 0x20 and 0x10 have been seen. Anything else is accepted (so it
        // is never hidden) but must be treated as "not confirmed running".
        bool statusKnown() const { return raw[18] == STATUS_RUN || raw[18] == STATUS_STOP; }
        uint8_t b13() const { return raw[13]; }
        uint8_t temp_guess() const { return raw[29]; }      // unverified
        uint8_t checksum() const { return raw[30]; }
    };

    enum class Result : uint8_t
    {
        Incomplete,       // need more bytes
        Accepted,         // envelope + template + ranges OK; `out` valid (NOT checksum-verified)
        RejectedTemplate, // envelope OK, a constant byte differs; `out` holds the frame
        RejectedRange,    // envelope + template OK, a value is implausible; `out` holds the frame
        BadEnvelope       // 33 bytes after AA 55 did not end 55 AA / type byte wrong
    };

    class Parser
    {
    public:
        // Feed one byte. `out` is written only when the result is Accepted or
        // one of the Rejected values, and is valid until the next push().
        Result push(uint8_t b, Frame &out)
        {
            if (n_ == 0)
            {
                if (b == HDR0) buf_[n_++] = b;
                else dropped_++;
                return Result::Incomplete;
            }
            if (n_ == 1)
            {
                if (b == HDR1) { buf_[n_++] = b; return Result::Incomplete; }
                n_ = 0;
                dropped_++;             // the AA we were holding
                if (b == HDR0) buf_[n_++] = b;
                else dropped_++;        // and this byte
                return Result::Incomplete;
            }
            buf_[n_++] = b;
            if (n_ < FRAME_LEN) return Result::Incomplete;
            n_ = 0;
            if (buf_[2] == TYPE && buf_[FRAME_LEN - 2] == TRL0 && buf_[FRAME_LEN - 1] == TRL1)
            {
                memcpy(out.raw, buf_, FRAME_LEN);
                if (!matchesTemplate(out)) { rejectedTemplate_++; return Result::RejectedTemplate; }
                if (!plausible(out)) { rejectedRange_++; return Result::RejectedRange; }
                good_++;
                return Result::Accepted;
            }
            // Envelope failed: the AA 55 we locked onto was not a frame start,
            // or a byte was lost. Re-feed the bytes after that header so a
            // genuine AA 55 inside the buffer is not skipped. Depth is bounded
            // at 2: the re-feed supplies 31 bytes, fewer than a frame.
            bad_++;
            dropped_++;   // the AA that started this false candidate
            uint8_t saved[FRAME_LEN];
            memcpy(saved, buf_, FRAME_LEN);
            Result best = Result::BadEnvelope;
            for (uint8_t i = 1; i < FRAME_LEN; i++)
            {
                Result r = push(saved[i], out);
                if (r != Result::Incomplete) best = r;
            }
            return best;
        }

        // Call when the line has been silent longer than one byte time while a
        // frame is pending: whatever was collected belongs to a frame whose
        // tail was lost. Otherwise the next frame's AA would be consumed as
        // the missing trailer byte and that whole next frame lost.
        void reset()
        {
            if (n_ == 0) return;
            dropped_ += n_;
            truncated_++;
            n_ = 0;
        }
        bool pending() const { return n_ != 0; }

        static bool plausible(const Frame &f)
        {
            return f.voltage() >= 10.0f && f.voltage() <= 40.0f &&
                   f.current() <= 80.0f &&
                   f.rpm() <= 4500 &&
                   f.b13() <= 31;   // status byte deliberately NOT range-checked: see statusKnown()
        }
        static bool matchesTemplate(const Frame &f)
        {
            for (uint8_t i = 0; i < FRAME_LEN; i++)
                if (TEMPLATE[i] != 0xFFFF && f.raw[i] != (uint8_t)TEMPLATE[i]) return false;
            return true;
        }

        uint32_t good() const { return good_; }
        uint32_t rejected() const { return rejectedTemplate_ + rejectedRange_; }
        uint32_t rejectedTemplate() const { return rejectedTemplate_; }
        uint32_t rejectedRange() const { return rejectedRange_; }
        uint32_t bad() const { return bad_; }
        uint32_t dropped() const { return dropped_; }
        uint32_t truncated() const { return truncated_; }

    private:
        uint8_t buf_[FRAME_LEN];
        uint8_t n_ = 0;
        uint32_t good_ = 0, rejectedTemplate_ = 0, rejectedRange_ = 0, bad_ = 0, dropped_ = 0, truncated_ = 0;
    };
}
