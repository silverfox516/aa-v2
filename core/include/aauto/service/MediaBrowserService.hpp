#pragma once

#include "aauto/service/ServiceBase.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aauto::service {

/// Media browser channel — phone exposes its media library tree
/// (Spotify playlists, on-device music, ...). HU is the browser client.
///
/// Status: DEPRECATED-IN-MODERN-AA (2026-04-29). The legacy AAP
/// MediaBrowser channel (ch12, MEDIA_ROOT_NODE/SOURCE_NODE/etc.) is
/// not opened by modern Android Auto host anymore. Tests across
/// multiple apps (YouTube Music, Spotify) all show the phone never
/// sends CHANNEL_OPEN_REQ for ch12 — even with Spotify, which has
/// both the legacy MediaBrowserService and the newer
/// androidx.car.app library. Modern AA uses Car App Library: the
/// phone renders the browse UI itself and projects it via the video
/// sink (ch1). The HU never receives track-list data on a separate
/// channel.
///
/// Service code stays in tree as a learning artifact and a fallback
/// path in case an older Android Auto version or a bare-bones
/// MediaBrowserService-only app needs to interoperate. See
/// docs/plans/0005_media_browser.md for the full investigation chain.
///
/// Inbound (Phone → HU):
///   - MEDIA_ROOT_NODE   (32769): { path, sources[] }
///   - MEDIA_SOURCE_NODE (32770): { source, start, total, lists[] }
///   - MEDIA_LIST_NODE   (32771): { list, start, total, songs[] }
///   - MEDIA_SONG_NODE   (32772): { song, album_art, duration_seconds }
///
/// Outbound (HU → Phone):
///   - MEDIA_GET_NODE    (32773): walk the tree
///   - MEDIA_BROWSE_INPUT(32774): nav input on a path (UP/DOWN/ENTER/BACK)
class MediaBrowserService : public ServiceBase {
public:
    /// One element from a MediaSource list (per MEDIA_ROOT_NODE).
    struct SourceEntry {
        std::string path;
        std::string name;
        std::vector<uint8_t> album_art;  // may be empty
    };

    /// One element from a MediaList list (per MEDIA_SOURCE_NODE).
    /// type matches MediaList.Type proto enum
    /// (0=UNKNOWN, 1=PLAYLIST, 2=ALBUM, 3=ARTIST, 4=STATION, 5=GENRE).
    struct ListEntry {
        std::string path;
        int32_t     type = 0;
        std::string name;
        std::vector<uint8_t> album_art;
    };

    /// One element from a MediaSong list (per MEDIA_LIST_NODE).
    struct SongEntry {
        std::string path;
        std::string name;
        std::string artist;
        std::string album;
    };

    using RootCallback = std::function<void(
        const std::string& path,
        const std::vector<SourceEntry>& sources)>;

    using SourceNodeCallback = std::function<void(
        const SourceEntry& source,
        int32_t start, int32_t total,
        const std::vector<ListEntry>& lists)>;

    using ListNodeCallback = std::function<void(
        const ListEntry& list,
        int32_t start, int32_t total,
        const std::vector<SongEntry>& songs)>;

    using SongNodeCallback = std::function<void(
        const SongEntry& song,
        const std::vector<uint8_t>& album_art,
        uint32_t duration_seconds)>;

    /// InstrumentClusterInput action enum (shared.proto):
    /// 0=UNKNOWN, 1=UP, 2=DOWN, 3=LEFT, 4=RIGHT, 5=ENTER, 6=BACK, 7=CALL.
    enum class BrowseAction : int32_t {
        Unknown = 0,
        Up = 1, Down = 2, Left = 3, Right = 4,
        Enter = 5, Back = 6, Call = 7,
    };

    explicit MediaBrowserService(SendMessageFn send_fn);

    void set_root_callback(RootCallback cb)             { root_cb_   = std::move(cb); }
    void set_source_callback(SourceNodeCallback cb)     { source_cb_ = std::move(cb); }
    void set_list_callback(ListNodeCallback cb)         { list_cb_   = std::move(cb); }
    void set_song_callback(SongNodeCallback cb)         { song_cb_   = std::move(cb); }

    /// Request a node by path. Empty path = root.
    /// start = pagination offset (default 0).
    /// get_album_art = whether the phone should include cover bytes
    /// (set to false when art is not yet needed — saves wire traffic).
    void request_node(const std::string& path,
                      int32_t start = 0,
                      bool get_album_art = true);

    /// Send a navigation input on a path. ENTER on a song path triggers
    /// playback start on the phone (see PLAYBACK_STATUS that follows).
    void browse_input(const std::string& path, BrowseAction action);

    ServiceType type() const override { return ServiceType::MediaBrowser; }
    void fill_config(aap_protobuf::service::ServiceConfiguration* config) override;

private:
    RootCallback        root_cb_;
    SourceNodeCallback  source_cb_;
    ListNodeCallback    list_cb_;
    SongNodeCallback    song_cb_;
};

} // namespace aauto::service
