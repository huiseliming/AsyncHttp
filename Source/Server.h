#pragma once
#include "HttpUtils.h"
#include "Session.h"
#include <boost/algorithm/string.hpp>
#include <boost/callable_traits.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/url/parse.hpp>

namespace Http {
class CServer;

typedef std::function<void(CSession*,
                           beast::http::request<beast::http::string_body>&&,
                           const std::vector<std::string_view>&)>
    FRequestHandlerFunc;

class CRequestHandler {
    static inline void
    NotImpl(CSession* session,
            beast::http::request<beast::http::string_body>&& request,
            const std::vector<std::string_view>&) {
        session->sendResponse(InternalServerError(
            std::move(request), "The requestHandler " +
                                    std::string(request.target()) +
                                    " not implemented"));
    }
    static inline const std::vector<std::string_view> EmptyMappingParameters;

  public:
    BOOST_FORCEINLINE CRequestHandler(const std::string& path)
        : mPath(path)
        , mFunc(&NotImpl) {}
    template <typename Lambda>
    BOOST_FORCEINLINE CRequestHandler(const std::string& path, Lambda&& lambda)
        : mPath(path)
        , mFunc(std::forward<Lambda>(lambda)) {}
    virtual ~CRequestHandler() = default;
    BOOST_FORCEINLINE void
    operator()(CSession* session,
               beast::http::request<beast::http::string_body>&& request,
               const std::vector<std::string_view>& mappingParameters =
                   EmptyMappingParameters) {
        BOOST_LOG_TRIVIAL(error) << "mappedParameter BG: ";
        for (auto mappedParameter : mappingParameters) {
            BOOST_LOG_TRIVIAL(error) << "mappedParameter   : " << mappedParameter;
        }
        BOOST_LOG_TRIVIAL(error) << "mappedParameter ED: ";
        mFunc(session, std::move(request), mappingParameters);
    }
    BOOST_FORCEINLINE std::string_view path() { return mPath; }
    BOOST_FORCEINLINE const FRequestHandlerFunc& func() { return mFunc; }

