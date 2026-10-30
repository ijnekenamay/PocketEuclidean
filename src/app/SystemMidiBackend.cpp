#include "app/SystemMidiBackend.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace pocket::app {
namespace {

#ifdef _WIN32

std::uint64_t nowMicros() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

#endif // _WIN32

class NullMidiBackend final : public platform::IMidiBackend {
public:
    bool open() noexcept override { return false; }
    void close() noexcept override {}
    bool poll(platform::MidiEvent&) noexcept override { return false; }
    bool send(const platform::MidiEvent&) noexcept override { return false; }
};

#ifdef _WIN32

constexpr std::size_t EventCapacity = 256;

class WinMmMidiBackend final : public platform::IMidiBackend {
public:
    explicit WinMmMidiBackend(std::uint8_t deviceIndex) noexcept
        : deviceIndex_(deviceIndex) {}

    ~WinMmMidiBackend() override { close(); }

    bool open() noexcept override {
        close();
        bool opened = false;
        lock();
        if (deviceIndex_ < midiInGetNumDevs()) {
            MIDIINCAPSW caps{};
            if (midiInGetDevCapsW(deviceIndex_, &caps, sizeof(caps)) ==
                MMSYSERR_NOERROR) {
                copyName(caps.szPname);
            }
            const auto result = midiInOpen(
                &input_, deviceIndex_,
                reinterpret_cast<DWORD_PTR>(&WinMmMidiBackend::callback),
                reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
            if (result == MMSYSERR_NOERROR) {
                if (midiInStart(input_) == MMSYSERR_NOERROR) {
                    opened = true;
                } else {
                    midiInClose(input_);
                    input_ = nullptr;
                }
            } else {
                input_ = nullptr;
            }
        }
        // Output is independent from input: a machine may expose only one of
        // the two. Clock output must still work on an output-only port.
        if (deviceIndex_ < midiOutGetNumDevs()) {
            if (midiOutOpen(&output_, deviceIndex_, 0, 0, CALLBACK_NULL) ==
                MMSYSERR_NOERROR) {
                opened = true;
                if (name_[0] == '\0') {
                    MIDIOUTCAPSW caps{};
                    if (midiOutGetDevCapsW(deviceIndex_, &caps,
                                           sizeof(caps)) ==
                        MMSYSERR_NOERROR) {
                        copyName(caps.szPname);
                    }
                }
            } else {
                output_ = nullptr;
            }
        }
        unlock();
        return opened;
    }

    void close() noexcept override {
        lock();
        if (input_ != nullptr) {
            midiInStop(input_);
            midiInReset(input_);
            midiInClose(input_);
            input_ = nullptr;
        }
        if (output_ != nullptr) {
            midiOutReset(output_);
            midiOutClose(output_);
            output_ = nullptr;
        }
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
        name_[0] = '\0';
        unlock();
    }

    bool poll(platform::MidiEvent& event) noexcept override {
        const auto read = readIndex_.load(std::memory_order_relaxed);
        const auto write = writeIndex_.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }
        event = events_[read % EventCapacity];
        readIndex_.store(read + 1, std::memory_order_release);
        return true;
    }

    bool send(const platform::MidiEvent& event) noexcept override {
        // The audio thread may emit clock and notes while the UI thread
        // toggles MIDI. A try-lock keeps the audio path non-blocking; a close
        // in progress drops the event instead of racing the handle.
        if (!tryLock()) {
            return false;
        }
        bool sent = false;
        if (output_ != nullptr) {
            DWORD message = 0;
            switch (event.type) {
            case platform::MidiEvent::Type::Clock:
                message = 0xF8;
                break;
            case platform::MidiEvent::Type::Start:
                message = 0xFA;
                break;
            case platform::MidiEvent::Type::Stop:
                message = 0xFC;
                break;
            case platform::MidiEvent::Type::Continue:
                message = 0xFB;
                break;
            case platform::MidiEvent::Type::NoteOn:
                message = 0x90U | ((event.channel - 1) & 0x0FU);
                break;
            case platform::MidiEvent::Type::NoteOff:
                message = 0x80U | ((event.channel - 1) & 0x0FU);
                break;
            case platform::MidiEvent::Type::ControlChange:
                message = 0xB0U | ((event.channel - 1) & 0x0FU);
                break;
            }
            message |= static_cast<DWORD>(event.data1 & 0x7FU) << 8U;
            message |= static_cast<DWORD>(event.data2 & 0x7FU) << 16U;
            sent = midiOutShortMsg(output_, message) == MMSYSERR_NOERROR;
        }
        unlock();
        return sent;
    }

    bool hasInput() const noexcept override { return input_ != nullptr; }
    bool hasOutput() const noexcept override { return output_ != nullptr; }

    const char* name() const noexcept override { return name_.data(); }

private:
    static void CALLBACK callback(HMIDIIN,
                                  UINT message,
                                  DWORD_PTR instance,
                                  DWORD_PTR param1,
                                  DWORD_PTR) noexcept {
        if (message != MIM_DATA || instance == 0) {
            return;
        }
        auto* self = reinterpret_cast<WinMmMidiBackend*>(instance);
        self->onMessage(static_cast<std::uint32_t>(param1));
    }

