#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>

#include "core/Nes.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"TestAiNESWin32Window";
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameTimerMs = 8;
constexpr int kAudioSamplesPerBuffer = 2048;
constexpr int kAudioBufferCount = 3;

std::wstring widenAscii(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

class WaveOutPlayer {
public:
    bool start() {
        if (wave_) {
            return true;
        }
        stopping_ = false;

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = 44100;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * sizeof(std::int16_t));
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        if (waveOutOpen(&wave_, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&WaveOutPlayer::callback),
                        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
            wave_ = nullptr;
            return false;
        }

        for (auto& buffer : buffers_) {
            buffer.samples.fill(0);
            buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
            buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(std::int16_t));
            waveOutPrepareHeader(wave_, &buffer.header, sizeof(buffer.header));
            fill(buffer);
            waveOutWrite(wave_, &buffer.header, sizeof(buffer.header));
        }
        return true;
    }

    void stop() {
        if (!wave_) {
            return;
        }
        stopping_ = true;
        waveOutReset(wave_);
        for (auto& buffer : buffers_) {
            waveOutUnprepareHeader(wave_, &buffer.header, sizeof(buffer.header));
            buffer.header = {};
        }
        waveOutClose(wave_);
        wave_ = nullptr;
    }

    void push(std::vector<float> samples) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (float sample : samples) {
            fifo_.push_back(sample);
        }
        while (fifo_.size() > 44100) {
            fifo_.pop_front();
        }
    }

private:
    struct Buffer {
        WAVEHDR header{};
        std::array<std::int16_t, kAudioSamplesPerBuffer> samples{};
    };

    static void CALLBACK callback(HWAVEOUT, UINT msg, DWORD_PTR user, DWORD_PTR param1, DWORD_PTR) {
        if (msg != WOM_DONE) {
            return;
        }
        auto* player = reinterpret_cast<WaveOutPlayer*>(user);
        if (player->stopping_) {
            return;
        }
        auto* header = reinterpret_cast<WAVEHDR*>(param1);
        for (auto& buffer : player->buffers_) {
            if (&buffer.header == header) {
                player->fill(buffer);
                if (!player->stopping_ && player->wave_) {
                    waveOutWrite(player->wave_, &buffer.header, sizeof(buffer.header));
                }
                return;
            }
        }
    }

    void fill(Buffer& buffer) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::int16_t& out : buffer.samples) {
            float sample = 0.0f;
            if (!fifo_.empty()) {
                sample = fifo_.front();
                fifo_.pop_front();
                if (underrunRecovery_ > 0) {
                    const float t = static_cast<float>(kRecoverySamples - underrunRecovery_ + 1) /
                                    static_cast<float>(kRecoverySamples);
                    sample = recoveryStart_ + (sample - recoveryStart_) * t;
                    --underrunRecovery_;
                }
                lastSample_ = sample;
            } else {
                lastSample_ *= 0.999f;
                sample = lastSample_;
                underrunRecovery_ = kRecoverySamples;
                recoveryStart_ = lastSample_;
            }
            out = static_cast<std::int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        }
    }

    HWAVEOUT wave_ = nullptr;
    std::array<Buffer, kAudioBufferCount> buffers_{};
    std::atomic_bool stopping_{false};
    std::mutex mutex_;
    std::deque<float> fifo_;
    float lastSample_ = 0.0f;
    static constexpr int kRecoverySamples = 64;
    int underrunRecovery_ = 0;
    float recoveryStart_ = 0.0f;
};

class App {
public:
    bool create(HINSTANCE instance, int showCommand) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = &App::windowProcSetup;
        wc.hInstance = instance;
        wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RegisterClassW(&wc);

