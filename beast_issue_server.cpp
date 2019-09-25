

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <cstdio>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

//------------------------------------------------------------------------------

// Echoes back all received WebSocket messages
void
do_session(tcp::socket& socket)
{
    try
    {
        std::cout << "accept connect" << std::this_thread::get_id() << std::endl;
        // Construct the stream by moving in the socket
        websocket::stream<tcp::socket> ws{std::move(socket)};

        ws.read_message_max(50 * 1024 * 1024);

        // Set a decorator to change the Server of the handshake
        ws.set_option(websocket::stream_base::decorator(
                [](websocket::response_type& res)
                {
                    res.set(http::field::server,
                            std::string(BOOST_BEAST_VERSION_STRING) +
                            " websocket-server-sync");
                }));

        // Accept the websocket handshake
        ws.accept();

        int count = 0;
        for(;;)
        {

            ws.text(ws.got_text());
            // This buffer will hold the incoming message
            beast::flat_buffer buffer;

            // Read a message
            ws.read(buffer);

            ws.write(buffer.data());

            static bool hasSleep = false;
            ++count;

            if(count > 10)
            {
                //recv message > 10. then sleep.client will be idle timeout
                if(!hasSleep)
                {
                    hasSleep = true;
                    count = 0;
                    sleep(12);
                    std::cout << "recover" << std::this_thread::get_id() << std::endl;
                }
            }



        }
    }
    catch(beast::system_error const& se)
    {
        // This indicates that the WebsocketSession was closed
        if(se.code() != websocket::error::closed)
            std::cerr << std::this_thread::get_id() << "Boost sys Error: " << se.code().message() << "  val:" << se.code().value() << std::endl;
    }
    catch(std::exception const& e)
    {
        std::cerr << "std Error: " << e.what() << std::endl;
    }
}

//------------------------------------------------------------------------------



int main(int argc, char* argv[])
{
    try
    {
        // Check command line arguments.
        if (argc != 3)
        {
            std::cerr <<
                      "Usage: websocket-server-sync <address> <port>\n" <<
                      "Example:\n" <<
                      "    websocket-server-sync 0.0.0.0 8080\n";
            return EXIT_FAILURE;
        }
        auto const address = net::ip::make_address(argv[1]);
        auto const port = static_cast<unsigned short>(std::atoi(argv[2]));

        // The io_context is required for all I/O
        net::io_context ioc{1};

        // The acceptor receives incoming connections
        tcp::acceptor acceptor{ioc, {address, port}};
        for(;;)
        {
            // This will receive the new connection
            tcp::socket socket{ioc};

            // Block until we get a connection
            acceptor.accept(socket);

            // Launch the WebsocketSession, transferring ownership of the socket
            std::thread{std::bind(
                    &do_session,
                    std::move(socket))}.detach();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}


