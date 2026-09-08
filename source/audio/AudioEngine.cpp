#include "AudioEngine.h"
#include "../core/AudioDuration.h"
#include "../core/FileUtil.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include <algorithm>

namespace
{
    // post-mix 回调没有 userdata 之外的上下文，这里用文件作用域指针把实例带进去
    CAudioEngine* g_engine = nullptr;
    volatile bool g_song_finished = false;
}

CAudioEngine::CAudioEngine()
{
    g_engine = this;
}

CAudioEngine::~CAudioEngine()
{
    Uninit();
    if (g_engine == this)
        g_engine = nullptr;
}

bool CAudioEngine::Init()
{
    if (m_inited)
        return true;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        m_last_error = std::string("SDL_InitSubSystem(AUDIO) 失败: ") + SDL_GetError();
        return false;
    }

    // Switch 的音频输出固定 48kHz 立体声；2048 帧的缓冲在掌机模式下也能稳定不断音
    if (Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 2048) != 0)
    {
        m_last_error = std::string("Mix_OpenAudio 失败: ") + Mix_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    int frequency = 0, channels = 0;
    Uint16 format = 0;
    if (Mix_QuerySpec(&frequency, &format, &channels))
        m_mix_channels = channels;

    // 只申请这个 SDL2_mixer 构建里真正带的解码器。
    // devkitPro 的 switch-sdl2_mixer 2.0.4 没有编译 FLAC，申请 MIX_INIT_FLAC 只会白白失败。
    const int wanted = MIX_INIT_MP3 | MIX_INIT_OGG | MIX_INIT_OPUS | MIX_INIT_MOD | MIX_INIT_MID;
    Mix_Init(wanted);

    Mix_SetPostMix(&CAudioEngine::PostMixCallback, this);
    Mix_HookMusicFinished(&CAudioEngine::MusicFinishedCallback);

    SetVolume(m_volume);
    m_inited = true;
    return true;
}

void CAudioEngine::Uninit()
{
    if (!m_inited)
        return;
    Close();
    Mix_SetPostMix(nullptr, nullptr);
    Mix_HookMusicFinished(nullptr);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_inited = false;
}

void CAudioEngine::PostMixCallback(void* udata, unsigned char* stream, int len)
{
    CAudioEngine* engine = static_cast<CAudioEngine*>(udata);
    if (engine == nullptr || stream == nullptr || len <= 0)
        return;
    // Mix_OpenAudio 指定了 AUDIO_S16SYS，所以这里可以安全地按 int16 解释
    const int16_t* samples = reinterpret_cast<const int16_t*>(stream);
    int frame_count = len / (2 * engine->m_mix_channels);
    engine->m_spectrum.PushSamples(samples, frame_count, engine->m_mix_channels);
}

void CAudioEngine::MusicFinishedCallback()
{
    g_song_finished = true;
}

void CAudioEngine::FlacMixCallback(void* udata, unsigned char* stream, int len)
{
    CAudioEngine* engine = static_cast<CAudioEngine*>(udata);
    if (engine == nullptr || stream == nullptr || len <= 0)
        return;

    // SDL_mixer 在调用这个钩子之前已经把 stream 填成静音，
    // 所以暂停时直接返回即可，不必再清零
    if (engine->m_flac_state != PS_PLAYING)
        return;

    engine->m_flac.Read(stream, static_cast<size_t>(len));

    // 解码完且缓冲排空后才算播完，交给主循环去切下一首
    if (engine->m_flac.IsFinished())
    {
        engine->m_flac_state = PS_STOPPED;
        g_song_finished = true;
    }
}

bool CAudioEngine::IsFlacFile(const std::string& file_path)
{
    return FileUtil::GetExtension(file_path) == "flac";
}

bool CAudioEngine::Open(const std::string& file_path)
{
    if (!m_inited)
    {
        m_last_error = "音频引擎尚未初始化";
        return false;
    }

    Close();

    if (!FileUtil::Exists(file_path))
    {
        m_last_error = "文件不存在: " + file_path;
        return false;
    }

    g_song_finished = false;
    m_spectrum.Reset();

    // devkitPro 的 SDL_mixer 没编译 FLAC，这类文件走自己的解码器
    return IsFlacFile(file_path) ? OpenWithFlac(file_path) : OpenWithMixer(file_path);
}

