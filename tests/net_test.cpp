// 在线下载相关模块的主机端测试。
// JSON 解析、URL 构造、搜索结果解析、匹配算法、下载流程编排全部与平台无关，
// 因此可以在开发机上用固定的响应样本完整验证，不需要真机也不需要联网。

#include "TestFramework.h"
#include "../source/core/VersionUtil.h"
#include "../source/net/ReleaseInfo.h"
#include "github_release_fixture.h"

#include "../source/core/FileUtil.h"
#include "../source/core/LrcParser.h"
#include "../source/net/DownloadManager.h"
#include "../source/net/HttpClient.h"
#include "../source/net/Json.h"
#include "../source/net/LyricProvider.h"
#include "../source/net/SongMatcher.h"
#include "../source/net/UrlUtil.h"

#include <chrono>
#include <map>
#include <thread>

// ---------------------------------------------------------------------- JSON

static void TestJson()
{
    std::printf("Json\n");

    JsonValue root;

    // 基本类型
    CHECK(JsonValue::Parse(R"({"a":1,"b":"text","c":true,"d":null,"e":[1,2,3]})", root));
    CHECK(root.IsObject());
    CHECK_EQ_INT(root.GetInt("a"), 1);
    CHECK_EQ(root.GetString("b"), "text");
    CHECK(root.GetBool("c"));
    CHECK(root["d"].IsNull());
    CHECK(root["e"].IsArray());
    CHECK_EQ_INT(root["e"].Size(), 3);
    CHECK_EQ_INT(root["e"][1].AsInt(), 2);

    // 缺失的键必须能安全链式访问而不崩溃
    CHECK(root["nope"].IsNull());
    CHECK(root["nope"]["deeper"][3]["x"].IsNull());
    CHECK_EQ(root["nope"].AsString("默认值"), "默认值");
    CHECK_EQ_INT(root["nope"].AsInt(-1), -1);
    CHECK(!root.Has("nope"));
    CHECK(root.Has("a"));

    // 类型不符时取默认值，不做隐式转换
    CHECK_EQ(root["a"].AsString("非字符串"), "非字符串");
    CHECK_EQ_INT(root["b"].AsInt(-1), -1);

    // 数字
    CHECK(JsonValue::Parse(R"({"i":-42,"f":3.5,"e":1.2e3,"big":1234567890123})", root));
    CHECK_EQ_INT(root.GetInt("i"), -42);
    CHECK_NEAR(root.GetNumber("f"), 3.5, 1e-9);
    CHECK_NEAR(root.GetNumber("e"), 1200.0, 1e-9);
    CHECK_EQ_INT(root.GetInt("big"), 1234567890123LL);

    // 转义
    CHECK(JsonValue::Parse(R"({"s":"line1\nline2\t\"quoted\"\\slash\/"})", root));
    CHECK_EQ(root.GetString("s"), "line1\nline2\t\"quoted\"\\slash/");

    // \u 转义：BMP 与代理对
    CHECK(JsonValue::Parse(R"({"cn":"\u4e2d\u6587","emoji":"\ud83c\udfb5"})", root));
    CHECK_EQ(root.GetString("cn"), u8"中文");
    CHECK_EQ(root.GetString("emoji"), u8"\U0001F3B5");

    // 落单的代理项要退化成替换字符而不是产生非法 UTF-8
    CHECK(JsonValue::Parse(R"({"bad":"\ud83c"})", root));
    CHECK_EQ(root.GetString("bad"), u8"\uFFFD");

    // 原始 UTF-8 直接透传
    CHECK(JsonValue::Parse(u8R"({"cn":"周杰伦"})", root));
    CHECK_EQ(root.GetString("cn"), u8"周杰伦");

    // 空容器
    CHECK(JsonValue::Parse("{}", root));
    CHECK_EQ_INT(root.Size(), 0);
    CHECK(JsonValue::Parse("[]", root));
    CHECK(root.IsArray());

    // 顶层非对象
    CHECK(JsonValue::Parse("[1,[2,[3]]]", root));
    CHECK_EQ_INT(root[1][1][0].AsInt(), 3);

    // BOM 要被忽略
    CHECK(JsonValue::Parse("\xEF\xBB\xBF{\"a\":1}", root));
    CHECK_EQ_INT(root.GetInt("a"), 1);

    // 非法输入必须失败而不是崩溃
    std::string error;
    CHECK(!JsonValue::Parse("", root, &error));
    CHECK(!error.empty());
    CHECK(!JsonValue::Parse("{", root));
    CHECK(!JsonValue::Parse("{\"a\":}", root));
    CHECK(!JsonValue::Parse("{\"a\" 1}", root));
    CHECK(!JsonValue::Parse("{a:1}", root));
    CHECK(!JsonValue::Parse("[1,2", root));
    CHECK(!JsonValue::Parse("tru", root));
    CHECK(!JsonValue::Parse("{\"a\":1}garbage", root));
    CHECK(!JsonValue::Parse("\"unterminated", root));
    // 解析失败后必须是干净的 null，不能留下半个结果
    CHECK(root.IsNull());

    // 深度限制：超过上限要失败而不是爆栈
    std::string deep;
    for (int i = 0; i < 200; ++i)
        deep += "[";
    for (int i = 0; i < 200; ++i)
        deep += "]";
    CHECK(!JsonValue::Parse(deep, root));

    // 刚好在限制内的嵌套应当成功
    std::string ok_deep;
    for (int i = 0; i < 30; ++i)
        ok_deep += "[";
    ok_deep += "1";
    for (int i = 0; i < 30; ++i)
        ok_deep += "]";
    CHECK(JsonValue::Parse(ok_deep, root));
}

