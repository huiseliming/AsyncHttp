
#include "Http.h"
#include "HttpMySQL.h"

#include <boost/mysql/static_results.hpp>
#include <boost/describe/class.hpp>
#include <boost/json.hpp>

using namespace boost;

template <class T, class D1 = boost::describe::describe_members<T, boost::describe::mod_public | boost::describe::mod_protected>, class D2 = boost::describe::describe_members<T, boost::describe::mod_private>, class En = std::enable_if_t<boost::mp11::mp_empty<D2>::value && !std::is_union<T>::value>>

void tag_invoke(boost::json::value_from_tag const&, boost::json::value& v, T const& t) {
    auto& obj = v.emplace_object();

    boost::mp11::mp_for_each<D1>([&](auto D) {
        obj[D.name] = boost::json::value_from(t.*D.pointer);
    });
}

template <class T>
void extract(boost::json::object const& obj, char const* name, T& value) {
    value = boost::json::value_to<T>(obj.at(name));
}

template <class T, class D1 = boost::describe::describe_members<T, boost::describe::mod_public | boost::describe::mod_protected>, class D2 = boost::describe::describe_members<T, boost::describe::mod_private>, class En = std::enable_if_t<boost::mp11::mp_empty<D2>::value && !std::is_union<T>::value>>

T tag_invoke(boost::json::value_to_tag<T> const&, boost::json::value const& v) {
    auto const& obj = v.as_object();

    T t{};

    boost::mp11::mp_for_each<D1>([&](auto D) {
        extract(obj, D.name, t.*D.pointer);
    });

    return t;
}






struct Tester {
    Tester() { 
        BOOST_LOG_TRIVIAL(error) << " Tester()"; 
    }
    ~Tester() { 
        BOOST_LOG_TRIVIAL(error) << " ~Tester()  "; 
    }
    int cc[16] = {0};
};


template <typename CompletionToken>
auto f2(int i, CompletionToken&& token) {
    return asio::async_compose<CompletionToken, void()>([=](auto&& self) mutable{
        std::thread([=, self = std::move(self)]() mutable { 
                
            std::this_thread::sleep_for(std::chrono::seconds(3));
            BOOST_LOG_TRIVIAL(error) << "co: " << i;
            self.complete();
        }).detach();
    }, token);
}

boost::asio::awaitable<void> echo(int i) {
    co_await f2(i, asio::use_awaitable);
    BOOST_LOG_TRIVIAL(error) << "echo: i" << i;
}

boost::asio::awaitable<void> Sql(MySQL::CDBConnectionPool* connectionPool) {
    MySQL::CPooledConnection pooledConnection = co_await connectionPool->asyncAllocConnection(asio::use_awaitable);
    if (pooledConnection.getConnection()) {
        boost::mysql::results result;
        co_await pooledConnection.getConnection()->async_query("SHOW DATABASES", result, asio::use_awaitable);
        BOOST_LOG_TRIVIAL(error) << " results : " << result.rows().at(0).at(0) ;
    } else {
        BOOST_LOG_TRIVIAL(error) << " not connection : ";
    }
}

struct User {
    std::uint64_t id;
    std::string wxId;
    std::optional<std::string> username;
    std::optional<std::string> password;
    std::optional<std::string> nickname;
    std::optional<std::string> phoneNumber;
    std::optional<std::string> companyName;
    std::uint64_t totalConsumption;
};
BOOST_DESCRIBE_STRUCT(User, (), (id, wxId, username, password, nickname, phoneNumber, companyName, totalConsumption))

struct ErrorWithDiagnostics {
    const char* error;
    std::string_view diagnostics;
};
BOOST_DESCRIBE_STRUCT(ErrorWithDiagnostics, (), (error, diagnostics))

class CUserController {
  public:
    CUserController(MySQL::CDBConnectionPool& connectionPool, std::shared_ptr<Http::CServer> server)
        : mConnectionPool(connectionPool)
        , mServer(server) {
        server->addRoute(beast::http::verb::get, "/bxzn_v1/user/{}", std::function<void(Http::CSession*, beast::http::request<beast::http::string_body>&&, int64_t)>(std::bind_front(&CUserController::getUser, this)));
    }

    asio::awaitable<void> asyncGetUser(std::shared_ptr<Http::CSession> session, beast::http::request<beast::http::string_body> request, int64_t id) {
        try {
            mysql::error_code errorCode;
            mysql::diagnostics diagnostics;
            MySQL::CPooledConnection pooledConnection = co_await mConnectionPool.asyncAllocConnection(asio::use_awaitable);
            if (pooledConnection.getConnection()) {
                mysql::statement statement;
                std::tie(errorCode, statement) = co_await pooledConnection.getConnection()->async_prepare_statement("SELECT * FROM user WHERE i443d = ?", diagnostics, asio::as_tuple(boost::asio::use_awaitable));
                boost::mysql::throw_on_error(errorCode, diagnostics);
                mysql::static_results<User> userResult;
                std::tie(errorCode) = co_await pooledConnection.getConnection()->async_execute(statement.bind(id), userResult, diagnostics, asio::as_tuple(boost::asio::use_awaitable));
                boost::mysql::throw_on_error(errorCode, diagnostics);
                auto row = userResult.rows();
                if (!row.empty()) {
                    auto& user = row[0];
                    session->sendResponse(Http::Ok(std::move(request), json::serialize(json::value_from(user))));
                } else {
                    session->sendResponse(Http::InternalServerError(std::move(request), "Not user"));
                }
            } else {
                session->sendResponse(Http::InternalServerError(std::move(request), "not connection"));
            }
        } catch (const mysql::error_with_diagnostics& mysqlError) {
            session->sendResponse(Http::InternalServerError(std::move(request), json::serialize(json::value_from(ErrorWithDiagnostics{.error = mysqlError.what(), .diagnostics = mysqlError.get_diagnostics().server_message()}))));
        }
    }
    void getUser(Http::CSession* session, beast::http::request<beast::http::string_body>&& request, int64_t id) {
        asio::co_spawn(mServer->ioContext(), asyncGetUser(session->shared_from_this(), std::move(request), id), asio::detached);
    }

    MySQL::CDBConnectionPool& mConnectionPool;
    std::shared_ptr<Http::CServer> mServer;
};

int main(int argc, char* argv[]) {
    try {
        std::shared_ptr<Http::CServer> server = std::make_shared<Http::CServer>(boost::asio::ip::address_v6::any(), 80);
        MySQL::CDBConnectionPool dbcp(server->ioContext(), "root", "", "user");
        CUserController userController(dbcp, server);
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
        std::this_thread::sleep_for(std::chrono::seconds(6000));
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "onWrite(...): " << e.what();
    }
    return 0;
}