
#include "Http.h"
#include "HttpMySQL.h"
using namespace boost;

template <typename FuncType, size_t I>
using ArgType = std::tuple_element<I, boost::callable_traits::args_t<FuncType>>::type;

template <typename T, std::size_t I>
std::decay_t<T> ccc(const std::vector<std::string_view>& var) {
    static_assert(!std::is_lvalue_reference_v<T> || (std::is_lvalue_reference_v<T> && std::is_const_v<std::remove_reference_t<T>>));
    if constexpr (std::is_same_v<T, std::string_view>) {
        return var[I];
    } else {
        return boost::lexical_cast<std::decay_t<T>>(var[I]);
    }
}

template <typename FuncType, std::size_t... Indices>
void fff(FuncType func, const std::vector<std::string_view>& var, int d, int e, std::index_sequence<Indices...> indices) {
    func(ccc<ArgType<FuncType, Indices>, Indices>(var)..., d, e);
}

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

template <typename CompletionToken>
auto f2(int i, CompletionToken&& token) {
    return asio::async_compose<CompletionToken, void()>([=](auto&& self) {
        std::thread([=, self = std::move(self)]() mutable { 
                
            std::this_thread::sleep_for(std::chrono::seconds(8));
            BOOST_LOG_TRIVIAL(error) << "co: " << i;
            self.complete();
        }).detach();
    }, token);
}

boost::asio::awaitable<void> echo(int i) {
    while (true) {
        co_await f2(i, asio::use_awaitable);
    }
    //for (;;) {
    //    Task task = co_await GetInt();
    //}
}

int main(int argc, char* argv[]) {

    std::string_view ccs = "dsds";
    auto aa = boost::lexical_cast<std::string>(ccs);

    std::cout << typeid(std::tuple_element<0, boost::callable_traits::args_t<decltype(main)>>::type).name() << std::endl;
    // std::cout << typeid(boost::mpl::at_c<boost::function_types::parameter_types<decltype(main)>, 0>::type).name() << std::endl;
    // boost::mpl::at_c<boost::function_types::parameter_types<decltype(main)>, 0>::type;
    fff(
        [](std::string_view a, std::string_view b, std::string_view c, int d, int e) {
            std::cout << "a: " << a << std::endl;
            std::cout << "b: " << b << std::endl;
            std::cout << "c: " << c << std::endl;
            std::cout << "d: " << d << std::endl;
            std::cout << "e: " << e << std::endl;
        },
        {
            "1",
            "2",
            "3",
        },
        4, 5, std::make_index_sequence<3>());
    fff(
        [](const int&& a, const std::string& b, const char c, int d, int e) mutable {
            std::cout << "a: " << a << std::endl;
            std::cout << "b: " << b << std::endl;
            std::cout << "c: " << c << std::endl;
            std::cout << "d: " << d << std::endl;
            std::cout << "e: " << e << std::endl;
        },
        {
            "1",
            "2",
            "3",
        },
        4, 5, std::make_index_sequence<3>());

    try {
        std::shared_ptr<Http::CServer> server = std::make_shared<Http::CServer>(boost::asio::ip::address_v4::any(), 80);
        for (size_t i = 0; i < 64; i++) {
            asio::co_spawn(server->ioContext(), echo(i), boost::asio::detached);
        }
        


        MySQL::CDBConnectionPool dbcp(server->ioContext(), "root", "");
        dbcp.asyncGetConnection([](MySQL::CPooledConnection pooledConnection) {
            boost::mysql::results results;
            pooledConnection.getConnection()->query("SHOW DATABASES", results);
            std::cout << " results : " << results.rows().at(0).at(0) << std::endl;
        });
        server->setEnabled(true);
        server->addRoute(beast::http::verb::get, "/hello/{}///{}/{}/{}/{}/{}", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, std::string a, std::string&& b, const std::string& c, int d, double&& e, const int64_t& f) {
            std::cout << "a: " << a << std::endl;
            std::cout << "b: " << b << std::endl;
            std::cout << "c: " << c << std::endl;
            std::cout << "d: " << d << std::endl;
            std::cout << "e: " << e << std::endl;
            std::cout << "f: " << f << std::endl;
            session->sendResponse(Http::InternalServerError(std::move(request), "world!"));
        });
        server->addRoute(beast::http::verb::get, "/hello/{}/world", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, std::string a) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        server->addRoute(beast::http::verb::get, "/hello/{}/world/{}", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>&) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        server->addRoute(beast::http::verb::get, "/h/e/l/l/o/{}", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>&) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        server->addRoute(beast::http::verb::get, "/h/e/l/l/o/{}/w/o/r/l/d", [=](Http::CSession* session, beast::http::request<beast::http::string_body>&& request, const std::vector<std::string_view>&, std::string a) { session->sendResponse(Http::InternalServerError(std::move(request), "world!")); });
        std::this_thread::sleep_for(std::chrono::seconds(60));
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "onWrite(...): " << e.what();
    }
    return 0;
}