// ------------------------------------------------------------------- UrlUtil

static void TestUrlUtil()
{
    std::printf("UrlUtil\n");

    CHECK_EQ(UrlUtil::Encode("abc123-_.~"), "abc123-_.~");
    CHECK_EQ(UrlUtil::Encode("a b"), "a%20b");
    CHECK_EQ(UrlUtil::Encode("a&b=c"), "a%26b%3Dc");
    // 中文按 UTF-8 逐字节编码
    CHECK_EQ(UrlUtil::Encode(u8"中"), "%E4%B8%AD");
    CHECK_EQ(UrlUtil::Encode(""), "");

    CHECK_EQ(UrlUtil::GetExtensionFromUrl("http://a.com/x/cover.JPG"), "jpg");
    CHECK_EQ(UrlUtil::GetExtensionFromUrl("http://a.com/cover.png?param=1&x=2"), "png");
    CHECK_EQ(UrlUtil::GetExtensionFromUrl("http://a.com/cover.jpg#frag"), "jpg");
    CHECK_EQ(UrlUtil::GetExtensionFromUrl("http://a.com/noext"), "");
    // 路径里带点但不是扩展名
    CHECK_EQ(UrlUtil::GetExtensionFromUrl("http://a.com/v1.2/file"), "");
    CHECK_EQ(UrlUtil::GetExtensionFromUrl("http://a.com/x.verylongext"), "");

    CHECK(UrlUtil::IsHttps("https://a.com"));
    CHECK(UrlUtil::IsHttps("HTTPS://a.com"));
    CHECK(!UrlUtil::IsHttps("http://a.com"));
}

// --------------------------------------------------------------- SongMatcher

static void TestSongMatcher()
{
    std::printf("SongMatcher\n");

    CHECK_NEAR(SongMatcher::CharacterSimilarDegree(U'a', U'a'), 1.0, 1e-9);
    CHECK_NEAR(SongMatcher::CharacterSimilarDegree(U'a', U'A'), 0.8, 1e-9);
    CHECK_NEAR(SongMatcher::CharacterSimilarDegree(U'A', U'a'), 0.8, 1e-9);
    CHECK_NEAR(SongMatcher::CharacterSimilarDegree(U'1', U'一'), 0.7, 1e-9);
    CHECK_NEAR(SongMatcher::CharacterSimilarDegree(U'九', U'9'), 0.7, 1e-9);
    CHECK_NEAR(SongMatcher::CharacterSimilarDegree(U'a', U'b'), 0.0, 1e-9);

    // 完全相同
    CHECK_NEAR(SongMatcher::StringSimilarDegree("hello", "hello"), 1.0, 1e-9);
    // 空串
    CHECK_NEAR(SongMatcher::StringSimilarDegree("", "hello"), 0.0, 1e-9);
    CHECK_NEAR(SongMatcher::StringSimilarDegree("hello", ""), 0.0, 1e-9);
    // 完全不同
    CHECK(SongMatcher::StringSimilarDegree("abc", "xyz") < 0.1);
    // 相近
    CHECK(SongMatcher::StringSimilarDegree("hello", "hallo") > 0.7);

    // 中文必须按码点比较：三个字里错一个应当约为 2/3，
    // 若按字节算（3 字节一个汉字）结果会严重偏低
    double cn = SongMatcher::StringSimilarDegree(u8"晴天了", u8"晴天啊");
    CHECK_NEAR(cn, 2.0 / 3.0, 0.01);

    // 中英文混排
    CHECK(SongMatcher::StringSimilarDegree(u8"周杰伦", u8"周杰倫") > 0.6);

    // 超长串直接返回 0（与桌面版一致）
    std::string very_long(300, 'a');
    CHECK_NEAR(SongMatcher::StringSimilarDegree(very_long, very_long), 0.0, 1e-9);

    // ---- SelectMatchedItem ----
    std::vector<DownloadItem> list;
    DownloadItem a; a.title = u8"晴天"; a.artist = u8"周杰伦"; a.album = u8"叶惠美";
    DownloadItem b; b.title = u8"完全不相干的歌"; b.artist = u8"某某"; b.album = u8"某专辑";
    DownloadItem c; c.title = u8"晴天 (Live)"; c.artist = u8"周杰伦"; c.album = u8"演唱会";
    list.push_back(b);      // 故意把不匹配的放前面，验证不是简单取第一项
    list.push_back(a);
    list.push_back(c);

    int matched = SongMatcher::SelectMatchedItem(list, u8"晴天", u8"周杰伦", u8"叶惠美",
                                                 u8"周杰伦 - 晴天");
    CHECK_EQ_INT(matched, 1);

    // 空列表
    CHECK_EQ_INT(SongMatcher::SelectMatchedItem({}, "a", "b", "c", "d"), -1);

    // 全都不匹配时返回 -1
    std::vector<DownloadItem> unrelated;
    DownloadItem u1; u1.title = "zzzzzzzz"; u1.artist = "yyyyyyyy"; u1.album = "xxxxxxxx";
    unrelated.push_back(u1);
    CHECK_EQ_INT(SongMatcher::SelectMatchedItem(unrelated, "aaaaaaaa", "bbbbbbbb",
                                                "cccccccc", "dddddddd"), -1);

    CHECK_EQ(a.GetDisplayName(), u8"周杰伦 - 晴天");
    DownloadItem no_artist; no_artist.title = "Solo";
    CHECK_EQ(no_artist.GetDisplayName(), "Solo");
}

