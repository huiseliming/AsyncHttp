#pragma once

#include <algorithm>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>
#include <boost/log/trivial.hpp>
#include <boost/bind/bind.hpp>
#include <boost/dll.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <future>

namespace AsyncHttp {
    using namespace boost;
    using Acceptor = asio::ip::tcp::acceptor;
    using IoContext = asio::io_context;
    using Endpoint = asio::ip::tcp::endpoint;

    beast::string_view MimeType(beast::string_view path);
    std::string PathCat(beast::string_view base, beast::string_view path);

    template <class Body, class Allocator>
    beast::http::response<beast::http::string_body> BadRequest(beast::http::request<Body, beast::http::basic_fields<Allocator>>& req, std::string&& body)
    {
        beast::http::response<beast::http::string_body> res{ beast::http::status::bad_request, req.version() };
        res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(body);
        res.prepare_payload();
        return res;
    }

    template <class Body, class Allocator>
    beast::http::response<beast::http::string_body> NotFound(beast::http::request<Body, beast::http::basic_fields<Allocator>>& req, std::string&& body)
    {
        beast::http::response<beast::http::string_body> res{ beast::http::status::not_found, req.version() };
        res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(body);
        res.prepare_payload();
        return res;
    }

    template <class Body, class Allocator>
    beast::http::response<beast::http::string_body> InternalServerError(beast::http::request<Body, beast::http::basic_fields<Allocator>>& req, std::string&& body)
    {
        beast::http::response<beast::http::string_body> res{ beast::http::status::internal_server_error, req.version() };
        res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::move(body);
        res.prepare_payload();
        return res;
    }

    class CSession;

    class IRequest : public std::enable_shared_from_this<IRequest> {
    public:
        virtual ~IRequest() = default;
        virtual auto method() const->beast::http::verb = 0;
        virtual auto query() const->beast::string_view = 0;
        virtual auto body() const->std::string_view = 0;
        virtual auto bodyData() const -> const char* = 0;
        virtual auto bodyLength() const->size_t = 0;
        virtual auto path() const->beast::string_view = 0;
        virtual auto version() const -> unsigned int = 0;
        virtual const std::shared_ptr<CSession>& session() const = 0;
    };

    class CRequest : public IRequest {
    public:
        CRequest(beast::http::request<beast::http::string_body> request, std::shared_ptr<CSession> session)
            : mImpl(request)
            , mSession(session){
        }
        auto method() const -> beast::http::verb override { return mImpl.method(); }
        auto query() const -> beast::string_view override { return (mImpl.target()); }
        auto body() const -> std::string_view override { return mImpl.body(); }
        auto bodyData() const -> const char* override { return mImpl.body().c_str(); }
        auto bodyLength() const -> size_t override { return mImpl.body().length(); }
        auto path() const -> beast::string_view override { return mImpl.target(); }
        auto version() const -> unsigned int override { return mImpl.version(); }
        auto session() const -> const std::shared_ptr<CSession>& override { return mSession; }

    protected:
        beast::http::request<beast::http::string_body> mImpl;
        std::shared_ptr<CSession> mSession;
    };


    class CSession : public std::enable_shared_from_this<CSession> {
        static constexpr std::size_t KQueueLimit = 8; // max responses
    public:
        CSession(asio::ip::tcp::socket&& socket, std::shared_ptr<std::string const> const& docRoot)
            : mStream(std::move(socket))
            , mDocRoot(docRoot) {
            static_assert(KQueueLimit > 0, "queue limit must be positive");
        }

        void run() {
            asio::dispatch(mStream.get_executor(), beast::bind_front_handler(&CSession::doRead, this->shared_from_this()));
        }
    protected:
        void doRead() {
            mRequestParser.emplace();
            mRequestParser->body_limit(10000);

            mStream.expires_after(std::chrono::seconds(30));

            beast::http::async_read(
                mStream, mFlatBuffer, *mRequestParser,
                beast::bind_front_handler(&CSession::onRead, shared_from_this()));
        }

