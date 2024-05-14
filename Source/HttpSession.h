#pragma once
#include "HttpUtils.h"
#include "HttpMessage.h"

namespace Http {
class CServer;
class CSession : public std::enable_shared_from_this<CSession> {
    static constexpr std::size_t KQueueLimit = 8; // max responses
  public:
    CSession(std::shared_ptr<CServer>& server, asio::ip::tcp::socket&& socket, std::shared_ptr<std::string const> const& docRoot);

    void run() { asio::dispatch(mStream.get_executor(), beast::bind_front_handler(&CSession::doRead, this->shared_from_this())); }

    template <class Body, class Allocator>
    void sendResponse(beast::http::response<Body, beast::http::basic_fields<Allocator>>&& req) {
        asio::dispatch(mStream.get_executor(), beast::bind_front_handler(&CSession::queueWrite, this->shared_from_this(), std::move(req)));
    }

    void queueWrite(beast::http::message_generator response) {
        mResponseQueue.push(std::move(response));

        if (mResponseQueue.size() == 1)
            doWrite();
    }

  protected:
    void doRead() {
        mRequestParser.emplace();
        mRequestParser->body_limit(4 * 1024 * 1024);

        mStream.expires_after(std::chrono::seconds(30));

        beast::http::async_read(mStream, mFlatBuffer, *mRequestParser, beast::bind_front_handler(&CSession::onRead, shared_from_this()));
    }

    void onRead(beast::error_code errorCode, std::size_t bytesTransferred);

    void doWrite() {
        if (!mResponseQueue.empty()) {
            bool keep_alive = mResponseQueue.front().keep_alive();

            beast::async_write(mStream, std::move(mResponseQueue.front()), beast::bind_front_handler(&CSession::onWrite, shared_from_this(), keep_alive));
        }
    }

    void onWrite(bool keep_alive, beast::error_code errorCode, std::size_t bytesTransferred) {
        boost::ignore_unused(bytesTransferred);

        if (errorCode) {
            BOOST_LOG_TRIVIAL(error) << "onWrite(...): " << errorCode.what();
            return;
        }

        if (!keep_alive) {
            return doClose();
        }

        if (mResponseQueue.size() == KQueueLimit)
            doRead();

        mResponseQueue.pop();

        doWrite();
    }

    void doClose() {
        beast::error_code errorCode;
        mStream.socket().shutdown(asio::ip::tcp::socket::shutdown_send, errorCode);
    }

    template <class Body, class Allocator>
    static beast::http::message_generator handleRequest(beast::string_view docRoot, beast::http::request<Body, beast::http::basic_fields<Allocator>>&& req) {
        if (req.method() != beast::http::verb::get && req.method() != beast::http::verb::head)
            return BadRequest(std::move(req), "Unknown HTTP-method");

        if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != beast::string_view::npos)
            return BadRequest(std::move(req), "Illegal request-target");

        std::string path = PathCat(docRoot, req.target());
        if (req.target().back() == '/')
            path.append("index.html");

        beast::error_code ec;
        beast::http::file_body::value_type body;
        body.open(path.c_str(), beast::file_mode::scan, ec);

        if (ec == beast::errc::no_such_file_or_directory)
            return NotFound(std::move(req), "The resource '" + std::string(req.target()) + "' was not found.");

        if (ec)
            return InternalServerError(std::move(req), "An error occurred: '" + ec.message() + "'");

        auto const size = body.size();

        if (req.method() == beast::http::verb::head) {
            beast::http::response<beast::http::empty_body> res{beast::http::status::ok, req.version()};
            res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(beast::http::field::content_type, MimeType(path));
            res.content_length(size);
            res.keep_alive(req.keep_alive());
            return res;
        }

        beast::http::response<beast::http::file_body> res{std::piecewise_construct, std::make_tuple(std::move(body)), std::make_tuple(beast::http::status::ok, req.version())};
        res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(beast::http::field::content_type, MimeType(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return res;
    }

  protected:
    std::shared_ptr<CServer> mServer;
    beast::tcp_stream mStream;
    beast::flat_buffer mFlatBuffer;
    std::shared_ptr<std::string const> mDocRoot;
    std::queue<beast::http::message_generator> mResponseQueue;
    boost::optional<beast::http::request_parser<beast::http::string_body>> mRequestParser;
};
} // namespace Http