  protected:
    std::string mPath;
    FRequestHandlerFunc mFunc;
    // std::function<void(beast::http::request<beast::http::buffer_body>&&)>
    // mHandler;
    // std::function<void(beast::http::request<beast::http::dynamic_body>&&)>
    // mHandler;
    // std::function<void(beast::http::request<beast::http::empty_body>&&)>
    // mHandler;
};

struct CSegmentRoutingNode
    : public std::enable_shared_from_this<CSegmentRoutingNode> {
    std::shared_ptr<CRequestHandler> mRequestHandler;
    std::shared_ptr<CSegmentRoutingNode> mMappingParameterRoutingNode;
    std::unordered_map<std::string_view, std::shared_ptr<CSegmentRoutingNode>>
        mSegmentRoutingTable;
};

struct CRoutingTable {
    void route(CSession* session,
               beast::http::request<beast::http::string_body>&& request) {
        boost::system::result<boost::urls::url_view> urlResult =
            boost::urls::parse_origin_form(request.target());
        if (urlResult.has_error()) {
            return session->sendResponse(BadRequest(
                std::move(request), "Invalid URL : " + urlResult.error().message()));
        }
        boost::urls::url_view& urlView = urlResult.value();
        {
            auto it = mFullPathRoutingTable.find(urlView.encoded_path());
            if (it != mFullPathRoutingTable.end()) {
                return (*it->second)(session, std::move(request));
            }
        }
        CRequestHandler* requestHandler = nullptr;
        std::vector<std::string_view> mappingParameters;
        CSegmentRoutingNode* segmentRoutingNode = &mSegmentRoutingTree;
        auto segmentsView = urlView.encoded_segments();
        const char* segmentPathStart = segmentsView.begin()->data();
        for (auto segmentsIt = segmentsView.begin();
             segmentsIt != segmentsView.end(); segmentsIt++) {
            std::string_view segmentPath(segmentPathStart, segmentsIt->data() +
                                                               segmentsIt->length() -
                                                               segmentPathStart);

            if (!segmentRoutingNode->mSegmentRoutingTable.empty()) {
                auto it = segmentRoutingNode->mSegmentRoutingTable.find(segmentPath);
                if (it != segmentRoutingNode->mSegmentRoutingTable.end()) {
                    segmentRoutingNode = it->second.get();
                    segmentPathStart = segmentsIt->data() + segmentsIt->length() + 1;
                }
            }
            if (segmentPathStart <= segmentsIt->data()) {
                if (segmentRoutingNode->mMappingParameterRoutingNode) {
                    mappingParameters.push_back(*segmentsIt);
                    segmentRoutingNode =
                        segmentRoutingNode->mMappingParameterRoutingNode.get();
                    segmentPathStart = segmentsIt->data() + segmentsIt->length() + 1;
                }
            }
        }
        auto lastSegment = segmentsView.back();
        if (lastSegment->data() + lastSegment->length() + 1 == segmentPathStart) {
            if (segmentRoutingNode->mRequestHandler) {
                return (*segmentRoutingNode->mRequestHandler)(
                    session, std::move(request), mappingParameters);
            }
        }
        return session->sendResponse(NotFound(
            std::move(request),
            "The resource '" + std::string(request.target()) + "' was not found."));
    }
    bool addRoute(std::shared_ptr<CRequestHandler> requestHandler) {
        std::string_view path = requestHandler->path();
        std::vector<std::string_view> segmentsView;
        boost::split(segmentsView, path[0] == '/' ? path.substr(1) : path,
                     boost::is_any_of("/"));
        CSegmentRoutingNode* segmentRoutingNode = &mSegmentRoutingTree;
        const char* segmentPathStart = segmentsView.begin()->data();
        for (auto segmentsIt = segmentsView.begin();
             segmentsIt != segmentsView.end(); segmentsIt++) {
            if (*segmentsIt->data() == '{' &&
                *(segmentsIt->data() + segmentsIt->length() - 1) == '}') {
                auto segmentPathLength = segmentsIt->data() - segmentPathStart - 1;
                if (segmentPathLength >= 0) {
                    std::string_view segmentPath(
                        segmentPathStart, segmentsIt->data() - segmentPathStart - 1);
                    auto it = segmentRoutingNode->mSegmentRoutingTable.find(segmentPath);
                    if (it == segmentRoutingNode->mSegmentRoutingTable.end()) {
                        it = segmentRoutingNode->mSegmentRoutingTable
                                 .insert(std::make_pair(
                                     segmentPath, std::make_shared<CSegmentRoutingNode>()))
                                 .first;
                    }
                    segmentRoutingNode = it->second.get();
                }
                if (!segmentRoutingNode->mMappingParameterRoutingNode) {
                    segmentRoutingNode->mMappingParameterRoutingNode =
                        std::make_shared<CSegmentRoutingNode>();
                }
                segmentRoutingNode =
                    segmentRoutingNode->mMappingParameterRoutingNode.get();
                if (segmentsIt->data() == segmentsView.back().data()) {
                    segmentRoutingNode->mRequestHandler = std::move(requestHandler);
                }
                segmentPathStart = segmentsIt->data() + segmentsIt->length() + 1;
            }
        }
        if (segmentRoutingNode == &mSegmentRoutingTree) {
            auto it = mFullPathRoutingTable.find(path);
            if (it == mFullPathRoutingTable.end()) {
                return mFullPathRoutingTable
                    .insert(std::make_pair(path, std::move(requestHandler)))
                    .second;
            }
        } else {
            if (segmentPathStart <= segmentsView.back().data()) {
                std::string_view segmentPath(segmentPathStart,
                                             segmentsView.back().data() +
                                                 segmentsView.back().length() -
                                                 segmentPathStart);
                auto it = segmentRoutingNode->mSegmentRoutingTable.find(segmentPath);
                if (it == segmentRoutingNode->mSegmentRoutingTable.end()) {
                    it = segmentRoutingNode->mSegmentRoutingTable
                             .insert(std::make_pair(
                                 segmentPath, std::make_shared<CSegmentRoutingNode>()))
                             .first;
                }
                segmentRoutingNode = it->second.get();
                segmentRoutingNode->mRequestHandler = std::move(requestHandler);
            }
        }
        return true;
    }

