// 可移植核心层的主机端测试。
// 这些代码在 Switch 上和在开发机上编译的是同一份实现，因此可以在本机验证解析逻辑。
//
// 构建：SwitchPort/tests/build_host_test.ps1

#include "../source/core/Config.h"
#include "../source/core/FileUtil.h"
#include "../source/core/LrcParser.h"
#include "../source/core/MediaScanner.h"
#include "../source/core/PathMapper.h"
#include "../source/core/PlaylistFile.h"
#include "../source/core/PlayTime.h"
#include "../source/core/StringUtil.h"

#include "TestFramework.h"

#include <cstdio>
#include <string>

// 在 net_test.cpp / audio_test.cpp 中实现
void RunNetTests();
void RunAudioTests();
void RunDurationTests();
void RunTagWriteTests();
void RunTagTests();


// ---------------------------------------------------------------- StringUtil

static void TestStringUtil()
{
    std::printf("StringUtil\n");

    // UTF-8 字符边界
    CHECK_EQ_INT(StringUtil::Utf8Length(u8"中文abc"), 5);
    CHECK_EQ(StringUtil::Utf8Substr(u8"中文abc", 2), u8"中文");
    CHECK_EQ_INT(StringUtil::Utf8CharLen(u8"中", 0), 3);
    CHECK_EQ_INT(StringUtil::Utf8CharLen("a", 0), 1);

    // 非法字节不能导致死循环
    std::string invalid = "\xC3";
    CHECK_EQ_INT(StringUtil::Utf8CharLen(invalid, 0), 1);
    CHECK_EQ_INT(StringUtil::Utf8Length("\xFF\xFE\xC0"), 3);

    // UTF-16 往返（含代理对：U+1D11E 高音谱号）
    std::string emoji = u8"a\U0001D11Eb";
    CHECK_EQ(StringUtil::Utf16ToUtf8(StringUtil::Utf8ToUtf16(emoji)), emoji);

    // BOM 处理
    std::string with_bom = "\xEF\xBB\xBF" "hello";
    CHECK_EQ(StringUtil::FileContentToUtf8(with_bom), "hello");
    std::string utf16le = std::string("\xFF\xFE", 2) + std::string("h\0i\0", 4);
    CHECK_EQ(StringUtil::FileContentToUtf8(utf16le), "hi");

    // Trim：ASCII 空白 + 全角空格
    CHECK_EQ(StringUtil::Trimmed("  a b \t\r"), "a b");
    CHECK_EQ(StringUtil::Trimmed(u8"\u3000中文\u3000"), u8"中文");
    CHECK_EQ(StringUtil::Trimmed(""), "");
    CHECK_EQ(StringUtil::Trimmed("   "), "");

    // Split：保留空字段是播放列表解析的关键
    std::vector<std::string> parts;
    StringUtil::Split("a||b|", '|', parts, false);
    CHECK_EQ_INT(parts.size(), 4);
    CHECK_EQ(parts[1], "");
    CHECK_EQ(parts[3], "");

    std::vector<std::string> lines;
    StringUtil::SplitLine("a\r\nb\nc", lines);
    CHECK_EQ_INT(lines.size(), 3);
    CHECK_EQ(lines[0], "a");
    CHECK_EQ(lines[2], "c");

    // ToInt 对齐 _wtoi 的行为
    CHECK_EQ_INT(StringUtil::ToInt("  -42abc"), -42);
    CHECK_EQ_INT(StringUtil::ToInt("abc"), 0);
    CHECK_EQ_INT(StringUtil::ToInt("999999999999"), 2147483647);

    CHECK(StringUtil::IsUrl("https://example.com/a.mp3"));
    CHECK(!StringUtil::IsUrl("D:\\music\\a.mp3"));
}

// ------------------------------------------------------------------ PlayTime

