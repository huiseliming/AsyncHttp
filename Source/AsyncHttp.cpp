//#include "AsyncHttp.h"
//
//#include <algorithm>
//#include <boost/asio/bind_executor.hpp>
//#include <boost/asio/dispatch.hpp>
//#include <boost/asio/signal_set.hpp>
//#include <boost/asio/strand.hpp>
//#include <boost/beast/core.hpp>
//#include <boost/beast/http.hpp>
//#include <boost/beast/version.hpp>
//#include <boost/beast/websocket.hpp>
//#include <boost/make_unique.hpp>
//#include <boost/optional.hpp>
//#include <boost/log/trivial.hpp>
//#include <cstdlib>
//#include <functional>
//#include <iostream>
//#include <memory>
//#include <queue>
//#include <string>
//#include <thread>
//#include <vector>
//
//namespace AsyncHttp {
//    using namespace boost;
//    using Acceptor = asio::ip::tcp::acceptor;
//    using IoContext = asio::io_context;
//    using Endpoint = asio::ip::tcp::endpoint;
//    using Strand = asio::strand<asio::io_context::executor_type>;
//
//    beast::string_view MimeType(beast::string_view path)
//    {
//        using beast::iequals;
//        auto const ext = [&path]
//            {
//                auto const pos = path.rfind(".");
//                if (pos == beast::string_view::npos)
//                    return beast::string_view{};
//                return path.substr(pos);
//            }();
//        if (iequals(ext, ".htm"))  return "text/html";
//        if (iequals(ext, ".html")) return "text/html";
//        if (iequals(ext, ".php"))  return "text/html";
//        if (iequals(ext, ".css"))  return "text/css";
//        if (iequals(ext, ".txt"))  return "text/plain";
//        if (iequals(ext, ".js"))   return "application/javascript";
//        if (iequals(ext, ".json")) return "application/json";
//        if (iequals(ext, ".xml"))  return "application/xml";
//        if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
//        if (iequals(ext, ".flv"))  return "video/x-flv";
//        if (iequals(ext, ".png"))  return "image/png";
//        if (iequals(ext, ".jpe"))  return "image/jpeg";
//        if (iequals(ext, ".jpeg")) return "image/jpeg";
//        if (iequals(ext, ".jpg"))  return "image/jpeg";
//        if (iequals(ext, ".gif"))  return "image/gif";
//        if (iequals(ext, ".bmp"))  return "image/bmp";
//        if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
//        if (iequals(ext, ".tiff")) return "image/tiff";
//        if (iequals(ext, ".tif"))  return "image/tiff";
//        if (iequals(ext, ".svg"))  return "image/svg+xml";
//        if (iequals(ext, ".svgz")) return "image/svg+xml";
//        return "application/text";
//    }
//
//    std::string PathCat(beast::string_view base, beast::string_view path) {
//        if (base.empty())
//            return std::string(path);
//        std::string result(base);
//#ifdef BOOST_MSVC
//        char constexpr pathSeparator = '\\';
//        if (result.back() == pathSeparator)
//            result.resize(result.size() - 1);
//        result.append(path.data(), path.size());
//        for (auto& c : result)
//            if (c == '/')
//                c = pathSeparator;
//#else
//        char constexpr pathSeparator = '/';
//        if (result.back() == pathSeparator)
//            result.resize(result.size() - 1);
//        result.append(path.data(), path.size());
//#endif
//        return result;
//    }
//
//    class CSession : public std::enable_shared_from_this<CSession> {
//        static constexpr std::size_t KQueueLimit = 8; // max responses
//    public:
//        CSession(asio::ip::tcp::socket&& socket, std::shared_ptr<std::string const> const& docRoot)
//            : mStream(std::move(socket))
//            , mDocRoot(docRoot) {
//            static_assert(KQueueLimit > 0, "queue limit must be positive");
//        }
//
//        void run() {
//            asio::dispatch(mStream.get_executor(), beast::bind_front_handler(&CSession::doRead, this->shared_from_this()));
//        }
//    protected:
//        void doRead() {
//            mRequestParser.emplace();
//            mRequestParser->body_limit(10000);
//
//            mStream.expires_after(std::chrono::seconds(30));
//
//            beast::http::async_read(
//                mStream, mFlatBuffer, *mRequestParser,
//                beast::bind_front_handler(&CSession::onRead, shared_from_this()));
//        }
//
//        void onRead(beast::error_code errorCode, std::size_t bytesTransferred) {
//            boost::ignore_unused(bytesTransferred);
//
//            if (errorCode == beast::http::error::end_of_stream)
//                return doClose();
//
//            if (errorCode) {
//                BOOST_LOG_TRIVIAL(error) << "onRead(...): " << errorCode.what();
//                return;
//            }
//
//            queueWrite(handleRequest(*mDocRoot, mRequestParser->release()));
//
//            if (mResponseQueue.size() < KQueueLimit)
//                doRead();
//        }
//
//        void queueWrite(beast::http::message_generator response) {
//            mResponseQueue.push(std::move(response));
//
//            if (mResponseQueue.size() == 1)
//                doWrite();
//        }
//
//        void doWrite() {
//            if (!mResponseQueue.empty()) {
//                bool keep_alive = mResponseQueue.front().keep_alive();
//
//                beast::async_write(mStream, std::move(mResponseQueue.front()),
//                    beast::bind_front_handler(&CSession::onWrite,
//                        shared_from_this(),
//                        keep_alive));
//            }
//        }
//
//        void onWrite(bool keep_alive, beast::error_code errorCode, std::size_t bytesTransferred) {
//            boost::ignore_unused(bytesTransferred);
//
//            if (errorCode) {
//                BOOST_LOG_TRIVIAL(error) << "onWrite(...): " << errorCode.what();
//                return;
//            }
//
//            if (!keep_alive) {
//                return doClose();
//            }
//
//            if (mResponseQueue.size() == KQueueLimit)
//                doRead();
//
//            mResponseQueue.pop();
//
//            doWrite();
//        }
//
//        void doClose() {
//            beast::error_code errorCode;
//            mStream.socket().shutdown(asio::ip::tcp::socket::shutdown_send, errorCode);
//        }
//
//
//        template <class Body, class Allocator>
//        static beast::http::message_generator handleRequest(
//               beast::string_view docRoot, 
//               beast::http::request<Body, beast::http::basic_fields<Allocator>>&& req)
//        {
//            auto const bad_request =
//                [&req](beast::string_view why)
//                {
//                    beast::http::response<beast::http::string_body> res{ beast::http::status::bad_request, req.version() };
//                    res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
//                    res.set(beast::http::field::content_type, "text/html");
//                    res.keep_alive(req.keep_alive());
//                    res.body() = std::string(why);
//                    res.prepare_payload();
//                    return res;
//                };
//
//            auto const not_found =
//                [&req](beast::string_view target)
//                {
//                    beast::http::response<beast::http::string_body> res{ beast::http::status::not_found, req.version() };
//                    res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
//                    res.set(beast::http::field::content_type, "text/html");
//                    res.keep_alive(req.keep_alive());
//                    res.body() = "The resource '" + std::string(target) + "' was not found.";
//                    res.prepare_payload();
//                    return res;
//                };
//
//            auto const server_error =
//                [&req](beast::string_view what)
//                {
//                    beast::http::response<beast::http::string_body> res{ beast::http::status::internal_server_error, req.version() };
//                    res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
//                    res.set(beast::http::field::content_type, "text/html");
//                    res.keep_alive(req.keep_alive());
//                    res.body() = "An error occurred: '" + std::string(what) + "'";
//                    res.prepare_payload();
//                    return res;
//                };
//
//            if (req.method() != beast::http::verb::get &&
//                req.method() != beast::http::verb::head)
//                return bad_request("Unknown HTTP-method");
//
//            if (req.target().empty() ||
//                req.target()[0] != '/' ||
//                req.target().find("..") != beast::string_view::npos)
//                return bad_request("Illegal request-target");
//
//            std::string path = PathCat(docRoot, req.target());
//            if (req.target().back() == '/')
//                path.append("index.html");
//
//            beast::error_code ec;
//            beast::http::file_body::value_type body;
//            body.open(path.c_str(), beast::file_mode::scan, ec);
//
//            if (ec == beast::errc::no_such_file_or_directory)
//                return not_found(req.target());
//
//            if (ec)
//                return server_error(ec.message());
//
//            auto const size = body.size();
//
//            if (req.method() == beast::http::verb::head)
//            {
//                beast::http::response<beast::http::empty_body> res{ beast::http::status::ok, req.version() };
//                res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
//                res.set(beast::http::field::content_type, MimeType(path));
//                res.content_length(size);
//                res.keep_alive(req.keep_alive());
//                return res;
//            }
//
//            beast::http::response<beast::http::file_body> res{
//                std::piecewise_construct,
//                std::make_tuple(std::move(body)),
//                std::make_tuple(beast::http::status::ok, req.version()) };
//            res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
//            res.set(beast::http::field::content_type, MimeType(path));
//            res.content_length(size);
//            res.keep_alive(req.keep_alive());
//            return res;
//        }
//
//    protected:
//        beast::tcp_stream mStream;
//        beast::flat_buffer mFlatBuffer;
//        std::shared_ptr<std::string const> mDocRoot;
//        std::queue<beast::http::message_generator> mResponseQueue;
//        boost::optional<beast::http::request_parser<beast::http::string_body>> mRequestParser;
//    };
//
//    class CListener : public std::enable_shared_from_this<CListener> {
//    public:
//        CListener(IoContext& ioContext, Endpoint endpoint, std::shared_ptr<std::string const> const& docRoot)
//            : mIoContext(ioContext)
//            , mAcceptor(asio::make_strand(ioContext))
//            , mDocRoot(docRoot){
//            beast::error_code errorCode;
//
//            mAcceptor.open(endpoint.protocol(), errorCode);
//            if (errorCode) {
//                BOOST_LOG_TRIVIAL(error) << "mAcceptor.open(...): " << errorCode.what();
//                return;
//            }
//
//            mAcceptor.set_option(asio::socket_base::reuse_address(true), errorCode);
//            if (errorCode) {
//                BOOST_LOG_TRIVIAL(error) << "mAcceptor.set_option(...): " << errorCode.what();
//                return;
//            }
//
//            mAcceptor.bind(endpoint, errorCode);
//            if (errorCode) {
//                BOOST_LOG_TRIVIAL(error) << "mAcceptor.bind(...): " << errorCode.what();
//                return;
//            }
//
//            mAcceptor.listen(asio::socket_base::max_listen_connections, errorCode);
//            if (errorCode) {
//                BOOST_LOG_TRIVIAL(error) << "mAcceptor.listen(...): " << errorCode.what();
//                return;
//            }
//        }
//
//        void run() {
//            asio::dispatch(mAcceptor.get_executor(),
//                beast::bind_front_handler(&CListener::doAccept,
//                    this->shared_from_this()));
//        }
//
//        void doAccept() {
//            mAcceptor.async_accept(
//                asio::make_strand(mIoContext),
//                beast::bind_front_handler(&CListener::onAccept, shared_from_this()));
//        }
//
//        void onAccept(beast::error_code errorCode, asio::ip::tcp::socket socket) {
//            if (errorCode) {
//                BOOST_LOG_TRIVIAL(error) << "onAccept(...): " << errorCode.what();
//            } else {
//                std::make_shared<CSession>(std::move(socket), mDocRoot)->run();
//            }
//            doAccept();
//        }
//
//    protected:
//        IoContext& mIoContext;
//        Acceptor mAcceptor;
//        std::shared_ptr<std::string const> mDocRoot;
//    };
//
//    class CServer {
//
//
//    };
//}