        RECT rect{0, 0, nes::kScreenWidth * 3, nes::kScreenHeight * 3};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);
        hwnd_ = CreateWindowExW(0, kWindowClass, L"TestAiNES", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                rect.right - rect.left, rect.bottom - rect.top, nullptr, createMenu(), instance_, this);
        if (!hwnd_) {
            return false;
        }

        bitmapInfo_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo_.bmiHeader.biWidth = nes::kScreenWidth;
        bitmapInfo_.bmiHeader.biHeight = -nes::kScreenHeight;
        bitmapInfo_.bmiHeader.biPlanes = 1;
        bitmapInfo_.bmiHeader.biBitCount = 32;
        bitmapInfo_.bmiHeader.biCompression = BI_RGB;
        pixels_.resize(nes::kScreenWidth * nes::kScreenHeight);

        if (!audio_.start()) {
            MessageBoxW(hwnd_, L"Audio output could not be started. The emulator will continue without sound.",
                        L"Audio unavailable", MB_ICONWARNING | MB_OK);
        }
        lastFrameTick_ = Clock::now();
        SetTimer(hwnd_, kFrameTimer, kFrameTimerMs, nullptr);
        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
        return true;
    }

    int run() {
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

private:
    using Clock = std::chrono::steady_clock;

    enum Command : UINT {
        CommandOpen = 100,
        CommandReset = 101,
        CommandExit = 102,
    };

    static LRESULT CALLBACK windowProcSetup(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            auto* app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&App::windowProcThunk));
            return app->windowProc(hwnd, msg, wparam, lparam);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    static LRESULT CALLBACK windowProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return app->windowProc(hwnd, msg, wparam, lparam);
    }

    LRESULT windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        switch (msg) {
        case WM_COMMAND:
            handleCommand(LOWORD(wparam));
            return 0;
        case WM_TIMER:
            if (wparam == kFrameTimer) {
                onFrameTimer(hwnd);
            }
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP:
            setKey(static_cast<UINT>(wparam), msg == WM_KEYDOWN);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kFrameTimer);
            audio_.stop();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    HMENU createMenu() {
        HMENU menu = CreateMenu();
        HMENU file = CreatePopupMenu();
        AppendMenuW(file, MF_STRING, CommandOpen, L"&Open ROM...");
        AppendMenuW(file, MF_STRING, CommandReset, L"&Reset");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file, MF_STRING, CommandExit, L"E&xit");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
        return menu;
    }

    void handleCommand(UINT command) {
        switch (command) {
        case CommandOpen:
            openRom();
            break;
        case CommandReset:
            if (emulator_.hasCartridge()) {
                emulator_.reset();
            }
            break;
        case CommandExit:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
        }
    }

    void openRom() {
        wchar_t path[MAX_PATH]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"NES ROMs (*.nes)\0*.nes\0All files (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"nes";
        if (!GetOpenFileNameW(&ofn)) {
            return;
        }

        std::string error;
        if (!emulator_.loadRom(std::filesystem::path(path), error)) {
            MessageBoxW(hwnd_, widenAscii(error).c_str(), L"Could not load ROM", MB_ICONERROR | MB_OK);
            return;
        }
        frameAccumulator_ = std::chrono::duration<double>::zero();
        lastFrameTick_ = Clock::now();

        std::wstring title = L"TestAiNES - ";
        title += std::filesystem::path(path).filename().wstring();
        title += L" (";
        title += widenAscii(emulator_.mapperName());
        title += L")";
        SetWindowTextW(hwnd_, title.c_str());
    }

    void setKey(UINT key, bool pressed) {
        if (!emulator_.hasCartridge()) {
            return;
        }
        switch (key) {
        case 'Z': emulator_.setButton(0, nes::Button::A, pressed); break;
        case 'X': emulator_.setButton(0, nes::Button::B, pressed); break;
        case 'A': emulator_.setButton(0, nes::Button::Select, pressed); break;
        case 'S': emulator_.setButton(0, nes::Button::Start, pressed); break;
        case VK_UP: emulator_.setButton(0, nes::Button::Up, pressed); break;
        case VK_DOWN: emulator_.setButton(0, nes::Button::Down, pressed); break;
        case VK_LEFT: emulator_.setButton(0, nes::Button::Left, pressed); break;
        case VK_RIGHT: emulator_.setButton(0, nes::Button::Right, pressed); break;
        default: break;
        }
    }

    void onFrameTimer(HWND hwnd) {
        const auto now = Clock::now();
        if (!emulator_.hasCartridge()) {
            frameAccumulator_ = std::chrono::duration<double>::zero();
            lastFrameTick_ = now;
            return;
        }

        constexpr std::chrono::duration<double> frameDuration{1.0 / nes::kFrameRateNtsc};
        frameAccumulator_ += now - lastFrameTick_;
        lastFrameTick_ = now;

        int framesStepped = 0;
        while (frameAccumulator_ >= frameDuration && framesStepped < 2) {
            emulator_.stepFrame();
            audio_.push(emulator_.takeAudioSamples());
            frameAccumulator_ -= frameDuration;
            ++framesStepped;
        }
        if (framesStepped == 2 && frameAccumulator_ >= frameDuration) {
            frameAccumulator_ = std::chrono::duration<double>::zero();
        }
        if (framesStepped > 0) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        if (emulator_.hasCartridge()) {
            const auto framebuffer = emulator_.framebufferSnapshot();
            for (std::size_t i = 0; i < framebuffer.size(); ++i) {
                const auto& p = framebuffer[i];
                pixels_[i] = static_cast<DWORD>(p.b) | (static_cast<DWORD>(p.g) << 8) | (static_cast<DWORD>(p.r) << 16);
            }

            const int clientWidth = client.right - client.left;
            const int clientHeight = client.bottom - client.top;
            const double aspect = static_cast<double>(nes::kScreenWidth) / static_cast<double>(nes::kScreenHeight);
            int width = clientWidth;
            int height = static_cast<int>(width / aspect);
            if (height > clientHeight) {
                height = clientHeight;
                width = static_cast<int>(height * aspect);
            }
            const int x = (clientWidth - width) / 2;
            const int y = (clientHeight - height) / 2;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, x, y, width, height, 0, 0, nes::kScreenWidth, nes::kScreenHeight, pixels_.data(),
                          &bitmapInfo_, DIB_RGB_COLORS, SRCCOPY);
        }

        EndPaint(hwnd_, &ps);
    }

    HWND hwnd_ = nullptr;
    nes::Nes emulator_;
    WaveOutPlayer audio_;
    BITMAPINFO bitmapInfo_{};
    std::vector<DWORD> pixels_;
    Clock::time_point lastFrameTick_{};
    std::chrono::duration<double> frameAccumulator_{0.0};
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    timeBeginPeriod(1);
    App app;
    const int result = app.create(instance, showCommand) ? app.run() : 1;
    timeEndPeriod(1);
    return result;
}