static void TestPlayTime()
{
    std::printf("PlayTime\n");

    CPlayTime t;
    t.fromInt(125300);
    CHECK_EQ_INT(t.min, 2);
    CHECK_EQ_INT(t.sec, 5);
    CHECK_EQ_INT(t.msec, 300);
    CHECK_EQ_INT(t.toInt(), 125300);
    CHECK_EQ(t.toString(), "2:05");

    CPlayTime neg;
    neg.fromInt(-1500);
    CHECK_EQ_INT(neg.toInt(), -1500);
    CHECK(neg < CPlayTime(0));

    CHECK_EQ_INT(CPlayTime(5000) - CPlayTime(2000), 3000);
    CHECK_EQ(CPlayTime(3725000).toString3(), "1:02:05");
    CHECK_EQ(CPlayTime(0).toString(), "-:--");
}

// ------------------------------------------------------------------ FileUtil

static void TestFileUtil()
{
    std::printf("FileUtil\n");

    CHECK_EQ(FileUtil::GetFileName("sdmc:/music/a b.mp3"), "a b.mp3");
    CHECK_EQ(FileUtil::GetFileNameWithoutExt("sdmc:/music/a.b.mp3"), "a.b");
    CHECK_EQ(FileUtil::GetExtension("A.MP3"), "mp3");
    CHECK_EQ(FileUtil::GetExtension("A.MP3", true, true), ".MP3");
    CHECK_EQ(FileUtil::GetExtension("noext"), "");
    CHECK_EQ(FileUtil::GetDir("sdmc:/music/a.mp3"), "sdmc:/music");
    CHECK_EQ(FileUtil::ReplaceExtension("sdmc:/m/a.mp3", "lrc"), "sdmc:/m/a.lrc");
    CHECK_EQ(FileUtil::ReplaceExtension("sdmc:/m/noext", ".lrc"), "sdmc:/m/noext.lrc");

    CHECK_EQ(FileUtil::NormalizeSeparators("D:\\a\\\\b"), "D:/a/b");
    CHECK(FileUtil::IsAbsolute("sdmc:/a"));
    CHECK(FileUtil::IsAbsolute("D:\\a"));
    CHECK(FileUtil::IsAbsolute("/a"));
    CHECK(!FileUtil::IsAbsolute("a/b"));

    CHECK_EQ(FileUtil::RelativeToAbsolute("sub/a.mp3", "sdmc:/music"), "sdmc:/music/sub/a.mp3");
    CHECK_EQ(FileUtil::RelativeToAbsolute("../a.mp3", "sdmc:/music/sub"), "sdmc:/music/a.mp3");
    CHECK_EQ(FileUtil::RelativeToAbsolute("./a.mp3", "sdmc:/music"), "sdmc:/music/a.mp3");
    CHECK_EQ(FileUtil::RelativeToAbsolute("sdmc:/x/a.mp3", "sdmc:/music"), "sdmc:/x/a.mp3");
}

// ---------------------------------------------------------------- PathMapper

static void TestPathMapper()
{
    std::printf("PathMapper\n");

    CPathMapper mapper;
    mapper.SetDefaultMusicDir("sdmc:/music");
    mapper.AddRule("D:\\Music\\", "sdmc:/music/");
    mapper.AddRule("E:\\ACG", "sdmc:/music/acg/");

    // 命中规则（Windows 路径大小写不敏感）
    CHECK_EQ(mapper.ToSwitchPath("D:\\Music\\pop\\a.mp3"), "sdmc:/music/pop/a.mp3");
    CHECK_EQ(mapper.ToSwitchPath("d:/MUSIC/pop/a.mp3"), "sdmc:/music/pop/a.mp3");
    CHECK_EQ(mapper.ToSwitchPath("E:\\ACG\\b.flac"), "sdmc:/music/acg/b.flac");

    // 未命中规则：去掉盘符挂到默认目录
    CHECK_EQ(mapper.ToSwitchPath("F:\\other\\c.mp3"), "sdmc:/music/other/c.mp3");

    // 已经是 Switch 路径 / URL 时原样返回
    CHECK_EQ(mapper.ToSwitchPath("sdmc:/music/d.mp3"), "sdmc:/music/d.mp3");
    CHECK_EQ(mapper.ToSwitchPath("http://a/b.mp3"), "http://a/b.mp3");

    // 反向映射
    CHECK_EQ(mapper.ToDesktopPath("sdmc:/music/acg/b.flac"), "e:\\acg\\b.flac");
    CHECK_EQ(mapper.ToDesktopPath("sdmc:/unmapped/x.mp3"), "sdmc:/unmapped/x.mp3");
}

