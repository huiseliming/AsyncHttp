#pragma once
#include "HttpUtils.h"

namespace Http {
class CSession;

class IRequest : public std::enable_shared_from_this<IRequest> {
  public:
    IRequest(std::shared_ptr<CSession>&& session)
        : mSession(std::move(session)) {}
    virtual ~IRequest() = default;
    virtual auto method() const -> beast::http::verb = 0;
    virtual auto query() const -> beast::string_view = 0;
    virtual auto body() const -> std::string_view = 0;
    virtual auto bodyData() const -> const char* = 0;
    virtual auto bodyLength() const -> size_t = 0;
    virtual auto path() const -> beast::string_view = 0;
    virtual auto version() const -> unsigned int = 0;

    auto session() const -> const std::shared_ptr<CSession>& { return mSession; }

  protected:
    std::shared_ptr<CSession> mSession;
};

class CRequest : public IRequest {
  public:
    CRequest(std::shared_ptr<CSession>&& session, beast::http::request<beast::http::string_body>&& request)
        : IRequest(std::move(session))
        , mImpl(std::move(request)) {}
    auto method() const -> beast::http::verb override { return mImpl.method(); }
    auto query() const -> beast::string_view override { return (mImpl.target()); }
    auto body() const -> std::string_view override { return mImpl.body(); }
    auto bodyData() const -> const char* override { return mImpl.body().c_str(); }
    auto bodyLength() const -> size_t override { return mImpl.body().length(); }
    auto path() const -> beast::string_view override { return mImpl.target(); }
    auto version() const -> unsigned int override { return mImpl.version(); }

  protected:
    beast::http::request<beast::http::string_body> mImpl;
};

} // namespace Http