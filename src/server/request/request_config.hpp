#pragma once

#include <string>

#include <json_config/json_config.hpp>

namespace server {
namespace request {

class RequestConfig : public json_config::JsonConfig {
 public:
  enum class Type { kHttp };

  RequestConfig(formats::json::Value json, std::string full_path,
                json_config::VariableMapPtr config_vars_ptr);

  const Type& GetType() const;

  static RequestConfig ParseFromJson(
      const formats::json::Value& json, const std::string& full_path,
      const json_config::VariableMapPtr& config_vars_ptr);

  static const std::string& TypeToString(Type type);

 private:
  Type type_ = Type::kHttp;
};

}  // namespace request
}  // namespace server