// -------------------------------------------------------------- PlaylistFile

static void TestPlaylistFile()
{
    std::printf("PlaylistFile\n");

    CPathMapper mapper;
    mapper.SetDefaultMusicDir("sdmc:/music");
    mapper.AddRule("D:\\Music\\", "sdmc:/music/");

    // 桌面版 .playlist：普通曲目只有路径一列，cue 音轨是完整的管道分隔行
    const char* content =
        "D:\\Music\\pop\\a.mp3\n"
        "D:\\Music\\b.flac\n"
        "D:\\Music\\disc.ape|1|5000|180000|标题|艺术家|专辑|3|960|Rock|2020|注释|D:\\Music\\disc.cue\n"
        "\n"
        "\"D:\\Music\\quoted.mp3\"\n";

    CPlaylistFile pl(&mapper);
    pl.ParsePlaylistContent(content, "sdmc:/playlists");
    const std::vector<SongInfo>& songs = pl.GetPlaylist();

    CHECK_EQ_INT(songs.size(), 4);
    CHECK_EQ(songs[0].file_path, "sdmc:/music/pop/a.mp3");
    CHECK(!songs[0].is_cue);
    CHECK_EQ(songs[1].file_path, "sdmc:/music/b.flac");

    // cue 行的全部字段都要解析出来
    CHECK(songs[2].is_cue);
    CHECK_EQ_INT(songs[2].start_pos.toInt(), 5000);
    CHECK_EQ_INT(songs[2].end_pos.toInt(), 180000);
    CHECK_EQ(songs[2].title, u8"标题");
    CHECK_EQ(songs[2].artist, u8"艺术家");
    CHECK_EQ(songs[2].album, u8"专辑");
    CHECK_EQ_INT(songs[2].track, 3);
    CHECK_EQ_INT(songs[2].bitrate, 960);
    CHECK_EQ(songs[2].genre, "Rock");
    CHECK_EQ(songs[2].year, "2020");
    CHECK_EQ(songs[2].comment, u8"注释");
    CHECK_EQ(songs[2].cue_file_path, "D:\\Music\\disc.cue");

    // 引号要被剥掉
    CHECK_EQ(songs[3].file_path, "sdmc:/music/quoted.mp3");

    // m3u8：#EXTINF 提供标题和时长，相对路径按播放列表所在目录展开
    const char* m3u =
        "#EXTM3U\n"
        "#EXTINF:236,Artist - Title\n"
        "sub/x.mp3\n"
        "#EXTINF:-1,No Length\n"
        "sdmc:/music/y.ogg\n";

    CPlaylistFile m3u_pl(&mapper);
    m3u_pl.ParseM3uContent(m3u, "sdmc:/playlists");
    const std::vector<SongInfo>& m3u_songs = m3u_pl.GetPlaylist();
    CHECK_EQ_INT(m3u_songs.size(), 2);
    CHECK_EQ(m3u_songs[0].file_path, "sdmc:/playlists/sub/x.mp3");
    CHECK_EQ(m3u_songs[0].title, "Artist - Title");
    CHECK_EQ_INT(m3u_songs[0].length.toInt(), 236000);
    CHECK_EQ(m3u_songs[1].file_path, "sdmc:/music/y.ogg");
    CHECK_EQ_INT(m3u_songs[1].length.toInt(), 0);

    // 去重与排序
    CPlaylistFile add_pl(&mapper);
    std::vector<SongInfo> batch;
    SongInfo s1; s1.file_path = "sdmc:/music/1.mp3";
    SongInfo s2; s2.file_path = "sdmc:/music/2.mp3";
    batch.push_back(s1);
    batch.push_back(s2);
    batch.push_back(s1);                                    // 重复项应被忽略
    CHECK_EQ_INT(add_pl.AddSongs(batch), 2);
    CHECK_EQ_INT(add_pl.GetSongIndex(s2), 1);
    add_pl.RemoveSong(s1);
    CHECK_EQ_INT(add_pl.GetPlaylist().size(), 1);

    CHECK(CPlaylistFile::IsPlaylistFile("a.m3u8"));
    CHECK(CPlaylistFile::IsPlaylistFile("a.playlist"));
    CHECK(!CPlaylistFile::IsPlaylistFile("a.mp3"));
    CHECK(CPlaylistFile::TypeFromExtension("a.m3u") == CPlaylistFile::PL_M3U);
}