bool CAudioEngine::OpenWithFlac(const std::string& file_path)
{
    int frequency = 48000, channels = 2;
    Uint16 format = 0;
    Mix_QuerySpec(&frequency, &format, &channels);

    if (!m_flac.Open(file_path, frequency, channels))
    {
        m_last_error = m_flac.GetLastError();
        return false;
    }

    m_backend = BK_FLAC;
    m_file_path = file_path;
    m_length_ms = m_flac.GetLengthMs();
    m_flac_state = PS_PLAYING;

    // 接管音乐流。注意这会绕过 Mix_Music，所以播放状态要自己维护。
    Mix_HookMusic(&CAudioEngine::FlacMixCallback, this);
    return true;
}

bool CAudioEngine::OpenWithMixer(const std::string& file_path)
{
    m_music = Mix_LoadMUS(file_path.c_str());
    if (m_music == nullptr)
    {
        m_last_error = std::string("无法解码: ") + Mix_GetError();
        return false;
    }

    m_file_path = file_path;
    m_length_ms = 0;
// Mix_MusicDuration 是 SDL_mixer 2.6.0 才有的接口；devkitPro 带的是 2.0.4，没有它
#if defined(SDL_MIXER_VERSION_ATLEAST) && SDL_MIXER_VERSION_ATLEAST(2, 6, 0)
    double duration = Mix_MusicDuration(m_music);
    if (duration > 0.0)
        m_length_ms = static_cast<int>(duration * 1000.0);
#endif
    // 拿不到就自己从文件头算。不这么做的话 MP3 的进度条右侧永远是 "-:--"，
    // 而且没有总长度就无法按比例定位，触摸拖动进度条也用不了。
    if (m_length_ms <= 0)
        m_length_ms = AudioDuration::Estimate(file_path);

    if (Mix_PlayMusic(m_music, 1) != 0)      // 循环由上层的播放模式决定，这里只播一遍
    {
        m_last_error = std::string("Mix_PlayMusic 失败: ") + Mix_GetError();
        Close();
        return false;
    }

    m_backend = BK_MIXER;
    m_seek_base_ms = 0;
    m_seek_tick = SDL_GetTicks();
    m_paused_position_ms = 0;
    return true;
}

void CAudioEngine::Close()
{
    if (m_backend == BK_FLAC)
    {
        // 必须先摘掉钩子再关解码器，否则音频线程可能读到已释放的对象
        Mix_HookMusic(nullptr, nullptr);
        m_flac.Close();
        m_flac_state = PS_STOPPED;
    }
    if (m_music != nullptr)
    {
        Mix_HaltMusic();
        Mix_FreeMusic(m_music);
        m_music = nullptr;
    }
    m_backend = BK_NONE;
    m_file_path.clear();
    m_length_ms = 0;
    m_seek_base_ms = 0;
    m_paused_position_ms = 0;
    m_spectrum.Reset();
}

void CAudioEngine::Play()
{
    if (m_backend == BK_FLAC)
    {
        if (m_flac_state == PS_PAUSED)
        {
            m_flac_state = PS_PLAYING;
        }
        else if (m_flac_state == PS_STOPPED)
        {
            // 已经播完的曲目重新播放：回到开头
            m_flac.Seek(0);
            m_flac_state = PS_PLAYING;
        }
        return;
    }

    if (m_music == nullptr)
        return;
    if (Mix_PausedMusic())
    {
        Mix_ResumeMusic();
        // 恢复播放：把计时基准挪到当前时刻，避免把暂停时长算进播放位置
        m_seek_base_ms = m_paused_position_ms;
        m_seek_tick = SDL_GetTicks();
    }
    else if (!Mix_PlayingMusic())
    {
        Mix_PlayMusic(m_music, 1);
        m_seek_base_ms = 0;
        m_seek_tick = SDL_GetTicks();
    }
}

void CAudioEngine::Pause()
{
    if (m_backend == BK_FLAC)
    {
        // 混音回调看到非 PS_PLAYING 就不再消费缓冲，位置自然停住
        if (m_flac_state == PS_PLAYING)
            m_flac_state = PS_PAUSED;
        return;
    }

    if (m_music == nullptr || !Mix_PlayingMusic() || Mix_PausedMusic())
        return;
    m_paused_position_ms = GetCurrentPosition();
    Mix_PauseMusic();
}

