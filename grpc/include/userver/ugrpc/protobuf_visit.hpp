#pragma once

/// @file userver/ugrpc/protobuf_visit.hpp
/// @brief Utilities for visiting the fields of protobufs

#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <google/protobuf/stubs/common.h>

#include <userver/utils/function_ref.hpp>
#include <userver/utils/impl/internal_tag.hpp>
#include <userver/utils/span.hpp>

namespace google::protobuf {

class Message;
class Descriptor;
class FieldDescriptor;

}  // namespace google::protobuf

USERVER_NAMESPACE_BEGIN

namespace ugrpc {

using MessageVisitCallback =
    utils::function_ref<void(google::protobuf::Message&)>;

using FieldVisitCallback = utils::function_ref<void(
    google::protobuf::Message&, const google::protobuf::FieldDescriptor&)>;

/// @brief Execute a callback for all non-empty fields of the message.
void VisitFields(google::protobuf::Message& message,
                 FieldVisitCallback callback);

/// @brief Execute a callback for the message and its non-empty submessages.
void VisitMessagesRecursive(google::protobuf::Message& message,
                            MessageVisitCallback callback);

/// @brief Execute a callback for all fields
/// of the message and its non-empty submessages.
void VisitFieldsRecursive(google::protobuf::Message& message,
                          FieldVisitCallback callback);

using DescriptorList = std::vector<const google::protobuf::Descriptor*>;

using FieldDescriptorList =
    std::vector<const google::protobuf::FieldDescriptor*>;

/// @brief Get the descriptors of fields in the message.
FieldDescriptorList GetFieldDescriptors(
    const google::protobuf::Descriptor& descriptor);

/// @brief Get the descriptors of current and nested messages.
DescriptorList GetNestedMessageDescriptors(
    const google::protobuf::Descriptor& descriptor);

/// @brief Find a generated type by name.
const google::protobuf::Descriptor* FindGeneratedMessage(std::string_view name);

/// @brief Find the field of a generated type by name.
const google::protobuf::FieldDescriptor* FindField(
    const google::protobuf::Descriptor* descriptor, std::string_view field);

/// @brief Base class for @ref FieldsVisitor and @ref MessagesVisitor.
/// Provides the interface and manages the descriptor graph
/// to enable the visitors to find all selected structures.
template <typename Callback>
class BaseVisitor {
 public:
  enum class LockBehavior {
    /// @brief Do not take shared_mutex locks for any operation on the visitor
    kNone = 0,

    /// @brief Take shared_lock for all read operations on the visitor
    /// and unique_lock for all Compile operations
    kShared = 1
  };

  BaseVisitor(BaseVisitor&&) = delete;
  BaseVisitor(const BaseVisitor&) = delete;

  /// @brief Compiles the visitor for the given message type
  /// and its dependent types
  void Compile(const google::protobuf::Descriptor* descriptor);

  /// @brief Compiles the visitor for the given message types
  /// and their dependent types
  void Compile(const DescriptorList& descriptors);

  /// @brief Compiles the visitor for the given
  /// generated message type and its dependent types
  void CompileGenerated(std::string_view message_name) {
    Compile(FindGeneratedMessage(message_name));
  }

  /// @brief Compiles the visitor for the given
  /// generated message type and their dependent types
  void CompileGenerated(utils::span<std::string_view> message_names) {
    DescriptorList descriptors;
    for (const std::string_view& message_name : message_names) {
      descriptors.push_back(FindGeneratedMessage(message_name));
    }
    Compile(descriptors);
  }

  /// @brief Execute a callback without recursion
  ///
  /// Equivalent to @ref VisitFields
  /// but utilizes the precompilation data from @ref Compile
  void Visit(google::protobuf::Message& message, Callback callback);

  /// @brief Execute a callback recursively
  ///
  /// Equivalent to @ref VisitFieldsRecursive and @ref VisitMessagesRecursive
  /// but utilizes the precompilation data from @ref Compile
  void VisitRecursive(google::protobuf::Message& message, Callback callback);

  /// @cond
  /// Only for internal use.
  using Dependencies = std::unordered_map<
      const google::protobuf::Descriptor*,
      std::unordered_set<const google::protobuf::FieldDescriptor*>>;

  /// Only for internal use.
  using DescriptorSet = std::unordered_set<const google::protobuf::Descriptor*>;

  /// Only for internal use.
  using FieldDescriptorSet =
      std::unordered_set<const google::protobuf::FieldDescriptor*>;

  /// Only for internal use.
  const Dependencies& GetFieldsWithSelectedChildren(
      utils::impl::InternalTag) const {
    return fields_with_selected_children_;
  }

  /// Only for internal use.
  const Dependencies& GetReverseEdges(utils::impl::InternalTag) const {
    return reverse_edges_;
  }

  /// Only for internal use.
  const DescriptorSet& GetPropagated(utils::impl::InternalTag) const {
    return propagated_;
  }

  /// Only for internal use.
  const DescriptorSet& GetCompiled(utils::impl::InternalTag) const {
    return compiled_;
  }
  /// @endcond

 protected:
  /// @cond
  explicit BaseVisitor(LockBehavior lock_behavior)
      : lock_behavior_(lock_behavior) {}

  // Disallow destruction via pointer to base
  ~BaseVisitor() = default;

  /// @brief Compile one message without nested.
  virtual void CompileOne(const google::protobuf::Descriptor& descriptor) = 0;