// ----------------------------------------------------------------- LrcParser

static void TestLrcParser()
{
    std::printf("LrcParser\n");

    // ---- 标准 LRC + 元数据 + 翻译 ----
    const char* lrc =
        "[ti:测试歌曲]\n"
        "[ar:测试歌手]\n"
        "[al:测试专辑]\n"
        "[offset:-500]\n"
        "[00:01.00]第一句 / First line\n"
        "[00:05.50]第二句\n"
        "[00:10.00]第三句\n";

    CLrcParser parser;
    CHECK(parser.ParseString(lrc));
    CHECK_EQ(parser.GetTitle(), u8"测试歌曲");
    CHECK_EQ(parser.GetArtist(), u8"测试歌手");
    CHECK_EQ(parser.GetAlbum(), u8"测试专辑");
    CHECK_EQ_INT(parser.GetOffset(), -500);
    CHECK(parser.HasTranslation());
    CHECK(!parser.IsKaraoke());

    const std::vector<CLrcParser::Lyric>& lyrics = parser.GetLyrics();
    CHECK_EQ_INT(lyrics.size(), 3);
    CHECK_EQ(lyrics[0].text, u8"第一句");
    CHECK_EQ(lyrics[0].translate, "First line");
    // 偏移量已经应用：1000 - 500 = 500
    CHECK_EQ_INT(lyrics[0].time_start, 500);
    CHECK_EQ_INT(lyrics[1].time_start, 5000);
    // 非逐字歌词的行时长取到下一行开始
    CHECK_EQ_INT(lyrics[0].time_span, 4500);

    // 时间定位
    CHECK_EQ_INT(parser.GetLyricIndex(0), -1);
    CHECK_EQ_INT(parser.GetLyricIndex(600), 0);
    CHECK_EQ_INT(parser.GetLyricIndex(5000), 1);
    CHECK_EQ_INT(parser.GetLyricIndex(999999), 2);

    // ---- 压缩 LRC：一行多个时间标签应展开成多句 ----
    CLrcParser compressed;
    CHECK(compressed.ParseString("[00:01.00][00:30.00][01:00.00]副歌\n"));
    CHECK_EQ_INT(compressed.GetLyrics().size(), 3);
    CHECK_EQ_INT(compressed.GetLyrics()[0].time_start, 1000);
    CHECK_EQ_INT(compressed.GetLyrics()[1].time_start, 30000);
    CHECK_EQ_INT(compressed.GetLyrics()[2].time_start, 60000);
    CHECK_EQ(compressed.GetLyrics()[2].text, u8"副歌");

    // ---- 增强 LRC（逐字/卡拉OK）----
    CLrcParser karaoke;
    CHECK(karaoke.ParseString("[00:01.00]<00:01.00>你<00:01.50>好<00:02.00>世界\n[00:10.00]结束\n"));
    CHECK(karaoke.IsKaraoke());
    const CLrcParser::Lyric& k = karaoke.GetLyrics()[0];
    CHECK_EQ(k.text, u8"你好世界");
    CHECK_EQ_INT(k.split.size(), 3);
    CHECK_EQ_INT(k.word_time.size(), 3);
    // split 是字节偏移，"你" 和 "好" 各 3 字节
    CHECK_EQ_INT(k.split[0], 3);
    CHECK_EQ_INT(k.split[1], 6);
    CHECK_EQ_INT(k.split[2], 12);
    // 与桌面版 DisposeLrc 一致：首个 <00:01.00> 会被并入行首时间串，
    // 所以第一段就是“你”（1.00→1.50），最后一段没有显式时长，延续到下一行开始
    CHECK_EQ_INT(k.word_time[0], 500);
    CHECK_EQ_INT(k.word_time[1], 500);
    CHECK_EQ_INT(k.word_time[2], 8000);                     // 2.00 → 下一行 10.00
    // 用 split 的字节偏移切分 UTF-8 必须落在字符边界上
    CHECK_EQ(k.text.substr(0, k.split[0]), u8"你");
    CHECK_EQ(k.text.substr(0, k.split[1]), u8"你好");

    // ---- 毫秒位数兼容：1/2/3 位都要正确 ----
    CLrcParser msec;
    CHECK(msec.ParseString("[00:01.5]a\n[00:02.25]b\n[00:03.125]c\n"));
    CHECK_EQ_INT(msec.GetLyrics()[0].time_start, 1500);
    CHECK_EQ_INT(msec.GetLyrics()[1].time_start, 2250);
    CHECK_EQ_INT(msec.GetLyrics()[2].time_start, 3125);

    // ---- 用冒号分隔毫秒的非标准歌词 ----
    CLrcParser colon;
    CHECK(colon.ParseString("[00:01:50]a\n[00:03:00]b\n"));
    CHECK_EQ_INT(colon.GetLyrics()[0].time_start, 1500);

    // ---- 无时间标签的行不算歌词 ----
    CLrcParser no_tag;
    CHECK(!no_tag.ParseString("[ti:只有元数据]\n这是一行普通文本\n"));
    CHECK(no_tag.IsEmpty());

    // ---- 用户偏移可叠加在文件 offset 之上 ----
    CLrcParser offset_parser;
    offset_parser.ParseString("[00:10.00]a\n[00:20.00]b\n");
    CHECK_EQ_INT(offset_parser.GetLyrics()[0].time_start, 10000);
    offset_parser.SetUserOffset(2000);
    CHECK_EQ_INT(offset_parser.GetLyrics()[0].time_start, 12000);
    offset_parser.SetUserOffset(-3000);
    CHECK_EQ_INT(offset_parser.GetLyrics()[0].time_start, 7000);

    // ---- 演唱进度 ----
    CLrcParser progress;
    progress.ParseString("[00:00.00]a\n[00:10.00]b\n");
    CHECK(progress.GetLyricProgress(5000) > 0.49 && progress.GetLyricProgress(5000) < 0.51);
    CHECK(progress.GetLyricProgress(-100) == 0.0);

    // ---- 空输入不能崩 ----
    CLrcParser empty;
    CHECK(!empty.ParseString(""));
    CHECK(empty.IsEmpty());
    CHECK_EQ_INT(empty.GetLyricIndex(1000), -1);
    CHECK(empty.GetLyricProgress(1000) == 0.0);
}

