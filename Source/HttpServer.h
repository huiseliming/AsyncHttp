#pragma once
#include "HttpRouter.h"

namespace Http {

class CServer;

class CListener : public std::enable_shared_from_this<CListener> {
  public:
    CListener(std::shared_ptr<CServer>&& server, Endpoint endpoint, std::shared_ptr<std::string const> const& docRoot);
    void run();
    void doAccept();
    void onAccept(beast::error_code errorCode, asio::ip::tcp::socket socket);
    Acceptor& acceptor() { return mAcceptor; }

  protected:
    std::shared_ptr<CServer> mServer;
    Acceptor mAcceptor;
    std::shared_ptr<std::string const> mDocRoot;

};

class CServer : public std::enable_shared_from_this<CServer>, public CRouter {
  public:
    CServer(asio::ip::address address = asio::ip::address_v4::any(), uint16_t port = 0, std::string docRoot = ".", size_t numThreads = std::thread::hardware_concurrency())
        : mIoContext()
        , mIoContextWork(mIoContext)
        , mStand(asio::make_strand(mIoContext))
        , mCheckTimer(mIoContext, boost::asio::chrono::seconds(1))
        , mEnabled(false)
        , mAddress(address)
        , mPort(port)
        , mDocRoot(docRoot) {
        for (size_t i = 0; i < numThreads; i++) {
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
        if (mEnabled != newEnabled) {
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
        if (mAddress != newAddress) {
            mAddress = newAddress;
            if (auto listener = mListener.lock()) {
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
        if (mPort != newPort) {
            mPort = newPort;
            if (auto listener = mListener.lock()) {
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
        if (mDocRoot != newDocRoot) {
            mDocRoot = newDocRoot;
            if (auto listener = mListener.lock()) {
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
                if (!mDocRoot.empty() && mDocRoot[0] == '.' && (mDocRoot.size() == 1 || (mDocRoot.size() >= 2 && (mDocRoot[1] == '\\' || mDocRoot[1] == '/')))) {
                    auto docRootPath = boost::dll::program_location().parent_path() / (mDocRoot.c_str() + 1);
                    docRootPtr = std::make_shared<std::string>(docRootPath.string());
                } else {
                    docRootPtr = std::make_shared<std::string>(mDocRoot);
                }
                auto listener = std::make_shared<CListener>(shared_from_this(), asio::ip::tcp::endpoint{mAddress, mPort}, docRootPtr);
                mListener = listener;
                listener->run();
            }
        }
    }
    void onCheckTimeout(const boost::system::error_code& errorCode) {
        if (errorCode.failed()) {
            BOOST_LOG_TRIVIAL(error) << "onCheckTimeout(...): " << errorCode.what();
            return;
        }
        onCheck();
        mCheckTimer.expires_at(mCheckTimer.expiry() + std::chrono::seconds(1));
        mCheckTimer.async_wait(std::bind(&CServer::onCheckTimeout, this, std::placeholders::_1));
    }
    void stop() {
        asio::dispatch(mStand, [this, self = shared_from_this()] { mIoContext.stop(); });
        for (auto& thread : mThreads) {
            thread.join();
        }
    }

    size_t numThreads() { return mThreads.size(); }
    asio::io_context& ioContext() { return mIoContext; }
    const asio::strand<asio::io_context::executor_type>& strand() { return mStand; }

  protected:
    asio::io_context mIoContext;
    asio::io_context::work mIoContextWork;
    asio::strand<asio::io_context::executor_type> mStand;
    boost::asio::steady_timer mCheckTimer;
    std::weak_ptr<CListener> mListener;
    bool mEnabled;
    std::vector<std::thread> mThreads;

    boost::asio::ip::address mAddress;
    uint16_t mPort{0};
    std::string mDocRoot;
};

inline Http::CListener::CListener(std::shared_ptr<CServer>&& server, Endpoint endpoint, std::shared_ptr<std::string const> const& docRoot)
    : mServer(std::move(server))
    , mAcceptor(mServer->strand())
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

inline void CListener::run() {
    asio::dispatch(mAcceptor.get_executor(), beast::bind_front_handler(&CListener::doAccept, this->shared_from_this()));
}

inline void CListener::doAccept() {
    mAcceptor.async_accept(asio::make_strand(mServer->ioContext()), beast::bind_front_handler(&CListener::onAccept, shared_from_this()));
}

inline void CListener::onAccept(beast::error_code errorCode, asio::ip::tcp::socket socket) {
    if (errorCode) {
        BOOST_LOG_TRIVIAL(error) << "onAccept(...): " << errorCode.what();
    } else {
        std::make_shared<CSession>(mServer, std::move(socket), mDocRoot)->run();
    }
    doAccept();
}

} // namespace Http