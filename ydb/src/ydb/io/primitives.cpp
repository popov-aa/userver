#include <userver/ydb/io/primitives.hpp>

#include <ydb-cpp-sdk/client/params/params.h>
#include <ydb-cpp-sdk/client/value/value.h>

#include <userver/compiler/demangle.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/logging/log.hpp>
#include <userver/ydb/impl/cast.hpp>

#include <ydb/impl/type_category.hpp>

#include <boost/lexical_cast.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

USERVER_NAMESPACE_BEGIN

namespace ydb {

namespace {

bool IsOptional(const NYdb::TValueParser& value_parser) {
    return value_parser.GetKind() == NYdb::TTypeParser::ETypeKind::Optional;
}

template <typename PrimitiveTrait, typename Builder>
void WriteOptionalPrimitive(
    NYdb::TValueBuilderBase<Builder>& builder,
    const std::optional<typename PrimitiveTrait::Type>& value
) {
    if (value) {
        builder.BeginOptional();
        PrimitiveTrait::Write(builder, *value);
        builder.EndOptional();
    } else {
        builder.EmptyOptional(impl::kTypeCategory<typename PrimitiveTrait::Type>);
    }
}

}  // namespace

template <typename PrimitiveTrait>
std::optional<typename PrimitiveTrait::Type>
OptionalPrimitiveTraits<PrimitiveTrait>::Parse(NYdb::TValueParser& parser, const ParseContext& context) {
    const bool is_optional = IsOptional(parser);
    if (is_optional) {
        parser.OpenOptional();

        if (parser.IsNull()) {
            parser.CloseOptional();
            return {};
        }
    } else {
        auto name = compiler::GetTypeName<std::optional<typename PrimitiveTrait::Type>>();
        LOG_WARNING() << "Trying to parse " << context.column_name << " as " << name
                      << " while actual type is not Optional";
    }

    auto value = PrimitiveTrait::Parse(parser);
    if (is_optional) {
        parser.CloseOptional();
    }

    return value;
}

template <typename PrimitiveTrait>
void OptionalPrimitiveTraits<PrimitiveTrait>::Write(
    NYdb::TValueBuilderBase<NYdb::TValueBuilder>& builder,
    const std::optional<typename PrimitiveTrait::Type>& value
) {
    WriteOptionalPrimitive<PrimitiveTrait>(builder, value);
}

template <typename PrimitiveTrait>
void OptionalPrimitiveTraits<PrimitiveTrait>::Write(
    NYdb::TValueBuilderBase<NYdb::TParamValueBuilder>& builder,
    const std::optional<typename PrimitiveTrait::Type>& value
) {
    WriteOptionalPrimitive<PrimitiveTrait>(builder, value);
}

template <typename PrimitiveTrait>
NYdb::TType OptionalPrimitiveTraits<PrimitiveTrait>::MakeType() {
    NYdb::TTypeBuilder builder;
    builder.BeginOptional();
    builder.Primitive(impl::kTypeCategory<typename PrimitiveTrait::Type>);
    builder.EndOptional();
    return builder.Build();
}

template <typename PrimitiveTrait>
typename PrimitiveTrait::Type
PrimitiveTraits<PrimitiveTrait>::Parse(NYdb::TValueParser& parser, const ParseContext& /*context*/) {
    const bool is_optional = IsOptional(parser);

    if (is_optional) {
        parser.OpenOptional();
    }

    // Will throw exception if value is null
    auto value = PrimitiveTrait::Parse(parser);
    if (is_optional) {
        parser.CloseOptional();
    }

    return value;
}

template <typename PrimitiveTrait>
void PrimitiveTraits<PrimitiveTrait>::Write(
    NYdb::TValueBuilderBase<NYdb::TValueBuilder>& builder,
    const typename PrimitiveTrait::Type& value
) {
    PrimitiveTrait::Write(builder, value);
}

template <typename PrimitiveTrait>
void PrimitiveTraits<PrimitiveTrait>::Write(
    NYdb::TValueBuilderBase<NYdb::TParamValueBuilder>& builder,
    const typename PrimitiveTrait::Type& value
) {
    PrimitiveTrait::Write(builder, value);
}

template <typename PrimitiveTrait>
NYdb::TType PrimitiveTraits<PrimitiveTrait>::MakeType() {
    NYdb::TTypeBuilder builder;
    builder.Primitive(impl::kTypeCategory<typename PrimitiveTrait::Type>);
    return builder.Build();
}

template struct OptionalPrimitiveTraits<BoolTrait>;
template struct PrimitiveTraits<BoolTrait>;

BoolTrait::Type BoolTrait::Parse(const NYdb::TValueParser& value_parser) { return value_parser.GetBool(); }

template <typename Builder>
void BoolTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, Type value) {
    builder.Bool(value);
}

template struct OptionalPrimitiveTraits<Int32Trait>;
template struct PrimitiveTraits<Int32Trait>;

Int32Trait::Type Int32Trait::Parse(const NYdb::TValueParser& value_parser) { return value_parser.GetInt32(); }

template <typename Builder>
void Int32Trait::Write(NYdb::TValueBuilderBase<Builder>& builder, Type value) {
    builder.Int32(value);
}

template struct OptionalPrimitiveTraits<Uint32Trait>;
template struct PrimitiveTraits<Uint32Trait>;

Uint32Trait::Type Uint32Trait::Parse(const NYdb::TValueParser& value_parser) { return value_parser.GetUint32(); }

template <typename Builder>
void Uint32Trait::Write(NYdb::TValueBuilderBase<Builder>& builder, Type value) {
    builder.Uint32(value);
}