// -------------------------------------------------------------- MediaScanner

static void TestMediaScanner()
{
    std::printf("MediaScanner\n");

    CHECK(CMediaScanner::IsSupportedAudio("a.mp3"));
    CHECK(CMediaScanner::IsSupportedAudio("a.OGG"));            // 扩展名不区分大小写
    CHECK(CMediaScanner::IsSupportedAudio("a.opus"));
    CHECK(CMediaScanner::IsSupportedAudio("a.wav"));
    CHECK(CMediaScanner::IsSupportedAudio("a.it"));
    CHECK(!CMediaScanner::IsSupportedAudio("a.txt"));
    CHECK(!CMediaScanner::IsSupportedAudio("noext"));
    // devkitPro 的 switch-sdl2_mixer 2.0.4 没编译 FLAC，
    // 但我们用 CFlacDecoder 直接调 libFLAC 补上了这一路，所以 flac 仍是可播放的
    CHECK(CMediaScanner::IsSupportedAudio("a.flac"));
    CHECK(CMediaScanner::IsSupportedAudio("a.FLAC"));

    SongInfo s;
    s.file_path = "sdmc:/music/周杰伦 - 晴天.mp3";
    CMediaScanner::FillTagFromFileName(s);
    CHECK_EQ(s.artist, u8"周杰伦");
    CHECK_EQ(s.title, u8"晴天");

    SongInfo plain;
    plain.file_path = "sdmc:/music/track01.mp3";
    CMediaScanner::FillTagFromFileName(plain);
    CHECK_EQ(plain.title, "track01");
    CHECK_EQ(plain.artist, "");
    CHECK_EQ(plain.GetArtist(), u8"未知艺术家");
}