// -------------------------------------------------------- 网易云 / QQ 响应样本

static const char* kNeteaseSearchResponse = u8R"({
  "result": {
    "songs": [
      {
        "id": 186016,
        "name": "晴天",
        "duration": 269000,
        "album": { "id": 18877, "name": "叶惠美" },
        "artists": [ { "id": 6452, "name": "周杰伦" } ]
      },
      {
        "id": 999,
        "name": "合唱曲",
        "duration": 200000,
        "album": { "name": "合辑" },
        "artists": [ { "name": "歌手A" }, { "name": "歌手B" } ]
      }
    ]
  },
  "code": 200
})";

static const char* kNeteaseLyricResponse = u8R"({
  "lrc":    { "lyric": "[00:01.00]故事的小黄花\n[00:05.00]从出生那年就飘着\n" },
  "tlyric": { "lyric": "[00:01.00]The little yellow flower\n[00:05.00]Has been floating\n" },
  "code": 200
})";

static const char* kNeteaseDetailResponse = u8R"({
  "songs": [
    { "id": 186016, "album": { "name": "叶惠美", "picUrl": "http://p1.music.126.net/abc.jpg" } }
  ],
  "code": 200
})";

static const char* kQQSearchResponse = u8R"({
  "code": 0,
  "data": {
    "song": {
      "list": [
        {
          "songmid": "001Qu4I30eVFYb",
          "songname": "晴天",
          "albumname": "叶惠美",
          "interval": 269,
          "cdIdx": 0,
          "singer": [ { "name": "周杰伦" } ]
        },
        {
          "songmid": "002abc",
          "songname": "对唱",
          "albumname": "合辑",
          "interval": 200,
          "cdIdx": -1,
          "singer": [ { "name": "甲" }, { "name": "乙" } ]
        }
      ]
    }
  }
})";

static const char* kQQLyricResponse = u8R"({
  "retcode": 0,
  "lyric": "[00:01.00]故事的小黄花\n",
  "trans": "[00:01.00]The little yellow flower\n"
})";

static const char* kQQSongResponse = u8R"({
  "data": [ { "album": { "mid": "000MkMni19ClKG", "name": "叶惠美" } } ]
})";

// ------------------------------------------------------------- LyricProvider

static void TestNeteaseProvider()
{
    std::printf("CNeteaseProvider\n");
    CNeteaseProvider provider;

    CHECK_EQ(std::string(provider.GetName()), u8"网易云音乐");
    CHECK(provider.SearchUsesPost());

    // 搜索 URL：关键词要被百分号编码
    std::string url = provider.GetSearchUrl(u8"周杰伦 晴天", 20);
    CHECK(url.find("music.163.com/api/search/get/") != std::string::npos);
    CHECK(url.find("%E5%91%A8%E6%9D%B0%E4%BC%A6%20") != std::string::npos);
    CHECK(url.find("limit=20") != std::string::npos);

    // 搜索结果解析
    std::vector<DownloadItem> items;
    provider.ParseSearchResult(kNeteaseSearchResponse, items);
    CHECK_EQ_INT(items.size(), 2);
    CHECK_EQ(items[0].id, "186016");
    CHECK_EQ(items[0].title, u8"晴天");
    CHECK_EQ(items[0].artist, u8"周杰伦");
    CHECK_EQ(items[0].album, u8"叶惠美");
    CHECK_EQ_INT(items[0].duration, 269000);
    // 多个艺术家用 '/' 连接
    CHECK_EQ(items[1].artist, u8"歌手A/歌手B");

    // 歌词 URL：带翻译和不带翻译走不同接口
    CHECK(provider.GetLyricUrl("186016", true).find("song/lyric") != std::string::npos);
    CHECK(provider.GetLyricUrl("186016", false).find("song/media") != std::string::npos);

    // 歌词解析：翻译作为独立 LRC 追加在后面
    std::string lyric;
    CHECK(provider.ParseLyric(kNeteaseLyricResponse, true, lyric));
    CHECK(lyric.find(u8"故事的小黄花") != std::string::npos);
    CHECK(lyric.find("The little yellow flower") != std::string::npos);

    // 不要翻译时不应包含译文
    CHECK(provider.ParseLyric(kNeteaseLyricResponse, false, lyric));
    CHECK(lyric.find(u8"故事的小黄花") != std::string::npos);
    CHECK(lyric.find("The little yellow flower") == std::string::npos);

    // song/media 的扁平结构也要认
    CHECK(provider.ParseLyric(u8R"({"lyric":"[00:01.00]单层结构\n"})", false, lyric));
    CHECK(lyric.find(u8"单层结构") != std::string::npos);

    // 封面
    std::string cover_info = provider.GetCoverInfoUrl("186016");
    CHECK(cover_info.find("song/detail") != std::string::npos);
    CHECK(cover_info.find("%5B186016%5D") != std::string::npos);
    CHECK_EQ(provider.ParseCoverUrl(kNeteaseDetailResponse), "http://p1.music.126.net/abc.jpg");
    CHECK_EQ(provider.GetCoverInfoUrl(""), "");
}

