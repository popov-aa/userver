#include <tracing/span.hpp>

#include <boost/optional.hpp>

#include <opentracing/tracer.h>

#include <tracing/tracer.hpp>
#include <tracing/tracing_variant.hpp>

namespace tracing {

namespace {

const std::string kLinkTag = "link";

}  // namespace

class Span::Impl {
 public:
  Impl(std::unique_ptr<opentracing::Span> span, TracerPtr tracer,
       const std::string& name);

  Impl(Impl&&) = default;

  ~Impl();

  TimeStorage& GetTimeStorage() {
    if (!time_storage_) time_storage_.emplace(name_);
    return time_storage_.get();
  }

  std::unique_ptr<opentracing::Span> span;
  std::shared_ptr<Tracer> tracer;
  logging::LogExtra log_extra_inheritable;

  const std::string name_;

  boost::optional<logging::LogExtra> log_extra_local;
  boost::optional<TimeStorage> time_storage_;
};

Span::Impl::Impl(std::unique_ptr<opentracing::Span> span, TracerPtr tracer,
                 const std::string& name)
    : span(std::move(span)), tracer(std::move(tracer)), name_(name) {}

Span::Impl::~Impl() {
  if (log_extra_local) {
    for (const auto& it : log_extra_local->extra_)
      span->SetTag(it.first, ToOpentracingValue(it.second.GetValue()));
  }

  for (const auto& it : log_extra_inheritable.extra_)
    span->SetTag(it.first, ToOpentracingValue(it.second.GetValue()));

  if (time_storage_) {
    for (const auto& it : time_storage_->GetLogs().extra_)
      span->SetTag(it.first, ToOpentracingValue(it.second.GetValue()));
  }
}

Span::Span(std::unique_ptr<opentracing::Span> span, TracerPtr tracer,
           const std::string& name)
    : pimpl_(std::make_unique<Impl>(std::move(span), std::move(tracer), name)) {
}

Span::Span(Span&& other) noexcept = default;

Span::~Span() = default;

Span Span::CreateChild(const std::string& name) const {
  auto span = pimpl_->tracer->CreateSpan(name, *this);
  span.pimpl_->log_extra_inheritable = pimpl_->log_extra_inheritable;
  return span;
}

ScopeTime Span::CreateScopeTime() {
  return ScopeTime(pimpl_->GetTimeStorage());
}

void Span::AddNonInheritableTag(std::string key,
                                logging::LogExtra::Value value) {
  if (!pimpl_->log_extra_local) pimpl_->log_extra_local.emplace();
  pimpl_->log_extra_local->Extend(std::move(key), std::move(value));
}

void Span::AddTag(std::string key, logging::LogExtra::Value value) {
  pimpl_->log_extra_inheritable.Extend(std::move(key), std::move(value));
}

void Span::AddTagFrozen(std::string key, logging::LogExtra::Value value) {
  pimpl_->log_extra_inheritable.Extend(std::move(key), std::move(value),
                                       logging::LogExtra::ExtendType::kFrozen);
}

void Span::SetLink(std::string link) {
  AddTagFrozen(kLinkTag, std::move(link));
}

std::string Span::GetLink() const {
  const auto& value = pimpl_->log_extra_inheritable.GetValue(kLinkTag);
  const auto s = boost::get<std::string>(&value);
  if (s)
    return *s;
  else
    return {};
}

opentracing::Span& Span::GetOpentracingSpan() { return *pimpl_->span; }

const opentracing::Span& Span::GetOpentracingSpan() const {
  return *pimpl_->span;
}

logging::LogExtra& Span::GetInheritableLogExtra() {
  return pimpl_->log_extra_inheritable;
}

void Span::LogTo(logging::LogHelper& log_helper) const {
  log_helper << pimpl_->log_extra_inheritable;
  if (pimpl_->log_extra_local) log_helper << pimpl_->log_extra_local.get();
  pimpl_->tracer->LogSpanContextTo(*pimpl_->span, log_helper);
}

}  // namespace tracing