template struct OptionalPrimitiveTraits<Int64Trait>;
template struct PrimitiveTraits<Int64Trait>;

Int64Trait::Type Int64Trait::Parse(const NYdb::TValueParser& value_parser) { return value_parser.GetInt64(); }

template <typename Builder>
void Int64Trait::Write(NYdb::TValueBuilderBase<Builder>& builder, Type value) {
    builder.Int64(value);
}

template struct OptionalPrimitiveTraits<Uint64Trait>;
template struct PrimitiveTraits<Uint64Trait>;

Uint64Trait::Type Uint64Trait::Parse(const NYdb::TValueParser& value_parser) { return value_parser.GetUint64(); }

template <typename Builder>
void Uint64Trait::Write(NYdb::TValueBuilderBase<Builder>& builder, Type value) {
    builder.Uint64(value);
}

template struct OptionalPrimitiveTraits<DoubleTrait>;
template struct PrimitiveTraits<DoubleTrait>;

DoubleTrait::Type DoubleTrait::Parse(const NYdb::TValueParser& value_parser) { return value_parser.GetDouble(); }

template <typename Builder>
void DoubleTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, Type value) {
    builder.Double(value);
}

template struct OptionalPrimitiveTraits<StringTrait>;
template struct PrimitiveTraits<StringTrait>;

StringTrait::Type StringTrait::Parse(const NYdb::TValueParser& value_parser) {
    const auto& value = value_parser.GetString();
    return std::string(value.data(), value.size());
}

template <typename Builder>
void StringTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, const Type& value) {
    builder.String(impl::ToString(value));
}

template struct OptionalPrimitiveTraits<Utf8Trait>;
template struct PrimitiveTraits<Utf8Trait>;

Utf8Trait::Type Utf8Trait::Parse(const NYdb::TValueParser& value_parser) {
    const auto& value = value_parser.GetUtf8();
    return Utf8(value.data(), value.size());
}

template <typename Builder>
void Utf8Trait::Write(NYdb::TValueBuilderBase<Builder>& builder, const Type& value) {
    builder.Utf8(impl::ToString(value.GetUnderlying()));
}

template struct OptionalPrimitiveTraits<UuidTrait>;
template struct PrimitiveTraits<UuidTrait>;

UuidTrait::Type UuidTrait::Parse(const NYdb::TValueParser& value_parser) {
    const auto& value = value_parser.GetUuid();

    // FIXME: Разобраться с порядком байт
    // boost::uuids::uuid uuid{};
    // memcpy(uuid.data, &value.Buf_.Halfs[0], sizeof(value.Buf_.Halfs[0]));
    // memcpy(uuid.data + sizeof(value.Buf_.Halfs[0]), &value.Buf_.Halfs[1], sizeof(value.Buf_.Halfs[1]));
    // return uuid;

    boost::uuids::string_generator gen;
    return gen(value.ToString());
}

template <typename Builder>
void UuidTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, const Type& value) {
    // FIXME: Разобраться с порядком байт
    // std::uint64_t low_128{0}, high_128{0};
    // memcpy(&low_128, value.data, sizeof(low_128));
    // memcpy(&high_128, value.data + sizeof(low_128), sizeof(high_128));
    // builder.Uuid(NYdb::TUuidValue(low_128, high_128));
    builder.Uuid(NYdb::TUuidValue(boost::lexical_cast<std::string>(value)));
}

template struct OptionalPrimitiveTraits<DateTrait>;
template struct PrimitiveTraits<DateTrait>;

DateTrait::Type DateTrait::Parse(const NYdb::TValueParser& value_parser) {
    return Date(std::chrono::microseconds(value_parser.GetDate().GetValue()));
}

template <typename Builder>
void DateTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, const Type& value) {
    builder.Date(TInstant::MicroSeconds(
        std::chrono::duration_cast<std::chrono::microseconds>(value.GetUnderlying().time_since_epoch()).count()
    ));
}
//
template struct OptionalPrimitiveTraits<TimestampTrait>;
template struct PrimitiveTraits<TimestampTrait>;

TimestampTrait::Type TimestampTrait::Parse(const NYdb::TValueParser& value_parser) {
    return Timestamp{std::chrono::microseconds(value_parser.GetTimestamp().GetValue())};
}

template <typename Builder>
void TimestampTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, Type value) {
    builder.Timestamp(
        TInstant::MicroSeconds(std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch()).count())
    );
}

template struct OptionalPrimitiveTraits<JsonTrait>;
template struct PrimitiveTraits<JsonTrait>;

JsonTrait::Type JsonTrait::Parse(const NYdb::TValueParser& value_parser) {
    const auto& value = value_parser.GetJson();
    return formats::json::FromString(std::string_view(value.data(), value.size()));
}

template <typename Builder>
void JsonTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, const Type& value) {
    builder.Json(formats::json::ToString(value));
}

template struct OptionalPrimitiveTraits<JsonDocumentTrait>;
template struct PrimitiveTraits<JsonDocumentTrait>;

JsonDocumentTrait::Type JsonDocumentTrait::Parse(const NYdb::TValueParser& value_parser) {
    const auto& value = value_parser.GetJsonDocument();
    return JsonDocument(formats::json::FromString(std::string_view(value.data(), value.size())));
}

template <typename Builder>
void JsonDocumentTrait::Write(NYdb::TValueBuilderBase<Builder>& builder, const Type& value) {
    builder.JsonDocument(formats::json::ToString(value.GetUnderlying()));
}

}  // namespace ydb

USERVER_NAMESPACE_END