        void onRead(beast::error_code errorCode, std::size_t bytesTransferred) {
            boost::ignore_unused(bytesTransferred);

            if (errorCode == beast::http::error::end_of_stream)
                return doClose();

            if (errorCode) {
                BOOST_LOG_TRIVIAL(error) << "onRead(...): " << errorCode.what();
                return;
            }
            auto cc = std::make_shared<CRequest>(mRequestParser->release(), shared_from_this());
            //queueWrite(handleRequest(*mDocRoot, mRequestParser->release()));

            if (mResponseQueue.size() < KQueueLimit)
                doRead();
        }

        void queueWrite(beast::http::message_generator response) {
            mResponseQueue.push(std::move(response));

            if (mResponseQueue.size() == 1)
                doWrite();
        }

        void doWrite() {
            if (!mResponseQueue.empty()) {
                bool keep_alive = mResponseQueue.front().keep_alive();

                beast::async_write(mStream, std::move(mResponseQueue.front()),
                    beast::bind_front_handler(&CSession::onWrite,
                        shared_from_this(),
                        keep_alive));
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
        static beast::http::message_generator handleRequest(
            beast::string_view docRoot,
            beast::http::request<Body, beast::http::basic_fields<Allocator>>&& req)
        {
            if (req.method() != beast::http::verb::get &&
                req.method() != beast::http::verb::head)
                return BadRequest(req, "Unknown HTTP-method");

            if (req.target().empty() ||
                req.target()[0] != '/' ||
                req.target().find("..") != beast::string_view::npos)
                return BadRequest(req, "Illegal request-target");

            std::string path = PathCat(docRoot, req.target());
            if (req.target().back() == '/')
                path.append("index.html");

            beast::error_code ec;
            beast::http::file_body::value_type body;
            body.open(path.c_str(), beast::file_mode::scan, ec);

            if (ec == beast::errc::no_such_file_or_directory)
                return NotFound(req, "The resource '" + std::string(req.target()) + "' was not found.");

            if (ec)
                return InternalServerError(req, "An error occurred: '" + ec.message() + "'" );

            auto const size = body.size();

            if (req.method() == beast::http::verb::head)
            {
                beast::http::response<beast::http::empty_body> res{ beast::http::status::ok, req.version() };
                res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(beast::http::field::content_type, MimeType(path));
                res.content_length(size);
                res.keep_alive(req.keep_alive());
                return res;
            }

            beast::http::response<beast::http::file_body> res{
                std::piecewise_construct,
                std::make_tuple(std::move(body)),
                std::make_tuple(beast::http::status::ok, req.version()) };
            res.set(beast::http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(beast::http::field::content_type, MimeType(path));
            res.content_length(size);
            res.keep_alive(req.keep_alive());
            return res;
        }

    protected:
        beast::tcp_stream mStream;
        beast::flat_buffer mFlatBuffer;
        std::shared_ptr<std::string const> mDocRoot;
        std::queue<beast::http::message_generator> mResponseQueue;
        boost::optional<beast::http::request_parser<beast::http::string_body>> mRequestParser;
    };

    class CListener : public std::enable_shared_from_this<CListener> {
    public:
        CListener(IoContext& ioContext, Endpoint endpoint, std::shared_ptr<std::string const> const& docRoot, asio::strand<asio::io_context::executor_type> strand)
            : mIoContext(ioContext)
            , mAcceptor(strand)
            , mDocRoot(docRoot) {
            beast::error_code errorCode;

            mAcceptor.open(endpoint.protocol(), errorCode);
            if (errorCode) {
                BOOST_LOG_TRIVIAL(error) << "mAcceptor.open(...): " << errorCode.what();
                return;
            }

            mAcceptor.set_option(asio::socket_base::reuse_address(true), errorCode);
            if (errorCode) {
                BOOST_LOG_TRIVIAL(error) << "mAcceptor.set_option(...): " << errorCode.what();
                return;
            }

            mAcceptor.bind(endpoint, errorCode);
            if (errorCode) {
                BOOST_LOG_TRIVIAL(error) << "mAcceptor.bind(...): " << errorCode.what();
                return;
            }

            mAcceptor.listen(asio::socket_base::max_listen_connections, errorCode);
            if (errorCode) {
                BOOST_LOG_TRIVIAL(error) << "mAcceptor.listen(...): " << errorCode.what();
                return;
            }
        }

        void run() {
            asio::dispatch(mAcceptor.get_executor(),
                beast::bind_front_handler(&CListener::doAccept,
                    this->shared_from_this()));
        }

        void doAccept() {
            mAcceptor.async_accept(
                asio::make_strand(mIoContext),
                beast::bind_front_handler(&CListener::onAccept, shared_from_this()));
        }

        void onAccept(beast::error_code errorCode, asio::ip::tcp::socket socket) {
            if (errorCode) {
                BOOST_LOG_TRIVIAL(error) << "onAccept(...): " << errorCode.what();
            }
            else {
                std::make_shared<CSession>(std::move(socket), mDocRoot)->run();
            }
            doAccept();
        }

        Acceptor& acceptor() { return mAcceptor; }

    protected:
        asio::io_context& mIoContext;
        Acceptor mAcceptor;
        std::shared_ptr<std::string const> mDocRoot;
    };

    class CController {
    public:
        CController() = default;
        template<typename Lambda>
        CController(Lambda lambda) :mHandler(lambda){
        
        }
    protected:
        std::function<void(std::shared_ptr<IRequest>)> mHandler;
    };



    class CRouter {
    public:
        
        bool addRoute(const char* path, std::unique_ptr<CController> controller) {
            auto it = mGetRequestRouteMap.find(path);
            if (it == mGetRequestRouteMap.end())
            {
                return mGetRequestRouteMap.insert(std::make_pair(path, std::move(controller))).second;
            } 
            return false;
        }

        template<typename Func>
        bool addRoute(const char* path, Func&& func) {
            auto it = mGetRequestRouteMap.find(path);
            if (it == mGetRequestRouteMap.end())
            {
                return mGetRequestRouteMap.insert(std::make_pair(path, std::make_unique<CController>([func = std::forward<Func>(func)](std::shared_ptr<IRequest> req) {
                    func(req);
                }))).second;
            }
            return false;
        }

    protected:
        std::unordered_map<std::string, std::unique_ptr<CController>> mGetRequestRouteMap;
    };

    class CServer : public std::enable_shared_from_this<CServer> , public CRouter {
    public:
        CServer(asio::ip::address address = asio::ip::address_v4::any(), uint16_t port = 0, std::string docRoot = ".", size_t numThreads = std::thread::hardware_concurrency())
            : mIoContext()
            , mIoContextWork(mIoContext)
            , mStand(asio::make_strand(mIoContext))
            , mCheckTimer(mIoContext, boost::asio::chrono::seconds(1))
            , mEnabled(false)
            , mAddress(address)
            , mPort(port)
            , mDocRoot(docRoot)
        {
            for (size_t i = 0; i < numThreads; i++)
            {
                mThreads.emplace_back(std::thread([=] { mIoContext.run(); }));
            }
            mCheckTimer.async_wait(std::bind(&CServer::onCheckTimeout, this, std::placeholders::_1));
        }

        ~CServer() {
            setEnabled(false);
            stop();
        }

        bool isEnabled() { return mEnabled; }
        bool isDisabled() { return !mEnabled; }
        void setEnabled(bool newEnabled) { asio::dispatch(mStand, std::bind(&CServer::onSetEnabled, shared_from_this(), newEnabled)); }
        void onSetEnabled(bool newEnabled) {
            if (mEnabled != newEnabled)
            {
                mEnabled = newEnabled;
                asio::dispatch(mStand, std::bind(&CServer::onCheck, shared_from_this()));
            }
        }

        boost::asio::ip::address address() {
            std::promise<boost::asio::ip::address> promise;
            asio::dispatch(mStand, [&] { promise.set_value(mAddress); });
            return promise.get_future().get();
        }
        void setAddress(boost::asio::ip::address newAddress) { asio::dispatch(mStand, std::bind(&CServer::onSetAddress, shared_from_this(), newAddress)); }
        void onSetAddress(boost::asio::ip::address newAddress) {
            if (mAddress != newAddress)
            {
                mAddress = newAddress;
                if (auto listener = mListener.lock())
                {
                    listener->acceptor().close();
                }
            }
        }

        uint16_t port() {
            std::promise<uint16_t> promise;
            asio::dispatch(mStand, [&] { promise.set_value(mPort); });
            return promise.get_future().get();
        }
        void setPort(uint16_t newPort) { asio::dispatch(mStand, std::bind(&CServer::onSetPort, shared_from_this(), newPort)); }
        void onSetPort(uint16_t newPort) {
            if (mPort != newPort)
            {
                mPort = newPort;
                if (auto listener = mListener.lock())
                {
                    listener->acceptor().close();
                }
            }
        }

        std::string docRoot() {
            std::promise<std::string> promise;
            asio::dispatch(mStand, [&] { promise.set_value(mDocRoot); });
            return promise.get_future().get();
        }
        void setDocRoot(std::string newDocRoot) { asio::dispatch(mStand, std::bind(&CServer::onSetDocRoot, shared_from_this(), newDocRoot)); }
        void onSetDocRoot(std::string newDocRoot) {
            if (mDocRoot != newDocRoot)
            {
                mDocRoot = newDocRoot;
                if (auto listener = mListener.lock())
                {
                    listener->acceptor().close();
                }
            }
        }

        void onCheck() {
            BOOST_LOG_TRIVIAL(error) << "onCheck(...): ";
            if (!mListener.expired()) {
                if (auto listener = mListener.lock()) {
                    if (isDisabled()) {
                        listener->acceptor().close();
                    }
                }
            } else {
                if (isEnabled()) {
                    std::shared_ptr<std::string> docRootPtr;
                    if (!mDocRoot.empty() && mDocRoot[0] == '.' && (mDocRoot.size() == 1 || (mDocRoot.size() >= 2 && (mDocRoot[1] == '\\' || mDocRoot[1] == '/'))))
                    {
                        auto docRootPath = boost::dll::program_location().parent_path() / (mDocRoot.c_str() + 1);
                        docRootPtr = std::make_shared<std::string>(docRootPath.string());
                    }
                    else
                    {
                        docRootPtr = std::make_shared<std::string>(mDocRoot);
                    }
                    auto listener = std::make_shared<AsyncHttp::CListener>(mIoContext, asio::ip::tcp::endpoint{ mAddress, mPort }, docRootPtr, mStand);
                    mListener = listener;
                    listener->run();
                }
            }
        }
        void onCheckTimeout(const boost::system::error_code& errorCode) {
            if (errorCode.failed())
            {
                BOOST_LOG_TRIVIAL(error) << "onCheckTimeout(...): " << errorCode.what();
                return;
            }
            onCheck();
            mCheckTimer.expires_at(mCheckTimer.expiry() + std::chrono::seconds(1));
            mCheckTimer.async_wait(std::bind(&CServer::onCheckTimeout, this, std::placeholders::_1));
        }
        void stop() {
            asio::dispatch(mStand, [this, self = shared_from_this()] { mIoContext.stop(); });
            for (auto& thread : mThreads)
            {
                thread.join();
            }
        }

        size_t numThreads() { return mThreads.size(); }

    protected:
        asio::io_context mIoContext;
        asio::io_context::work mIoContextWork;
        asio::strand<asio::io_context::executor_type> mStand;
        boost::asio::steady_timer mCheckTimer;
        std::weak_ptr<AsyncHttp::CListener> mListener;
        bool mEnabled;
        std::vector<std::thread> mThreads;

        boost::asio::ip::address mAddress;
        uint16_t mPort{0};
        std::string mDocRoot;

    };

}