// -------------------------------------------------------------------- Config

static void TestConfig()
{
    std::printf("Config");

    CConfig config;
    CHECK_EQ_INT(config.GetVolume(), 80);                   // 默认值
    CHECK(config.GetRepeatMode() == CConfig::RM_LOOP_PLAYLIST);
    CHECK_EQ(config.GetMusicDir(), "sdmc:/music");

    config.SetVolume(150);                                  // 应被夹到 100
    CHECK_EQ_INT(config.GetVolume(), 100);
    config.SetVolume(-10);
    CHECK_EQ_INT(config.GetVolume(), 0);

    config.SetRepeatMode(CConfig::RM_LOOP_TRACK);
    CHECK(config.GetRepeatMode() == CConfig::RM_LOOP_TRACK);
    config.SetInt("repeat_mode", 999);                      // 越界值回退到默认
    CHECK(config.GetRepeatMode() == CConfig::RM_LOOP_PLAYLIST);

    config.SetBool("show_translation", false);
    CHECK(!config.GetShowTranslation());
    config.SetString("last_playlist", "sdmc:/playlists/我的歌单.playlist");
    CHECK_EQ(config.GetLastPlaylist(), u8"sdmc:/playlists/我的歌单.playlist");

    std::printf("\n");
}

// -------------------------------------------------------- 文件读写往返（真实 IO）