  protected:
    std::unordered_map<std::string_view, std::shared_ptr<CRequestHandler>>
        mFullPathRoutingTable;
    CSegmentRoutingNode mSegmentRoutingTree;
};

class CRouter {
  public:
    template <class Body, class Allocator>
    BOOST_FORCEINLINE void
    route(CSession* session,
          beast::http::request<Body, beast::http::basic_fields<Allocator>>&& request) {
        switch (request.method()) {
        case boost::beast::http::verb::delete_:
            return mDeleteRoutingTable.route(session, std::move(request));
        case boost::beast::http::verb::get:
        case boost::beast::http::verb::head:
            return mGetRoutingTable.route(session, std::move(request));
        case boost::beast::http::verb::post:
            return mPostRoutingTable.route(session, std::move(request));
        case boost::beast::http::verb::put:
            return mPutRoutingTable.route(session, std::move(request));
        case boost::beast::http::verb::options:
            return mOptionsRoutingTable.route(session, std::move(request));
        case boost::beast::http::verb::patch:
            return mPatchRoutingTable.route(session, std::move(request));
        default:
            break;
        }
        return session->sendResponse(
            BadRequest(std::move(request), "Invalid method"));
    }

    BOOST_FORCEINLINE bool
    addRoute(beast::http::verb verb,
             std::shared_ptr<CRequestHandler> requestHandler) {
        switch (verb) {
        case boost::beast::http::verb::delete_:
            return mDeleteRoutingTable.addRoute(std::move(requestHandler));
        case boost::beast::http::verb::get:
        case boost::beast::http::verb::head:
            return mGetRoutingTable.addRoute(std::move(requestHandler));
        case boost::beast::http::verb::post:
            return mPostRoutingTable.addRoute(std::move(requestHandler));
        case boost::beast::http::verb::put:
            return mPutRoutingTable.addRoute(std::move(requestHandler));
        case boost::beast::http::verb::options:
            return mOptionsRoutingTable.addRoute(std::move(requestHandler));
        case boost::beast::http::verb::patch:
            return mPatchRoutingTable.addRoute(std::move(requestHandler));
        default:
            break;
        }
        return false;
    }

    template <typename FuncType, std::size_t... Indices>
    BOOST_FORCEINLINE static void
    InvokeRequestHandler(FuncType func, CSession* session,
                         beast::http::request<beast::http::string_body>&& request,
                         const std::vector<std::string_view>& mappingParameters,
                         std::index_sequence<Indices...> indices) {
        constexpr bool kHasMappingParametersArg =
            std::is_same_v<typename std::tuple_element<
                               2, boost::callable_traits::args_t<FuncType>>::type,
                           const std::vector<std::string_view>&>;
        if constexpr (kHasMappingParametersArg) {
            func(session, std::move(request), mappingParameters,
                 boost::lexical_cast<std::decay_t<typename std::tuple_element<
                     Indices + 3, boost::callable_traits::args_t<FuncType>>::type>>(
                     mappingParameters[Indices])...);
        } else {
            func(session, std::move(request),
                 boost::lexical_cast<std::decay_t<typename std::tuple_element<
                     Indices + 2, boost::callable_traits::args_t<FuncType>>::type>>(
                     mappingParameters[Indices])...);
        }
    }

    template <typename FuncType>
    BOOST_FORCEINLINE bool addRoute(beast::http::verb verb, const char* path,
                                    FuncType&& func) {
        if constexpr (std::is_convertible_v<FuncType, FRequestHandlerFunc>) {
            return addRoute(verb, std::make_shared<CRequestHandler>(
                                      path, std::forward<FuncType>(func)));
        } else {
            static_assert(
                std::is_same_v<typename std::tuple_element<
                                   0, boost::callable_traits::args_t<FuncType>>::type,
                               CSession*>);
            static_assert(
                std::is_same_v<typename std::tuple_element<
                                   1, boost::callable_traits::args_t<FuncType>>::type,
                               beast::http::request<beast::http::string_body>&&>);
            return addRoute(
                verb,
                std::make_shared<CRequestHandler>(
                    path,
                    [func = std::move(func)](
                        CSession* session,
                        beast::http::request<beast::http::string_body>&& request,
                        const std::vector<std::string_view>& mappingParameters) {
                        constexpr bool kHasMappingParametersArg = std::is_same_v<
                            typename std::tuple_element<
                                2, boost::callable_traits::args_t<FuncType>>::type,
                            const std::vector<std::string_view>&>;
                        constexpr std::size_t kNumFuncArgs =
                            std::tuple_size_v<boost::callable_traits::args_t<FuncType>>;
                        constexpr std::size_t kNumMappingParameters =
                            kHasMappingParametersArg ? kNumFuncArgs - 3
                                                     : kNumFuncArgs - 2;
                        if (mappingParameters.size() == kNumMappingParameters) {
                            try {
                                InvokeRequestHandler(
                                    func, session, std::move(request), mappingParameters,
                                    std::make_index_sequence<kNumMappingParameters>());
                            } catch (const boost::bad_lexical_cast& badLexicalCast) {
                                session->sendResponse(Http::InternalServerError(
                                    std::move(request),
                                    std::format("Bad lexical cast: %s",
                                                badLexicalCast.what())));
                            }
                        } else {
                            session->sendResponse(Http::InternalServerError(
                                std::move(request),
                                std::format(
                                    "Mismatch in number of mapping parameters:")));
                        }
                    }));
        }
    }

