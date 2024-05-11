#pragma once
#include "HttpUtils.h"
#include "HttpSession.h"
#include <boost/algorithm/string.hpp>
#include <boost/callable_traits.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/url/parse.hpp>
#include <boost/thread/pthread/shared_mutex.hpp>

namespace Http {

using FRequestHandlerFunc = std::function<void(CSession*, beast::http::request<beast::http::string_body>&&, const std::vector<std::string_view>&)>;

class CRequestHandler {
    static inline void NotImpl(CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>&) { session->sendResponse(InternalServerError(std::move(request), "The requestHandler " + std::string(request.target()) + " not implemented")); }
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
    BOOST_FORCEINLINE void operator()(CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>& mappingParameters = EmptyMappingParameters) {
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
};

struct CSegmentRoutingNode : public std::enable_shared_from_this<CSegmentRoutingNode> {
    std::shared_ptr<CRequestHandler> mRequestHandler;
    std::shared_ptr<CSegmentRoutingNode> mMappingParameterRoutingNode;
    std::unordered_map<std::string_view, std::shared_ptr<CSegmentRoutingNode>> mSegmentRoutingTable;
};

struct CRoutingTable {
    void route(CSession* session, beast::http::request<beast::http::string_body>&& request) {
        boost::system::result<boost::urls::url_view> urlResult = boost::urls::parse_origin_form(request.target());
        if (urlResult.has_error()) {
            return session->sendResponse(BadRequest(std::move(request), "Invalid URL : " + urlResult.error().message()));
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
        std::shared_lock<std::shared_mutex> sharedLock(mSharedMutex);
        for (auto segmentsIt = segmentsView.begin(); segmentsIt != segmentsView.end(); segmentsIt++) {
            std::string_view segmentPath(segmentPathStart, segmentsIt->data() + segmentsIt->length() - segmentPathStart);
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
                    segmentRoutingNode = segmentRoutingNode->mMappingParameterRoutingNode.get();
                    segmentPathStart = segmentsIt->data() + segmentsIt->length() + 1;
                }
            }
        }
        auto lastSegment = segmentsView.back();
        if (lastSegment->data() + lastSegment->length() + 1 == segmentPathStart) {
            if (segmentRoutingNode->mRequestHandler) {
                return (*segmentRoutingNode->mRequestHandler)(session, std::move(request), mappingParameters);
            }
        }
        return session->sendResponse(NotFound(std::move(request), "The resource '" + std::string(request.target()) + "' was not found."));
    }
    bool addRoute(std::shared_ptr<CRequestHandler> requestHandler) {
        std::string_view path = requestHandler->path();
        std::vector<std::string_view> segmentsView;
        boost::split(segmentsView, path[0] == '/' ? path.substr(1) : path, boost::is_any_of("/"));
        CSegmentRoutingNode* segmentRoutingNode = &mSegmentRoutingTree;
        const char* segmentPathStart = segmentsView.begin()->data();
        std::unique_lock<std::shared_mutex> uniqueLock(mSharedMutex);
        for (auto segmentsIt = segmentsView.begin(); segmentsIt != segmentsView.end(); segmentsIt++) {
            if (*segmentsIt->data() == '{' && *(segmentsIt->data() + segmentsIt->length() - 1) == '}') {
                auto segmentPathLength = segmentsIt->data() - segmentPathStart - 1;
                if (segmentPathLength >= 0) {
                    std::string_view segmentPath(segmentPathStart, segmentsIt->data() - segmentPathStart - 1);
                    auto it = segmentRoutingNode->mSegmentRoutingTable.find(segmentPath);
                    if (it == segmentRoutingNode->mSegmentRoutingTable.end()) {
                        it = segmentRoutingNode->mSegmentRoutingTable.insert(std::make_pair(segmentPath, std::make_shared<CSegmentRoutingNode>())).first;
                    }
                    segmentRoutingNode = it->second.get();
                }
                if (!segmentRoutingNode->mMappingParameterRoutingNode) {
                    segmentRoutingNode->mMappingParameterRoutingNode = std::make_shared<CSegmentRoutingNode>();
                }
                segmentRoutingNode = segmentRoutingNode->mMappingParameterRoutingNode.get();
                if (segmentsIt->data() == segmentsView.back().data()) {
                    segmentRoutingNode->mRequestHandler = std::move(requestHandler);
                }
                segmentPathStart = segmentsIt->data() + segmentsIt->length() + 1;
            }
        }
        if (segmentRoutingNode == &mSegmentRoutingTree) {
            auto it = mFullPathRoutingTable.find(path);
            if (it == mFullPathRoutingTable.end()) {
                return mFullPathRoutingTable.insert(std::make_pair(path, std::move(requestHandler))).second;
            }
        } else {
            if (segmentPathStart <= segmentsView.back().data()) {
                std::string_view segmentPath(segmentPathStart, segmentsView.back().data() + segmentsView.back().length() - segmentPathStart);
                auto it = segmentRoutingNode->mSegmentRoutingTable.find(segmentPath);
                if (it == segmentRoutingNode->mSegmentRoutingTable.end()) {
                    it = segmentRoutingNode->mSegmentRoutingTable.insert(std::make_pair(segmentPath, std::make_shared<CSegmentRoutingNode>())).first;
                }
                segmentRoutingNode = it->second.get();
                segmentRoutingNode->mRequestHandler = std::move(requestHandler);
            }
        }
        return true;
    }

