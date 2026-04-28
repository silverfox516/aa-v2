#define LOG_TAG "AA.MediaBrowserService"

#include "aauto/service/MediaBrowserService.hpp"
#include "aauto/utils/Logger.hpp"
#include "aauto/utils/ProtobufCompat.hpp"

#include <aap_protobuf/service/Service.pb.h>
#include <aap_protobuf/service/mediabrowser/MediaBrowserMessageId.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaRootNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaSourceNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaListNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaSongNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaGetNode.pb.h>
#include <aap_protobuf/service/mediabrowser/message/MediaBrowserInput.pb.h>
#include <aap_protobuf/shared/InstrumentClusterInput.pb.h>

namespace aauto::service {

namespace pb_mb     = aap_protobuf::service::mediabrowser;
namespace pb_mbm    = aap_protobuf::service::mediabrowser::message;
namespace pb_shared = aap_protobuf::shared;

namespace {

const char* list_type_name(pb_mbm::MediaList::Type t) {
    switch (t) {
        case pb_mbm::MediaList::UNKNOWN:  return "UNKNOWN";
        case pb_mbm::MediaList::PLAYLIST: return "PLAYLIST";
        case pb_mbm::MediaList::ALBUM:    return "ALBUM";
        case pb_mbm::MediaList::ARTIST:   return "ARTIST";
        case pb_mbm::MediaList::STATION:  return "STATION";
        case pb_mbm::MediaList::GENRE:    return "GENRE";
    }
    return "?";
}

} // namespace

MediaBrowserService::MediaBrowserService(SendMessageFn send_fn)
    : ServiceBase(std::move(send_fn)) {

    register_handler(
        static_cast<uint16_t>(pb_mb::MEDIA_ROOT_NODE),
        [this](const uint8_t* data, std::size_t size) {
            pb_mbm::MediaRootNode msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "media.browser", "ROOT_NODE", size);
                return;
            }
            AA_LOG_I("%-18s %-24s path=\"%s\" sources=%d",
                     "media.browser", "ROOT_NODE",
                     msg.path().c_str(),
                     msg.media_sources_size());
            std::vector<SourceEntry> sources;
            sources.reserve(msg.media_sources_size());
            for (int i = 0; i < msg.media_sources_size(); ++i) {
                const auto& s = msg.media_sources(i);
                AA_LOG_I("%-18s %-24s   [%d] name=\"%s\" path=\"%s\""
                         " album_art=%zuB",
                         "media.browser", "ROOT_NODE", i,
                         s.name().c_str(), s.path().c_str(),
                         s.album_art().size());
                SourceEntry e;
                e.path = s.path();
                e.name = s.name();
                e.album_art.assign(s.album_art().begin(), s.album_art().end());
                sources.push_back(std::move(e));
            }
            if (root_cb_) root_cb_(msg.path(), sources);
        });

    register_handler(
        static_cast<uint16_t>(pb_mb::MEDIA_SOURCE_NODE),
        [this](const uint8_t* data, std::size_t size) {
            pb_mbm::MediaSourceNode msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "media.browser", "SOURCE_NODE", size);
                return;
            }
            AA_LOG_I("%-18s %-24s source=\"%s\" path=\"%s\""
                     " start=%d total=%d lists=%d",
                     "media.browser", "SOURCE_NODE",
                     msg.source().name().c_str(),
                     msg.source().path().c_str(),
                     msg.start(), msg.total(), msg.lists_size());
            SourceEntry src;
            src.path = msg.source().path();
            src.name = msg.source().name();
            src.album_art.assign(msg.source().album_art().begin(),
                                 msg.source().album_art().end());
            std::vector<ListEntry> lists;
            lists.reserve(msg.lists_size());
            for (int i = 0; i < msg.lists_size(); ++i) {
                const auto& l = msg.lists(i);
                AA_LOG_I("%-18s %-24s   [%d] type=%s name=\"%s\""
                         " path=\"%s\" album_art=%zuB",
                         "media.browser", "SOURCE_NODE", i,
                         list_type_name(l.type()),
                         l.has_name() ? l.name().c_str() : "",
                         l.path().c_str(), l.album_art().size());
                ListEntry e;
                e.path = l.path();
                e.type = static_cast<int32_t>(l.type());
                e.name = l.has_name() ? l.name() : std::string();
                e.album_art.assign(l.album_art().begin(), l.album_art().end());
                lists.push_back(std::move(e));
            }
            if (source_cb_) source_cb_(src, msg.start(), msg.total(), lists);
        });

    register_handler(
        static_cast<uint16_t>(pb_mb::MEDIA_LIST_NODE),
        [this](const uint8_t* data, std::size_t size) {
            pb_mbm::MediaListNode msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "media.browser", "LIST_NODE", size);
                return;
            }
            AA_LOG_I("%-18s %-24s list=\"%s\" path=\"%s\""
                     " start=%d total=%d songs=%d",
                     "media.browser", "LIST_NODE",
                     msg.list().has_name() ? msg.list().name().c_str() : "",
                     msg.list().path().c_str(),
                     msg.start(), msg.total(), msg.songs_size());
            ListEntry list;
            list.path = msg.list().path();
            list.type = static_cast<int32_t>(msg.list().type());
            list.name = msg.list().has_name() ? msg.list().name() : std::string();
            list.album_art.assign(msg.list().album_art().begin(),
                                  msg.list().album_art().end());
            std::vector<SongEntry> songs;
            songs.reserve(msg.songs_size());
            for (int i = 0; i < msg.songs_size(); ++i) {
                const auto& s = msg.songs(i);
                AA_LOG_I("%-18s %-24s   [%d] name=\"%s\" artist=\"%s\""
                         " album=\"%s\" path=\"%s\"",
                         "media.browser", "LIST_NODE", i,
                         s.name().c_str(),
                         s.has_artist() ? s.artist().c_str() : "",
                         s.has_album()  ? s.album().c_str()  : "",
                         s.path().c_str());
                SongEntry e;
                e.path   = s.path();
                e.name   = s.name();
                e.artist = s.has_artist() ? s.artist() : std::string();
                e.album  = s.has_album()  ? s.album()  : std::string();
                songs.push_back(std::move(e));
            }
            if (list_cb_) list_cb_(list, msg.start(), msg.total(), songs);
        });

    register_handler(
        static_cast<uint16_t>(pb_mb::MEDIA_SONG_NODE),
        [this](const uint8_t* data, std::size_t size) {
            pb_mbm::MediaSongNode msg;
            if (!msg.ParseFromArray(data, static_cast<int>(size))) {
                AA_LOG_W("%-18s %-24s parse failed (%zu bytes)",
                         "media.browser", "SONG_NODE", size);
                return;
            }
            AA_LOG_I("%-18s %-24s name=\"%s\" artist=\"%s\" album=\"%s\""
                     " path=\"%s\" album_art=%zuB duration=%us",
                     "media.browser", "SONG_NODE",
                     msg.song().name().c_str(),
                     msg.song().has_artist() ? msg.song().artist().c_str() : "",
                     msg.song().has_album()  ? msg.song().album().c_str()  : "",
                     msg.song().path().c_str(),
                     msg.album_art().size(),
                     msg.duration_seconds());
            SongEntry song;
            song.path   = msg.song().path();
            song.name   = msg.song().name();
            song.artist = msg.song().has_artist() ? msg.song().artist() : std::string();
            song.album  = msg.song().has_album()  ? msg.song().album()  : std::string();
            std::vector<uint8_t> art(msg.album_art().begin(),
                                     msg.album_art().end());
            if (song_cb_) song_cb_(song, art, msg.duration_seconds());
        });
}