    void onMessage(std::uint32_t data) noexcept {
        const auto status = static_cast<std::uint8_t>(data & 0xFFU);
        platform::MidiEvent event;
        event.timestampMicros = nowMicros();
        switch (status) {
        case 0xF8:
            event.type = platform::MidiEvent::Type::Clock;
            break;
        case 0xFA:
            event.type = platform::MidiEvent::Type::Start;
            break;
        case 0xFB:
            event.type = platform::MidiEvent::Type::Continue;
            break;
        case 0xFC:
            event.type = platform::MidiEvent::Type::Stop;
            break;
        default: {
            const auto kind = status & 0xF0U;
            if (kind != 0x80U && kind != 0x90U && kind != 0xB0U) {
                return;
            }
            event.channel = static_cast<std::uint8_t>((status & 0x0FU) + 1U);
            event.data1 = static_cast<std::uint8_t>((data >> 8U) & 0x7FU);
            event.data2 = static_cast<std::uint8_t>((data >> 16U) & 0x7FU);
            if (kind == 0x80U ||
                (kind == 0x90U && event.data2 == 0)) {
                event.type = platform::MidiEvent::Type::NoteOff;
            } else if (kind == 0x90U) {
                event.type = platform::MidiEvent::Type::NoteOn;
            } else {
                event.type = platform::MidiEvent::Type::ControlChange;
            }
            break;
        }
        }
        push(event);
    }

    void push(const platform::MidiEvent& event) noexcept {
        const auto write = writeIndex_.load(std::memory_order_relaxed);
        const auto read = readIndex_.load(std::memory_order_acquire);
        if (write - read >= EventCapacity) {
            return;
        }
        events_[write % EventCapacity] = event;
        writeIndex_.store(write + 1, std::memory_order_release);
    }

    void copyName(const wchar_t* source) noexcept {
        const int written = WideCharToMultiByte(CP_UTF8, 0, source, -1,
                                                name_.data(),
                                                static_cast<int>(name_.size()),
                                                nullptr, nullptr);
        if (written <= 0) {
            name_[0] = '\0';
        }
    }

    std::uint8_t deviceIndex_ = 0;
    HMIDIIN input_ = nullptr;
    HMIDIOUT output_ = nullptr;
    std::array<char, 96> name_{};
    std::array<platform::MidiEvent, EventCapacity> events_{};
    alignas(64) std::atomic<std::uint32_t> writeIndex_{0};
    alignas(64) std::atomic<std::uint32_t> readIndex_{0};
    // Guards handle open/close against audio-thread sends. Never held for long.
    mutable std::atomic_flag ioLock_ = ATOMIC_FLAG_INIT;

    void lock() const noexcept {
        while (ioLock_.test_and_set(std::memory_order_acquire)) {
        }
    }
    bool tryLock() const noexcept {
        return !ioLock_.test_and_set(std::memory_order_acquire);
    }
    void unlock() const noexcept {
        ioLock_.clear(std::memory_order_release);
    }
};

#endif // _WIN32

} // namespace

bool systemMidiInputAvailable() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

std::unique_ptr<platform::IMidiBackend> makeSystemMidiBackend(
    std::uint8_t deviceIndex) {
#ifdef _WIN32
    return std::make_unique<WinMmMidiBackend>(deviceIndex);
#else
    (void)deviceIndex;
    return std::make_unique<NullMidiBackend>();
#endif
}

void listSystemMidiInputs(std::ostream& out) {
#ifdef _WIN32
    const auto inputs = midiInGetNumDevs();
    if (inputs == 0) {
        out << "No MIDI input ports found.\n";
    } else {
        out << "MIDI input ports:\n";
        for (UINT index = 0; index < inputs; ++index) {
            MIDIINCAPSW caps{};
            if (midiInGetDevCapsW(index, &caps, sizeof(caps)) !=
                MMSYSERR_NOERROR) {
                continue;
            }
            char name[96] = {};
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name,
                                static_cast<int>(sizeof(name)), nullptr,
                                nullptr);
            out << index << ": " << name << '\n';
        }
    }
    const auto outputs = midiOutGetNumDevs();
    if (outputs == 0) {
        out << "No MIDI output ports found.\n";
    } else {
        out << "MIDI output ports:\n";
        for (UINT index = 0; index < outputs; ++index) {
            MIDIOUTCAPSW caps{};
            if (midiOutGetDevCapsW(index, &caps, sizeof(caps)) !=
                MMSYSERR_NOERROR) {
                continue;
            }
            char name[96] = {};
            WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, name,
                                static_cast<int>(sizeof(name)), nullptr,
                                nullptr);
            out << index << ": " << name << '\n';
        }
    }
#else
    out << "MIDI is not supported on this platform.\n";
#endif
}

} // namespace pocket::app