  /// @brief Checks if the message is selected or has anything selected.
  virtual bool IsSelected(const google::protobuf::Descriptor&) const = 0;

  /// @brief Execute a callback without recursion
  virtual void DoVisit(google::protobuf::Message& message,
                       Callback callback) = 0;
  /// @endcond

 private:
  /// @brief Gets all submessages of the given messages.
  DescriptorSet GetFullSubtrees(const DescriptorList& descriptors) const;

  /// @brief Propagate the selection information upwards
  void PropagateSelected(const google::protobuf::Descriptor* descriptor);

  /// @brief Safe version with recursion_limit
  void VisitRecursiveImpl(google::protobuf::Message& message, Callback callback,
                          int recursion_limit);

  std::shared_mutex mutex_;
  const LockBehavior lock_behavior_;

  Dependencies fields_with_selected_children_;
  Dependencies reverse_edges_;
  DescriptorSet propagated_;
  DescriptorSet compiled_;
};

/// @brief Collects knowledge of the structure of the protobuf messages
/// allowing for efficient loops over fields to apply a callback to the ones
/// selected by the 'selector' function.
///
/// If you do not have static knowledge of the required fields, you should
/// use @ref VisitFields or @ref VisitFieldsRecursive that are equivalent to
/// FieldsVisitor with a `return true;` selector.
///
/// @warning You should not construct this at runtime as it performs significant
/// computations in the constructor to precompile the visitors.
/// You should create this ones at start-up.
///
/// Example usage: @snippet grpc/src/ugrpc/impl/protobuf_utils.cpp
class FieldsVisitor final : public BaseVisitor<FieldVisitCallback> {
 public:
  using Selector =
      utils::function_ref<bool(const google::protobuf::Descriptor& descriptor,
                               const google::protobuf::FieldDescriptor& field)>;

  /// @brief Creates the visitor with the given selector
  /// and compiles it for the message types we can find.
  explicit FieldsVisitor(Selector selector);

  /// @brief Creates the visitor with the given selector
  /// and compiles it for the given message types and their fields recursively.
  FieldsVisitor(Selector selector, const DescriptorList& descriptors);

  /// @brief Creates the visitor with custom thread locking behavior
  /// and the given selector for runtime compilation.
  ///
  /// @warning Do not use this unless you know what you are doing.
  FieldsVisitor(Selector selector, LockBehavior lock_behavior);

  /// @brief Creates the visitor with custom thread locking behavior
  /// and the given selector; compiles it for the given message types.
  ///
  /// @warning Do not use this unless you know what you are doing.
  FieldsVisitor(Selector selector, const DescriptorList& descriptors,
                LockBehavior lock_behavior);

  /// @cond
  /// Only for internal use.
  const Dependencies& GetSelectedFields(utils::impl::InternalTag) const {
    return selected_fields_;
  }
  /// @endcond

 private:
  void CompileOne(const google::protobuf::Descriptor& descriptor) override;

  bool IsSelected(
      const google::protobuf::Descriptor& descriptor) const override {
    return selected_fields_.find(&descriptor) != selected_fields_.end();
  }

  void DoVisit(google::protobuf::Message& message,
               FieldVisitCallback callback) override;

  Dependencies selected_fields_;
  const Selector selector_;
};

/// @brief Collects knowledge of the structure of the protobuf messages
/// allowing for efficient loops over nested messages to apply a callback
/// to the ones selected by the 'selector' function.
///
/// If you do not have static knowledge of the required messages, you should
/// use @ref VisitMessagesRecursive that is equivalent to
/// MessagesVisitor with a 'return true' selector.
///
/// @warning You should not construct this at runtime as it performs significant
/// computations in the constructor to precompile the visitors.
/// You should create this ones at start-up.
class MessagesVisitor final : public BaseVisitor<MessageVisitCallback> {
 public:
  using Selector =
      utils::function_ref<bool(const google::protobuf::Descriptor& descriptor)>;

  /// @brief Creates the visitor with the given selector for runtime compilation
  /// and compiles it for the message types we can find.
  explicit MessagesVisitor(Selector selector);

  /// @brief Creates the visitor with the given selector
  /// and compiles it for the given message types and their fields recursively.
  MessagesVisitor(Selector selector, const DescriptorList& descriptors);

  /// @brief Creates the visitor with custom thread locking behavior
  /// and the given selector for runtime compilation.
  ///
  /// @warning Do not use this unless you know what you are doing.
  MessagesVisitor(Selector selector, LockBehavior lock_behavior);

  /// @brief Creates the visitor with custom thread locking behavior
  /// and the given selector; compiles it for the given message types.
  ///
  /// @warning Do not use this unless you know what you are doing.
  MessagesVisitor(Selector selector, const DescriptorList& descriptors,
                  LockBehavior lock_behavior);

  /// @cond
  /// Only for internal use.
  const DescriptorSet& GetSelectedMessages(utils::impl::InternalTag) const {
    return selected_messages_;
  }
  /// @endcond

 private:
  void CompileOne(const google::protobuf::Descriptor& descriptor) override;

  bool IsSelected(
      const google::protobuf::Descriptor& descriptor) const override {
    return selected_messages_.find(&descriptor) != selected_messages_.end();
  }

  void DoVisit(google::protobuf::Message& message,
               MessageVisitCallback callback) override;

  DescriptorSet selected_messages_;
  const Selector selector_;
};

}  // namespace ugrpc

USERVER_NAMESPACE_END