void MediaBrowserService::request_node(const std::string& path,
                                       int32_t start,
                                       bool get_album_art) {
    pb_mbm::MediaGetNode req;
    req.set_path(path);
    if (start != 0) req.set_start(start);
    req.set_get_album_art(get_album_art);
    auto buf = utils::serialize_to_vector(req);
    AA_LOG_I("%-18s %-24s path=\"%s\" start=%d get_art=%d",
             "media.browser", "GET_NODE (TX)",
             path.c_str(), start, get_album_art ? 1 : 0);
    send(static_cast<uint16_t>(pb_mb::MEDIA_GET_NODE), buf);
}

void MediaBrowserService::browse_input(const std::string& path,
                                       BrowseAction action) {
    pb_mbm::MediaBrowserInput req;
    req.set_path(path);
    req.mutable_input()->set_action(
        static_cast<pb_shared::InstrumentClusterInput::InstrumentClusterAction>(
            action));
    auto buf = utils::serialize_to_vector(req);
    AA_LOG_I("%-18s %-24s path=\"%s\" action=%d",
             "media.browser", "BROWSE_INPUT (TX)",
             path.c_str(), static_cast<int>(action));
    send(static_cast<uint16_t>(pb_mb::MEDIA_BROWSE_INPUT), buf);
}

void MediaBrowserService::fill_config(
        aap_protobuf::service::ServiceConfiguration* config) {
    config->mutable_media_browser_service();
    AA_LOG_I("media browser service configured");
}

} // namespace aauto::service