static void TestQQProvider()
{
    std::printf("CQQMusicProvider\n");
    CQQMusicProvider provider;

    CHECK_EQ(std::string(provider.GetName()), u8"QQ音乐");
    CHECK(!provider.SearchUsesPost());
    // QQ 的接口不带 Referer 会返回空数据
    CHECK(!provider.GetExtraHeaders().empty());

    std::string url = provider.GetSearchUrl(u8"晴天", 20);
    CHECK(url.find("c.y.qq.com/soso/fcgi-bin/client_search_cp") != std::string::npos);
    CHECK(url.find("n=20") != std::string::npos);
    CHECK(url.find("format=json") != std::string::npos);

    std::vector<DownloadItem> items;
    provider.ParseSearchResult(kQQSearchResponse, items);
    CHECK_EQ_INT(items.size(), 2);
    CHECK_EQ(items[0].id, "001Qu4I30eVFYb");
    CHECK_EQ(items[0].title, u8"晴天");
    CHECK_EQ(items[0].artist, u8"周杰伦");
    CHECK_EQ(items[0].album, u8"叶惠美");
    CHECK_EQ_INT(items[0].duration, 269000);        // interval 是秒，要乘 1000
    CHECK_EQ_INT(items[0].track, 0);
    // 多个歌手用 ';' 连接；cdIdx 为负时不写入 track
    CHECK_EQ(items[1].artist, u8"甲;乙");
    CHECK_EQ_INT(items[1].track, 0);

    std::string lyric;
    CHECK(provider.ParseLyric(kQQLyricResponse, true, lyric));
    CHECK(lyric.find(u8"故事的小黄花") != std::string::npos);
    CHECK(lyric.find("The little yellow flower") != std::string::npos);
    CHECK(provider.ParseLyric(kQQLyricResponse, false, lyric));
    CHECK(lyric.find("The little yellow flower") == std::string::npos);

    // 封面要经过 album.mid 拼接
    CHECK(provider.GetCoverInfoUrl("001Qu4I30eVFYb").find("fcg_play_single_song") != std::string::npos);
    CHECK_EQ(provider.ParseCoverUrl(kQQSongResponse),
             "http://y.gtimg.cn/music/photo_new/T002R800x800M000000MkMni19ClKG.jpg");
}

static void TestProviderRobustness()
{
    std::printf("Provider 容错\n");

    CNeteaseProvider netease;
    CQQMusicProvider qq;
    std::vector<DownloadItem> items;
    std::string lyric;

    // 空响应、非 JSON、结构不符都不能崩，只能返回空结果
    const char* bad_inputs[] = { "", "not json", "{}", "[]", R"({"result":null})",
                                 R"({"result":{"songs":"不是数组"}})", R"({"data":{}})" };
    for (const char* bad : bad_inputs)
    {
        netease.ParseSearchResult(bad, items);
        CHECK_EQ_INT(items.size(), 0);
        qq.ParseSearchResult(bad, items);
        CHECK_EQ_INT(items.size(), 0);
        CHECK(!netease.ParseLyric(bad, true, lyric));
        CHECK(!qq.ParseLyric(bad, true, lyric));
        CHECK_EQ(netease.ParseCoverUrl(bad), "");
        CHECK_EQ(qq.ParseCoverUrl(bad), "");
    }

    // 缺少 id / songmid 的条目要被跳过
    netease.ParseSearchResult(R"({"result":{"songs":[{"name":"没有id"}]}})", items);
    CHECK_EQ_INT(items.size(), 0);
    qq.ParseSearchResult(R"({"data":{"song":{"list":[{"songname":"没有mid"}]}}})", items);
    CHECK_EQ_INT(items.size(), 0);

    // 空歌词视为失败
    CHECK(!netease.ParseLyric(R"({"lrc":{"lyric":""}})", true, lyric));
}