  protected:
    std::unordered_map<std::string_view, std::shared_ptr<CRequestHandler>> mFullPathRoutingTable;
    CSegmentRoutingNode mSegmentRoutingTree;
    std::shared_mutex mSharedMutex;
};

class CRouter {
  public:
    template <class Body, class Allocator>
    BOOST_FORCEINLINE void route(CSession* session, beast::http::request<Body, beast::http::basic_fields<Allocator>>&& request) {
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
        return session->sendResponse(BadRequest(std::move(request), "Invalid method"));
    }

    BOOST_FORCEINLINE bool addRoute(beast::http::verb verb, std::shared_ptr<CRequestHandler> requestHandler) {
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
    BOOST_FORCEINLINE static void InvokeRequestHandler(FuncType func, CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>& mappingParameters, std::index_sequence<Indices...> indices) {
        constexpr bool kHasMappingParametersArg = std::is_same_v<typename std::tuple_element<2, boost::callable_traits::args_t<FuncType>>::type, const std::vector<std::string_view>&>;
        if constexpr (kHasMappingParametersArg) {
            func(session, std::move(request), mappingParameters, boost::lexical_cast<std::decay_t<typename std::tuple_element<Indices + 3, boost::callable_traits::args_t<FuncType>>::type>>(mappingParameters[Indices])...);
        } else {
            func(session, std::move(request), boost::lexical_cast<std::decay_t<typename std::tuple_element<Indices + 2, boost::callable_traits::args_t<FuncType>>::type>>(mappingParameters[Indices])...);
        }
    }

    template <typename FuncType>
    BOOST_FORCEINLINE bool addRoute(beast::http::verb verb, const char* path, FuncType&& func) {
        if constexpr (std::is_convertible_v<FuncType, FRequestHandlerFunc>) {
            return addRoute(verb, std::make_shared<CRequestHandler>(path, std::forward<FuncType>(func)));
        } else {
            static_assert(std::is_same_v<typename std::tuple_element<0, boost::callable_traits::args_t<FuncType>>::type, CSession*>);
            static_assert(std::is_same_v<typename std::tuple_element<1, boost::callable_traits::args_t<FuncType>>::type, beast::http::request<beast::http::string_body>&&>);
            return addRoute(verb, std::make_shared<CRequestHandler>(path, [func = std::move(func)](CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>& mappingParameters) {
                                constexpr bool kHasMappingParametersArg = std::is_same_v<typename std::tuple_element<2, boost::callable_traits::args_t<FuncType>>::type, const std::vector<std::string_view>&>;
                                constexpr std::size_t kNumFuncArgs = std::tuple_size_v<boost::callable_traits::args_t<FuncType>>;
                                constexpr std::size_t kNumMappingParameters = kHasMappingParametersArg ? kNumFuncArgs - 3 : kNumFuncArgs - 2;
                                if (mappingParameters.size() == kNumMappingParameters) {
                                    try {
                                        InvokeRequestHandler(func, session, std::move(request), mappingParameters, std::make_index_sequence<kNumMappingParameters>());
                                    } catch (const boost::bad_lexical_cast& badLexicalCast) {
                                        session->sendResponse(Http::InternalServerError(std::move(request), std::format("Bad lexical cast: %s", badLexicalCast.what())));
                                    }
                                } else {
                                    session->sendResponse(Http::InternalServerError(std::move(request), std::format("Mismatch in number of mapping parameters:")));
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

} // namespace Http