void CAudioEngine::TogglePause()
{
    if (GetState() == PS_PLAYING)
        Pause();
    else
        Play();
}

void CAudioEngine::Stop()
{
    if (m_backend == BK_FLAC)
    {
        m_flac_state = PS_STOPPED;
        m_flac.Seek(0);
        m_spectrum.Reset();
        return;
    }

    if (m_music == nullptr)
        return;
    Mix_HaltMusic();
    m_seek_base_ms = 0;
    m_paused_position_ms = 0;
    m_spectrum.Reset();
}

CAudioEngine::PlayingState CAudioEngine::GetState() const
{
    if (m_backend == BK_FLAC)
        return m_flac_state;

    if (m_music == nullptr || !Mix_PlayingMusic())
        return PS_STOPPED;
    return Mix_PausedMusic() ? PS_PAUSED : PS_PLAYING;
}

int CAudioEngine::GetCurrentPosition() const
{
    // FLAC 后端按实际送进混音器的帧数计算，比下面基于时钟的估算更准
    if (m_backend == BK_FLAC)
        return m_flac.GetPositionMs();

    if (m_music == nullptr)
        return 0;
    if (Mix_PausedMusic())
        return m_paused_position_ms;
    if (!Mix_PlayingMusic())
        return 0;

    int elapsed = static_cast<int>(SDL_GetTicks() - m_seek_tick);
    int position = m_seek_base_ms + elapsed;
    if (m_length_ms > 0 && position > m_length_ms)
        position = m_length_ms;
    return position;
}

int CAudioEngine::GetSongLength() const
{
    return m_length_ms;
}

bool CAudioEngine::SetPosition(int ms)
{
    if (m_backend == BK_FLAC)
    {
        if (!m_flac.Seek(ms))
            return false;
        m_spectrum.Reset();
        // 播完之后再拖进度条应当恢复播放
        if (m_flac_state == PS_STOPPED)
            m_flac_state = PS_PLAYING;
        return true;
    }

    if (m_music == nullptr)
        return false;
    if (ms < 0)
        ms = 0;
    if (m_length_ms > 0 && ms > m_length_ms)
        ms = m_length_ms;

    // Mix_SetMusicPosition 只对当前正在播放的音乐生效
    if (!Mix_PlayingMusic())
        return false;

    bool was_paused = Mix_PausedMusic();
    if (was_paused)
        Mix_ResumeMusic();

    if (Mix_SetMusicPosition(ms / 1000.0) != 0)
    {
        m_last_error = std::string("该格式不支持定位: ") + Mix_GetError();
        if (was_paused)
            Mix_PauseMusic();
        return false;
    }

    m_seek_base_ms = ms;
    m_seek_tick = SDL_GetTicks();
    m_paused_position_ms = ms;
    if (was_paused)
        Mix_PauseMusic();
    m_spectrum.Reset();
    return true;
}

bool CAudioEngine::Seek(int delta_ms)
{
    return SetPosition(GetCurrentPosition() + delta_ms);
}

void CAudioEngine::SetVolume(int volume)
{
    m_volume = std::max(0, std::min(100, volume));
    if (m_inited)
        Mix_VolumeMusic(m_volume * MIX_MAX_VOLUME / 100);
}

void CAudioEngine::AdjustVolume(int delta)
{
    SetVolume(m_volume + delta);
}

void CAudioEngine::Update()
{
    // FLAC 的时长来自 STREAMINFO，开文件时就准确知道了，不需要反推
    if (m_backend != BK_MIXER || m_music == nullptr)
        return;
    // 部分格式（如无法预读时长的流）拿不到 Mix_MusicDuration，
    // 这里用播放到结束时的位置反过来补齐时长，好让进度条至少在第二次播放时正确
    if (m_length_ms == 0 && g_song_finished)
        m_length_ms = GetCurrentPosition();
}

bool CAudioEngine::TakeSongFinished()
{
    if (!g_song_finished)
        return false;
    g_song_finished = false;
    return true;
}