static void TestLyricProviderUtil()
{
    std::printf("LyricProviderUtil\n");

    // AddLyricTag：没有标签时补齐
    std::string lyric = "[00:01.00]歌词\n";
    LyricProviderUtil::AddLyricTag(lyric, "186016", u8"晴天", u8"周杰伦", u8"叶惠美");
    CHECK(lyric.find("[id:186016]") != std::string::npos);
    CHECK(lyric.find(u8"[ti:晴天]") != std::string::npos);
    CHECK(lyric.find(u8"[ar:周杰伦]") != std::string::npos);
    CHECK(lyric.find(u8"[al:叶惠美]") != std::string::npos);
    // 标签必须在歌词正文之前
    CHECK(lyric.find("[id:") < lyric.find("[00:01.00]"));

    // 已有非空标签时不重复添加
    std::string with_tag = u8"[ti:原有标题]\n[00:01.00]歌词\n";
    LyricProviderUtil::AddLyricTag(with_tag, "1", u8"新标题", "", "");
    CHECK(with_tag.find(u8"[ti:新标题]") == std::string::npos);
    CHECK(with_tag.find(u8"[ti:原有标题]") != std::string::npos);

    // 空标签 "[ti:]" 视为不存在，应当被补上
    std::string empty_tag = "[ti:]\n[00:01.00]歌词\n";
    LyricProviderUtil::AddLyricTag(empty_tag, "1", u8"标题", "", "");
    CHECK(empty_tag.find(u8"[ti:标题]") != std::string::npos);

    // 空值不生成标签
    std::string no_value = "[00:01.00]歌词\n";
    LyricProviderUtil::AddLyricTag(no_value, "", "", "", "");
    CHECK_EQ(no_value, "[00:01.00]歌词\n");

    // StripJsonp
    CHECK_EQ(LyricProviderUtil::StripJsonp(R"(callback({"a":1}))"), R"({"a":1})");
    CHECK_EQ(LyricProviderUtil::StripJsonp(R"(MusicJsonCallback({"a":1}))"), R"({"a":1})");
    CHECK_EQ(LyricProviderUtil::StripJsonp(R"({"a":1})"), R"({"a":1})");
    CHECK_EQ(LyricProviderUtil::StripJsonp("  [1,2]  "), "[1,2]");
    CHECK_EQ(LyricProviderUtil::StripJsonp(""), "");
    // 前缀不是合法函数名时原样返回，避免误伤
    CHECK_EQ(LyricProviderUtil::StripJsonp("some text (not jsonp)"), "some text (not jsonp)");

    // MakeSearchKeyword
    CHECK_EQ(LyricProviderUtil::MakeSearchKeyword(u8"晴天", u8"周杰伦", "file"), u8"周杰伦 晴天");
    CHECK_EQ(LyricProviderUtil::MakeSearchKeyword(u8"晴天", "", "file"), u8"晴天");
    CHECK_EQ(LyricProviderUtil::MakeSearchKeyword("", u8"周杰伦", "file"), u8"周杰伦");
    CHECK_EQ(LyricProviderUtil::MakeSearchKeyword("", "", u8"周杰伦 - 晴天"), u8"周杰伦 - 晴天");
}

// --------------------------------------------------- DownloadManager（假网络）

// 用固定的响应表模拟服务器：按 URL 的子串匹配
class FakeHttpClient : public IHttpClient
{
public:
    // 按顺序检查，第一个能在 URL 中找到的子串决定返回内容
    std::vector<std::pair<std::string, std::string>> routes;
    std::string binary_payload;
    bool fail_all{};
    bool fail_binary{};
    int get_count{};
    int post_count{};
    std::vector<std::string> requested_urls;

    bool Get(const std::string& url, const std::vector<std::string>& headers,
             HttpResponse& out) override
    {
        (void)headers;
        ++get_count;
        return Respond(url, out);
    }

    bool Post(const std::string& url, const std::string& body,
              const std::vector<std::string>& headers, HttpResponse& out) override
    {
        (void)body;
        (void)headers;
        ++post_count;
        return Respond(url, out);
    }

    bool GetBinary(const std::string& url, const std::vector<std::string>& headers,
                   std::string& out, std::string& error) override
    {
        (void)headers;
        requested_urls.push_back(url);
        if (fail_all || fail_binary)
        {
            error = "模拟的网络错误";
            out.clear();
            return false;
        }
        out = binary_payload;
        return true;
    }

private:
    bool Respond(const std::string& url, HttpResponse& out)
    {
        requested_urls.push_back(url);
        out = HttpResponse();
        if (fail_all)
        {
            out.error = "模拟的网络错误";
            return false;
        }
        for (const auto& route : routes)
        {
            if (url.find(route.first) != std::string::npos)
            {
                out.success = true;
                out.status_code = 200;
                out.body = route.second;
                return true;
            }
        }
        out.status_code = 404;
        out.error = "模拟的 404";
        return false;
    }
};

