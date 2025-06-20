#pragma once

/// @file userver/ydb/topic.hpp
/// @brief YDB Topic client

#include <chrono>
#include <memory>
#include <string>

#include <ydb-cpp-sdk/client/topic/client.h>

USERVER_NAMESPACE_BEGIN

namespace ydb {

class Transaction;

namespace impl {
class Driver;
struct TopicSettings;
}  // namespace impl

/// @brief Read session used to connect to one or more topics for reading
///
/// @see https://ydb.tech/docs/en/reference/ydb-sdk/topic#reading
///
/// ## Example usage:
///
/// @ref samples/ydb_service/components/topic_reader.hpp
/// @ref samples/ydb_service/components/topic_reader.cpp
///
/// @example samples/ydb_service/components/topic_reader.hpp
/// @example samples/ydb_service/components/topic_reader.cpp
class TopicReadSession final {
public:
    /// @cond
    // For internal use only.
    explicit TopicReadSession(std::shared_ptr<NYdb::NTopic::IReadSession> read_session);
    /// @endcond

    /// @brief Get read session events
    ///
    /// Waits until event occurs
    /// @param max_events_count maximum events count in batch
    /// @param max_size_bytes total size limit for data messages in batch
    /// if not specified, read session chooses event batch size automatically
    std::vector<NYdb::NTopic::TReadSessionEvent::TEvent> GetEvents(
        std::optional<std::size_t> max_events_count = {},
        size_t max_size_bytes = std::numeric_limits<size_t>::max()
    );

    /// @brief Close read session
    ///
    /// Waits for all commit acknowledgments to arrive.
    /// Force close after timeout
    bool Close(std::chrono::milliseconds timeout);

    /// Get native read session
    /// @warning Use with care! Facilities from
    /// `<core/include/userver/drivers/subscribable_futures.hpp>` can help with
    /// non-blocking wait operations.
    std::shared_ptr<NYdb::NTopic::IReadSession> GetNativeTopicReadSession();

private:
    std::shared_ptr<NYdb::NTopic::IReadSession> read_session_;
};

/// @brief Write session used to connect to topic for writing
///
/// @see https://ydb.tech/docs/en/reference/ydb-sdk/topic#writing
///
class TopicWriteSession final {
public:
    /// @cond
    // For internal use only.
    explicit TopicWriteSession(std::shared_ptr<NYdb::NTopic::IWriteSession> write_session);
    /// @endcond

    /// @brief Get write session events
    ///
    /// Waits until event occurs
    /// @param max_events_count maximum events count in batch
    /// if not specified, write session chooses event batch size automatically
    std::vector<NYdb::NTopic::TWriteSessionEvent::TEvent> GetEvents(
        std::optional<std::size_t> max_events_count = {}
    );

    //! Write single message.
    //! continuationToken - a token earlier provided to client with ReadyToAccept event.
    void Write(NYdb::NTopic::TContinuationToken&& continuationToken, NYdb::NTopic::TWriteMessage&& message,
               Transaction* tx = nullptr);

    //! Write single message. Old method with only basic message options.
    void Write(NYdb::NTopic::TContinuationToken&& continuationToken, std::string_view data, std::optional<uint64_t> seqNo = std::nullopt,
               std::optional<TInstant> createTimestamp = std::nullopt);

    //! Write single message that is already coded by codec.
    //! continuationToken - a token earlier provided to client with ReadyToAccept event.
    void WriteEncoded(NYdb::NTopic::TContinuationToken&& continuationToken, NYdb::NTopic::TWriteMessage&& params,
                      Transaction* tx = nullptr);

    //! Write single message that is already compressed by codec. Old method with only basic message options.
    void WriteEncoded(NYdb::NTopic::TContinuationToken&& continuationToken, std::string_view data, NYdb::NTopic::ECodec codec, uint32_t originalSize,
                      std::optional<uint64_t> seqNo = std::nullopt, std::optional<TInstant> createTimestamp = std::nullopt);

    /// @brief Close read session
    ///
    /// Waits for all commit acknowledgments to arrive.
    /// Force close after timeout
    bool Close(std::chrono::milliseconds timeout);

    /// Get native write session
    /// @warning Use with care! Facilities from
    /// `<core/include/userver/drivers/subscribable_futures.hpp>` can help with
    /// non-blocking wait operations.
    std::shared_ptr<NYdb::NTopic::IWriteSession> GetNativeTopicWriteSession();

private:
    std::shared_ptr<NYdb::NTopic::IWriteSession> write_session_;
};

/// @ingroup userver_clients
///
/// @brief YDB Topic Client
///
/// @see https://ydb.tech/docs/en/concepts/topic
class TopicClient final {
public:
    /// @cond
    // For internal use only.
    TopicClient(std::shared_ptr<impl::Driver> driver, impl::TopicSettings settings);
    /// @endcond

    ~TopicClient();

    /// Alter topic
    void AlterTopic(const std::string& path, const NYdb::NTopic::TAlterTopicSettings& settings);

    /// Describe topic
    NYdb::NTopic::TDescribeTopicResult DescribeTopic(const std::string& path);

    /// Create read session
    TopicReadSession CreateReadSession(const NYdb::NTopic::TReadSessionSettings& settings);

    /// Create write session
    TopicWriteSession CreateWriteSession(const NYdb::NTopic::TWriteSessionSettings& settings);

    /// Get native topic client
    /// @warning Use with care! Facilities from
    /// `<core/include/userver/drivers/subscribable_futures.hpp>` can help with
    /// non-blocking wait operations.
    NYdb::NTopic::TTopicClient& GetNativeTopicClient();

private:
    std::shared_ptr<impl::Driver> driver_;
    NYdb::NTopic::TTopicClient topic_client_;
};

}  // namespace ydb

USERVER_NAMESPACE_END