static void TestFileRoundTrip()
{
    std::printf("File round-trip\n");

    std::string dir = "host_test_tmp";
    std::string playlist_path = dir + "/test.playlist";
    std::string config_path = dir + "/test.ini";

    TestFramework::RemoveTestDir(dir);  // 从干净状态开始，不受上一轮残留影响
    CHECK(FileUtil::CreateDirRecursive(dir));

    CPathMapper mapper;
    mapper.AddRule("D:\\Music\\", "sdmc:/music/");

    // 写入 -> 读回，路径应能经由 PathMapper 双向还原
    CPlaylistFile out(&mapper);
    std::vector<SongInfo> songs;
    SongInfo a; a.file_path = "sdmc:/music/a.mp3"; a.title = u8"甲"; a.artist = "A";
    SongInfo b; b.file_path = "sdmc:/music/sub/b.flac";
    songs.push_back(a);
    songs.push_back(b);
    out.SetPlaylist(songs);
    CHECK(out.SaveToFile(playlist_path, CPlaylistFile::PL_PLAYLIST));

    CPlaylistFile in(&mapper);
    CHECK(in.LoadFromFile(playlist_path));
    CHECK_EQ_INT(in.GetPlaylist().size(), 2);
    CHECK_EQ(in.GetPlaylist()[0].file_path, "sdmc:/music/a.mp3");
    CHECK_EQ(in.GetPlaylist()[1].file_path, "sdmc:/music/sub/b.flac");

    // 磁盘上存的应该是 Windows 形式的路径，桌面版才能直接打开
    std::string raw;
    CHECK(FileUtil::ReadAll(playlist_path, raw));
    CHECK(raw.find("d:\\music\\a.mp3") != std::string::npos);

    // m3u8 往返
    std::string m3u_path = dir + "/test.m3u8";
    CHECK(out.SaveToFile(m3u_path, CPlaylistFile::PL_M3U8));
    CPlaylistFile m3u_in(&mapper);
    CHECK(m3u_in.LoadFromFile(m3u_path));
    CHECK_EQ_INT(m3u_in.GetPlaylist().size(), 2);
    CHECK_EQ(m3u_in.GetPlaylist()[0].file_path, "sdmc:/music/a.mp3");

    // 配置往返
    CConfig save_config;
    save_config.SetVolume(55);
    save_config.SetRepeatMode(CConfig::RM_PLAY_SHUFFLE);
    save_config.SetLastPlaylist(u8"sdmc:/歌单.playlist");
    CHECK(save_config.Save(config_path));

    CConfig load_config;
    CHECK(load_config.Load(config_path));
    CHECK_EQ_INT(load_config.GetVolume(), 55);
    CHECK(load_config.GetRepeatMode() == CConfig::RM_PLAY_SHUFFLE);
    CHECK_EQ(load_config.GetLastPlaylist(), u8"sdmc:/歌单.playlist");

    // 不存在的文件不能崩，返回 false
    CConfig missing;
    CHECK(!missing.Load(dir + "/does_not_exist.ini"));

    // 目录扫描。扫描目录与歌词目录刻意分开，这样重复运行测试的结果是幂等的
    // （否则上一轮留下的文件会让计数变多）
    std::string scan_dir = dir + "/scan";
    CHECK(FileUtil::WriteAll(scan_dir + "/x.mp3", "fake"));
    CHECK(FileUtil::WriteAll(scan_dir + "/readme.txt", "not audio"));
    CHECK(FileUtil::WriteAll(scan_dir + "/nested/y.ogg", "fake"));
    std::vector<SongInfo> scanned;
    CMediaScanner::ScanDirectory(scan_dir, scanned);
    CHECK_EQ_INT(scanned.size(), 2);

    // 限制深度后不应递归进子目录
    std::vector<SongInfo> shallow;
    CMediaScanner::ScanDirectory(scan_dir, shallow, 0);
    CHECK_EQ_INT(shallow.size(), 1);

    // 扫描不存在的目录不能崩
    std::vector<SongInfo> nothing;
    CMediaScanner::ScanDirectory(dir + "/no_such_dir", nothing);
    CHECK_EQ_INT(nothing.size(), 0);

    // 歌词文件查找
    std::string lyric_dir = dir + "/lyric";
    CHECK(FileUtil::WriteAll(lyric_dir + "/song.mp3", "fake"));
    CHECK(FileUtil::WriteAll(lyric_dir + "/song.lrc", "[00:01.00]hi\n"));
    CHECK_EQ(CLrcParser::FindLyricFile(lyric_dir + "/song.mp3"), lyric_dir + "/song.lrc");
    CHECK_EQ(CLrcParser::FindLyricFile(scan_dir + "/x.mp3"), "");

    CLrcParser from_file;
    CHECK(from_file.ParseFile(lyric_dir + "/song.lrc"));
    CHECK_EQ(from_file.GetLyrics()[0].text, "hi");

    CLrcParser missing_lyric;
    CHECK(!missing_lyric.ParseFile(lyric_dir + "/no_such.lrc"));
}

int main()
{
    // 关掉缓冲：测试崩溃时也能看到已经跑到哪一步
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("=== MusicPlayer2 for Switch - 核心层测试 ===\n\n");

    TestStringUtil();
    TestPlayTime();
    TestFileUtil();
    TestPathMapper();
    TestPlaylistFile();
    TestLrcParser();
    TestMediaScanner();
    TestConfig();
    TestFileRoundTrip();

    std::printf("\n--- 音频 ---\n");
    RunAudioTests();
    RunDurationTests();
    RunTagWriteTests();

    std::printf("\n--- 在线下载 ---\n");
    RunTagTests();
    RunNetTests();

    const int total = TestFramework::g_total;
    const int failed = TestFramework::g_failed;
    std::printf("\n=== %d/%d 通过 ===\n", total - failed, total);
    if (failed > 0)
        std::printf("*** %d 个断言失败 ***\n", failed);
    return failed == 0 ? 0 : 1;
}