// 等待后台任务结束，超时返回 false
static bool WaitIdle(CDownloadManager& manager, int timeout_ms = 5000)
{
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 10)
    {
        if (!manager.IsBusy())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

static void TestDownloadManager()
{
    std::printf("CDownloadManager\n");

    const std::string dir = "net_test_tmp";
    TestFramework::RemoveTestDir(dir);  // 从干净状态开始，不受上一轮残留影响
    CHECK(FileUtil::CreateDirRecursive(dir));
    const std::string audio = dir + "/周杰伦 - 晴天.mp3";
    CHECK(FileUtil::WriteAll(audio, "fake audio"));

    // 保存路径推导
    CHECK_EQ(CDownloadManager::GetLyricSavePath(audio), dir + "/周杰伦 - 晴天.lrc");
    CHECK_EQ(CDownloadManager::GetCoverSavePath(audio, "http://x/a.png"), dir + "/周杰伦 - 晴天.png");
    CHECK_EQ(CDownloadManager::GetCoverSavePath(audio, "http://x/a.jpg"), dir + "/周杰伦 - 晴天.jpg");
    // 拿不到扩展名时兜底为 jpg
    CHECK_EQ(CDownloadManager::GetCoverSavePath(audio, "http://x/noext"), dir + "/周杰伦 - 晴天.jpg");

    FakeHttpClient http;
    http.routes = {
        { "search/get",   kNeteaseSearchResponse },
        { "song/lyric",   kNeteaseLyricResponse },
        { "song/detail",  kNeteaseDetailResponse },
    };
    http.binary_payload = std::string(4096, '\xAB');     // 假装是图片数据

    CDownloadManager manager;
    manager.SetHttpClient(&http);
    manager.SetProvider(CDownloadManager::PROVIDER_NETEASE);
    CHECK_EQ(std::string(manager.GetProviderName()), u8"网易云音乐");
    CHECK(!manager.IsBusy());
    CHECK(manager.Poll().state == CDownloadManager::ST_IDLE);

    CDownloadManager::AutoRequest request;
    request.audio_file_path = audio;
    request.title = u8"晴天";
    request.artist = u8"周杰伦";
    request.album = u8"叶惠美";
    request.download_lyric = true;
    request.download_cover = true;
    request.with_translation = true;

    // ---- 完整的自动下载流程 ----
    CHECK(manager.StartAutoDownload(u8"周杰伦 晴天", request));
    CHECK(WaitIdle(manager));

    CDownloadManager::Status status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_SUCCESS);
    CHECK_EQ_INT(status.results.size(), 2);
    CHECK_EQ_INT(status.matched_index, 0);
    CHECK_EQ(status.saved_lyric_path, dir + "/周杰伦 - 晴天.lrc");
    CHECK_EQ(status.saved_cover_path, dir + "/周杰伦 - 晴天.jpg");
    // 网易云的搜索必须走 POST
    CHECK_EQ_INT(http.post_count, 1);

    // 落盘的歌词要带上标签，并且原文和译文都在
    std::string saved_lyric;
    CHECK(FileUtil::ReadAll(status.saved_lyric_path, saved_lyric));
    CHECK(saved_lyric.find("[id:186016]") != std::string::npos);
    CHECK(saved_lyric.find(u8"[ti:晴天]") != std::string::npos);
    CHECK(saved_lyric.find(u8"故事的小黄花") != std::string::npos);
    CHECK(saved_lyric.find("The little yellow flower") != std::string::npos);

    std::string saved_cover;
    CHECK(FileUtil::ReadAll(status.saved_cover_path, saved_cover));
    CHECK_EQ_INT(saved_cover.size(), 4096);

    // 下载下来的歌词必须能被播放器自己的解析器读回去
    CLrcParser parser;
    CHECK(parser.ParseFile(status.saved_lyric_path));
    CHECK(!parser.IsEmpty());
    CHECK_EQ(parser.GetTitle(), u8"晴天");
    CHECK_EQ(parser.GetArtist(), u8"周杰伦");
    // 同时间轴的译文应当被合并成翻译行，而不是当成两句歌词
    CHECK(parser.HasTranslation());
    if (!parser.IsEmpty())      // 前面的断言失败时不要再索引空容器，否则整个测试进程会崩
    {
        CHECK_EQ(parser.GetLyrics()[0].text, u8"故事的小黄花");
        CHECK_EQ(parser.GetLyrics()[0].translate, "The little yellow flower");
    }

    // ---- 手动搜索 + 指定项下载 ----
    manager.Reset();
    CHECK(manager.Poll().state == CDownloadManager::ST_IDLE);
    CHECK(manager.StartSearch(u8"晴天"));
    CHECK(WaitIdle(manager));
    status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_SEARCH_DONE);
    CHECK_EQ_INT(status.results.size(), 2);

    CDownloadManager::AutoRequest lyric_only = request;
    lyric_only.download_cover = false;
    CHECK(manager.StartDownloadSelected(1, lyric_only));
    CHECK(WaitIdle(manager));
    status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_SUCCESS);
    CHECK_EQ_INT(status.matched_index, 1);
    CHECK(status.saved_cover_path.empty());         // 没要封面就不该有封面路径

    // 越界下标要被拒绝
    CHECK(!manager.StartDownloadSelected(99, request));
    CHECK(!manager.StartDownloadSelected(-1, request));

    // ---- 失败路径 ----
    manager.Reset();
    http.fail_all = true;
    CHECK(manager.StartSearch(u8"晴天"));
    CHECK(WaitIdle(manager));
    status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_FAILED);
    CHECK(!status.message.empty());
    CHECK(status.results.empty());

    // 搜索有结果但歌词接口 404
    manager.Reset();
    http.fail_all = false;
    http.routes = { { "search/get", kNeteaseSearchResponse } };      // 歌词/详情都会 404
    CHECK(manager.StartAutoDownload(u8"周杰伦 晴天", request));
    CHECK(WaitIdle(manager));
    status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_FAILED);

    // 搜索返回空列表
    manager.Reset();
    http.routes = { { "search/get", R"({"result":{"songs":[]}})" } };
    CHECK(manager.StartAutoDownload(u8"不存在的歌", request));
    CHECK(WaitIdle(manager));
    CHECK(manager.Poll().state == CDownloadManager::ST_FAILED);

    // 自动匹配不到足够相似的项
    manager.Reset();
    http.routes = { { "search/get",
                      R"({"result":{"songs":[{"id":1,"name":"zzzzzzzzzz",
                          "album":{"name":"yyyyyyyyyy"},"artists":[{"name":"xxxxxxxxxx"}]}]}})" } };
    CDownloadManager::AutoRequest mismatched = request;
    mismatched.title = "aaaaaaaaaa";
    mismatched.artist = "bbbbbbbbbb";
    mismatched.album = "cccccccccc";
    mismatched.audio_file_path = dir + "/dddddddddd.mp3";
    CHECK(manager.StartAutoDownload("aaaaaaaaaa", mismatched));
    CHECK(WaitIdle(manager));
    status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_FAILED);
    CHECK_EQ_INT(status.matched_index, -1);

    // 封面数据过小时视为无效，不应写出坏文件
    manager.Reset();
    http.routes = {
        { "search/get",  kNeteaseSearchResponse },
        { "song/detail", kNeteaseDetailResponse },
    };
    http.binary_payload = "tiny";
    CDownloadManager::AutoRequest cover_only = request;
    cover_only.download_lyric = false;
    cover_only.audio_file_path = dir + "/cover_probe.mp3";
    CHECK(manager.StartAutoDownload(u8"周杰伦 晴天", cover_only));
    CHECK(WaitIdle(manager));
    CHECK(manager.Poll().state == CDownloadManager::ST_FAILED);
    CHECK(!FileUtil::Exists(dir + "/cover_probe.jpg"));

    // 只要歌词、且歌词失败时，整体必须判失败。
    // 回归用例：曾经因为把"没有请求封面"当成"封面下载成功"而误报成功。
    manager.Reset();
    http.routes = { { "search/get", kNeteaseSearchResponse } };      // 歌词接口会 404
    CDownloadManager::AutoRequest lyric_probe = request;
    lyric_probe.download_cover = false;
    lyric_probe.audio_file_path = dir + "/lyric_probe.mp3";
    CHECK(manager.StartAutoDownload(u8"周杰伦 晴天", lyric_probe));
    CHECK(WaitIdle(manager));
    CHECK(manager.Poll().state == CDownloadManager::ST_FAILED);
    CHECK(!FileUtil::Exists(dir + "/lyric_probe.lrc"));

    // 反过来：只要封面且封面成功时，不应因为"没请求歌词"而报出歌词成功
    manager.Reset();
    http.routes = {
        { "search/get",  kNeteaseSearchResponse },
        { "song/detail", kNeteaseDetailResponse },
    };
    http.binary_payload = std::string(4096, '\xAB');
    CDownloadManager::AutoRequest cover_ok_probe = request;
    cover_ok_probe.download_lyric = false;
    cover_ok_probe.audio_file_path = dir + "/cover_ok.mp3";
    CHECK(manager.StartAutoDownload(u8"周杰伦 晴天", cover_ok_probe));
    CHECK(WaitIdle(manager));
    status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_SUCCESS);
    CHECK(status.saved_lyric_path.empty());
    CHECK(!status.saved_cover_path.empty());
    CHECK(status.message.find(u8"歌词") == std::string::npos);
    CHECK(status.message.find(u8"封面") != std::string::npos);

    // 什么都不下载是调用方错误，应当被拒绝
    CDownloadManager::AutoRequest nothing = request;
    nothing.download_lyric = false;
    nothing.download_cover = false;
    CHECK(!manager.StartAutoDownload(u8"晴天", nothing));
    CHECK(!manager.StartDownloadSelected(0, nothing));

    // 没有设置 HTTP 客户端时所有入口都应拒绝
    CDownloadManager no_http;
    CHECK(!no_http.StartSearch(u8"晴天"));
    CHECK(!no_http.StartAutoDownload(u8"晴天", request));
    // 空关键词也要拒绝
    CHECK(!manager.StartSearch(""));

    // ---- 切换到 QQ 音乐 ----
    manager.Reset();
    manager.SetProvider(CDownloadManager::PROVIDER_QQ);
    CHECK_EQ(std::string(manager.GetProviderName()), u8"QQ音乐");
    FakeHttpClient qq_http;
    qq_http.routes = {
        { "client_search_cp",      kQQSearchResponse },
        { "fcg_query_lyric_new",   kQQLyricResponse },
        { "fcg_play_single_song",  kQQSongResponse },
    };
    qq_http.binary_payload = std::string(2048, '\xCD');
    manager.SetHttpClient(&qq_http);

    CDownloadManager::AutoRequest qq_request = request;
    qq_request.audio_file_path = dir + "/qq_test.mp3";
    CHECK(FileUtil::WriteAll(qq_request.audio_file_path, "fake"));
    CHECK(manager.StartAutoDownload(u8"周杰伦 晴天", qq_request));
    CHECK(WaitIdle(manager));
    status = manager.Poll();
    CHECK(status.state == CDownloadManager::ST_SUCCESS);
    CHECK_EQ(status.saved_lyric_path, dir + "/qq_test.lrc");
    CHECK_EQ(status.saved_cover_path, dir + "/qq_test.jpg");
    // QQ 的搜索走 GET，不该有 POST
    CHECK_EQ_INT(qq_http.post_count, 0);

    // 非法的 provider 值要退回默认源而不是崩溃
    manager.SetProvider(static_cast<CDownloadManager::ProviderId>(99));
    CHECK_EQ(std::string(manager.GetProviderName()), u8"网易云音乐");
}

