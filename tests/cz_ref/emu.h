// SPDX-License-Identifier: GPL-3.0-only
// The least of MAME's emu.h that upd933.cpp needs to build on its own (tests/cz_ref.cpp). Nothing here makes a
// sound: the chip's own code does, unmodified. The device framework (streams, timers, save states, callbacks) is
// inert; the test drives the chip by its registers (write) and its per-sample update().
#ifndef CZ_REF_EMU_H
#define CZ_REF_EMU_H
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numbers>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int16_t s16;
typedef int32_t s32;

template <typename T> constexpr T BIT(T x, int n) { return (x >> n) & T(1); }
template <typename T> constexpr T BIT(T x, int n, int w) { return (x >> n) & T((1u << w) - 1u); }

struct machine_config {};
typedef int device_type;
struct attotime { static const attotime never; };
inline const attotime attotime::never{};
struct emu_timer { void adjust(attotime) {} };
struct sound_stream {
    void update() {}
    void set_sample_rate(u32) {}
    int samples() const { return 0; }
    void put_int_clamp(int, int, s32, s32) {}
};
struct running_machine {
    bool side_effects_disabled() const { return false; }
    u32 rand() { return (u32)std::rand(); }
    const char *describe_context() const { return "cz_ref"; }
};

class device_t {
public:
    device_t(const machine_config &, device_type, const char *, device_t *, u32 clock) : m_clock(clock) {}
    virtual ~device_t() {}
    u32 clock() const { return m_clock; }
    running_machine &machine() { return m_machine; }
    template <typename F, typename P> emu_timer *timer_alloc(F, P) { return &m_timer; }
    attotime clocks_to_attotime(u64) const { return attotime{}; }
    template <typename... T> void save_item(T &&...) {}
    void logerror(const char *, ...) {}
protected:
    virtual void device_start() {}
    virtual void device_reset() {}
    virtual void device_clock_changed() {}
private:
    u32 m_clock;
    running_machine m_machine;
    emu_timer m_timer;
};

class device_sound_interface {
public:
    device_sound_interface(const machine_config &, device_t &) {}
    virtual ~device_sound_interface() {}
    sound_stream *stream_alloc(int, int, u32) { return &m_stream; }
protected:
    virtual void sound_stream_update(sound_stream &) {}
private:
    sound_stream m_stream;
};

struct devcb_write_line {
    explicit devcb_write_line(device_t &) {}
    void operator()(int) {}
    int bind() { return 0; }
};

#define DECLARE_DEVICE_TYPE(type, cls) extern device_type type;
#define DEFINE_DEVICE_TYPE(type, cls, shortname, fullname) device_type type = 0;
#define TIMER_CALLBACK_MEMBER(name) void name(s32 param)
#define FUNC(x) 0
#define NAME(x) x
#define STRUCT_MEMBER(a, b) a
#define ATTR_COLD
#endif