  protected:
    CRoutingTable mGetRoutingTable;
    CRoutingTable mPostRoutingTable;
    CRoutingTable mPutRoutingTable;
    CRoutingTable mDeleteRoutingTable;
    CRoutingTable mOptionsRoutingTable;
    CRoutingTable mPatchRoutingTable;
};

class CListener : public std::enable_shared_from_this<CListener> {
  public:
    CListener(std::shared_ptr<CServer>&& server, Endpoint endpoint,
              std::shared_ptr<std::string const> const& docRoot);

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
    CServer(asio::ip::address address = asio::ip::address_v4::any(),
            uint16_t port = 0, std::string docRoot = ".",
            size_t numThreads = std::thread::hardware_concurrency())
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
        mCheckTimer.async_wait(
            std::bind(&CServer::onCheckTimeout, this, std::placeholders::_1));
    }

    ~CServer() {
        setEnabled(false);
        stop();
    }

    bool isEnabled() { return mEnabled; }
    bool isDisabled() { return !mEnabled; }
    void setEnabled(bool newEnabled) {
        asio::dispatch(mStand, std::bind(&CServer::onSetEnabled, shared_from_this(),
                                         newEnabled));
    }
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
    void setAddress(boost::asio::ip::address newAddress) {
        asio::dispatch(mStand, std::bind(&CServer::onSetAddress, shared_from_this(),
                                         newAddress));
    }
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
    void setPort(uint16_t newPort) {
        asio::dispatch(mStand,
                       std::bind(&CServer::onSetPort, shared_from_this(), newPort));
    }
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
    void setDocRoot(std::string newDocRoot) {
        asio::dispatch(mStand, std::bind(&CServer::onSetDocRoot, shared_from_this(),
                                         newDocRoot));
    }
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
                if (!mDocRoot.empty() && mDocRoot[0] == '.' &&
                    (mDocRoot.size() == 1 ||
                     (mDocRoot.size() >= 2 &&
                      (mDocRoot[1] == '\\' || mDocRoot[1] == '/')))) {
                    auto docRootPath = boost::dll::program_location().parent_path() /
                                       (mDocRoot.c_str() + 1);
                    docRootPtr = std::make_shared<std::string>(docRootPath.string());
                } else {
                    docRootPtr = std::make_shared<std::string>(mDocRoot);
                }
                auto listener = std::make_shared<CListener>(
                    shared_from_this(), asio::ip::tcp::endpoint{mAddress, mPort},
                    docRootPtr);
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
        mCheckTimer.async_wait(
            std::bind(&CServer::onCheckTimeout, this, std::placeholders::_1));
    }
    void stop() {
        asio::dispatch(mStand,
                       [this, self = shared_from_this()] { mIoContext.stop(); });
        for (auto& thread : mThreads) {
            thread.join();
        }
    }

    size_t numThreads() { return mThreads.size(); }
    asio::io_context& ioContext() { return mIoContext; }
    const asio::strand<asio::io_context::executor_type>& strand() {
        return mStand;
    }

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

inline Http::CListener::CListener(
    std::shared_ptr<CServer>&& server, Endpoint endpoint,
    std::shared_ptr<std::string const> const& docRoot)
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
        BOOST_LOG_TRIVIAL(error)
            << "mAcceptor.set_option(...): " << errorCode.what();
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
    asio::dispatch(mAcceptor.get_executor(),
                   beast::bind_front_handler(&CListener::doAccept,
                                             this->shared_from_this()));
}

inline void CListener::doAccept() {
    mAcceptor.async_accept(
        asio::make_strand(mServer->ioContext()),
        beast::bind_front_handler(&CListener::onAccept, shared_from_this()));
}

inline void CListener::onAccept(beast::error_code errorCode,
                                asio::ip::tcp::socket socket) {
    if (errorCode) {
        BOOST_LOG_TRIVIAL(error) << "onAccept(...): " << errorCode.what();
    } else {
        std::make_shared<CSession>(mServer, std::move(socket), mDocRoot)->run();
    }
    doAccept();
}

} // namespace Http