static void TestVersionUtil()
{
    std::printf("VersionUtil\n");

    // 拆解
    std::vector<int> v = VersionUtil::Parse("v1.2.3");
    CHECK_EQ_INT(static_cast<long long>(v.size()), 3);
    if (v.size() == 3)
    {
        CHECK_EQ_INT(v[0], 1);
        CHECK_EQ_INT(v[1], 2);
        CHECK_EQ_INT(v[2], 3);
    }
    // 预发布后缀在遇到非数字时截断，前面的数字要保留
    v = VersionUtil::Parse("1.2.0-beta");
    CHECK_EQ_INT(static_cast<long long>(v.size()), 3);
    CHECK(VersionUtil::Parse("").empty());
    CHECK(VersionUtil::Parse("abc").empty());

    // 基本比较
    CHECK(VersionUtil::IsNewer("0.5.0", "0.4.0"));
    CHECK(VersionUtil::IsNewer("v0.5.0", "0.4.0"));
    CHECK(!VersionUtil::IsNewer("0.4.0", "0.4.0"));
    CHECK(!VersionUtil::IsNewer("0.4.0", "0.5.0"));

    // 位数不同：缺的位按 0 补
    CHECK(VersionUtil::IsNewer("1.0", "0.9.9"));
    CHECK(VersionUtil::IsNewer("0.4.1", "0.4"));
    CHECK(!VersionUtil::IsNewer("0.4", "0.4.0"));

    // 必须按数值比而不是按字典序：0.10 比 0.9 新
    CHECK(VersionUtil::IsNewer("0.10.0", "0.9.0"));
    CHECK(!VersionUtil::IsNewer("0.9.0", "0.10.0"));
    CHECK(VersionUtil::IsNewer("1.0.10", "1.0.9"));

    // 解析不出版本号时一律当作"没有更新"，宁可不更新也不能乱更新
    CHECK(!VersionUtil::IsNewer("", "0.4.0"));
    CHECK(!VersionUtil::IsNewer("latest", "0.4.0"));

    // 带后缀的新版本仍应被识别
    CHECK(VersionUtil::IsNewer("v1.2.0-beta", "1.1.9"));
}

