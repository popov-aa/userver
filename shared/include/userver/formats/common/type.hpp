#pragma once

/// @file userver/formats/common/type.hpp
/// @brief @copybrief formats::common::Type

/// Common utilities for all the formats
USERVER_NAMESPACE_BEGIN

namespace formats::common {

/// Common enum of types
enum class Type {
  kNull,    /// Value or ValueBuilder holds Null value
  kArray,   /// Value or ValueBuilder holds an Array
  kObject,  /// Value or ValueBuilder holds a Map
};

}  // namespace formats::common

USERVER_NAMESPACE_END
