#pragma once
#include "HttpFwd.h"

namespace Http {
using namespace boost;
using Acceptor = asio::ip::tcp::acceptor;
using IoContext = asio::io_context;
using Endpoint = asio::ip::tcp::endpoint;

beast::string_view MimeType(beast::string_view path);
std::string PathCat(beast::string_view base, beast::string_view path);
beast::http::response<beast::http::string_body> BadRequest(beast::http::request<beast::http::string_body>&& req, std::string body);
beast::http::response<beast::http::string_body> NotFound(beast::http::request<beast::http::string_body>&& req, std::string body);
beast::http::response<beast::http::string_body> InternalServerError(beast::http::request<beast::http::string_body>&& req, std::string body);
beast::http::response<beast::http::string_body> Ok(beast::http::request<beast::http::string_body>&& req, std::string body);

// beast::http::response<beast::http::string_body> InternalServerError(unsigned int version, std::string&& body);
} // namespace Http