static void TestReleaseInfo()
{
    // 用 std::puts 而不是 printf：省掉一个反斜杠转义
    std::puts("ReleaseInfo");

    ReleaseInfo::Info info;
    std::string error;

    // ---- 真实响应 ----
    CHECK(ReleaseInfo::Parse(kGithubReleaseResponse, "MusicPlayer2.nro", info, error));
    CHECK_EQ(error, std::string());
    CHECK_EQ(info.tag, std::string("v0.4.0"));
    CHECK_EQ(info.asset_url,
             std::string("https://github.com/Schweik7/MusicPlayer2/releases/download/"
                         "v0.4.0/MusicPlayer2.nro"));
    CHECK_EQ_INT(static_cast<long long>(info.asset_size), 10800541);
    CHECK(!info.notes.empty());
    // 正文里有中文，解析不能把多字节字符弄坏
    CHECK(info.notes.find(u8"Nintendo Switch") != std::string::npos);

    // ---- 有版本但没有目标资产：不算失败，但 asset_url 为空 ----
    ReleaseInfo::Info missing;
    CHECK(ReleaseInfo::Parse(kGithubReleaseResponse, "NotThere.nro", missing, error));
    CHECK_EQ(missing.tag, std::string("v0.4.0"));
    CHECK(missing.asset_url.empty());
    CHECK_EQ_INT(static_cast<long long>(missing.asset_size), 0);

    // ---- GitHub 的错误响应要把 message 透出来，而不是笼统报"没有 tag_name" ----
    ReleaseInfo::Info err_info;
    CHECK(!ReleaseInfo::Parse("{\"message\": \"Not Found\", \"status\": \"404\"}",
                              "MusicPlayer2.nro", err_info, error));
    CHECK_EQ(error, std::string("Not Found"));

    // ---- 无效输入 ----
    CHECK(!ReleaseInfo::Parse("", "MusicPlayer2.nro", err_info, error));
    CHECK(!ReleaseInfo::Parse("<html>502 Bad Gateway</html>", "MusicPlayer2.nro",
                              err_info, error));
    CHECK(!ReleaseInfo::Parse("{}", "MusicPlayer2.nro", err_info, error));
    // 仓库一个 release 都没有时 GitHub 返回 404，上面那条已覆盖；
    // 这里再确认 assets 缺失也不会崩
    ReleaseInfo::Info no_assets;
    CHECK(ReleaseInfo::Parse("{\"tag_name\": \"v9.9.9\"}", "MusicPlayer2.nro",
                             no_assets, error));
    CHECK_EQ(no_assets.tag, std::string("v9.9.9"));
    CHECK(no_assets.asset_url.empty());
}

void RunNetTests()
{
    TestJson();
    TestUrlUtil();
    TestSongMatcher();
    TestNeteaseProvider();
    TestQQProvider();
    TestProviderRobustness();
    TestLyricProviderUtil();
    TestDownloadManager();
    TestVersionUtil();
    TestReleaseInfo();
}
