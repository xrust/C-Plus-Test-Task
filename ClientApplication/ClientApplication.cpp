#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <random>
#include <thread>
#include <chrono>
#include <atomic>
#include <conio.h>

using boost::asio::ip::tcp;
namespace asio = boost::asio;

class Client{
    public:
    Client( asio::io_context& io_context, const std::string& server_ip, unsigned short server_port )
        : io_context_( io_context ),
        socket_( io_context ),
        server_ip_( server_ip ),
        server_port_( server_port ),
        stop_flag_( false ),
        random_engine_( std::random_device()( ) ),
        distribution_( 0, 1023 ),
        log_file_( "client_log_" + get_timestamp() + ".txt", std::ios::app ){
        try{
            socket_.connect( tcp::endpoint( asio::ip::make_address( server_ip ), server_port ) );
            std::cout << "Connected to server " << server_ip << ":" << server_port << std::endl;
            log( "Connected to server " + server_ip + ":" + std::to_string( server_port ) );
        } catch( std::exception& e ){
            std::cerr << "Connection error: " << e.what() << std::endl;
            log( "Connection error: " + std::string( e.what() ) );
            throw;
        }
    }

    ~Client(){
        stop();
    }

    void start(){
        send_random_number();
    }

    void stop(){
        if( !stop_flag_.exchange( true ) ){
            std::cout << "Stopping client..." << std::endl;
            log( "Stopping client..." );

            boost::system::error_code ec;
            socket_.close( ec );

            log( "Client stopped" );
            std::cout << "Client stopped" << std::endl;
        }
    }

    bool is_running() const{
        return !stop_flag_;
    }

    private:
    void send_random_number(){
        if( stop_flag_ ) return;

        int random_number = distribution_( random_engine_ );
        int32_t network_number = htonl( random_number );

        std::cout << "Sending number: " << random_number << std::endl;
        log( "Sending number: " + std::to_string( random_number ) );

        asio::async_write( socket_, asio::buffer( &network_number, sizeof( network_number ) ),
                           [ this, random_number ]( boost::system::error_code ec, std::size_t length ){
            if( !ec ){
                receive_result();
            } else{
                handle_error( ec );
            }
        } );
    }

    void receive_result(){
        if( stop_flag_ ) return;

        asio::async_read( socket_, asio::buffer( &result_, sizeof( result_ ) ),
                          [ this ]( boost::system::error_code ec, std::size_t length ){
            if( !ec ){
                // Convert from network byte order to host byte order
                int32_t host_result = ntohl( result_ );
                float* result_float = reinterpret_cast< float* >( &host_result );

                std::cout << "Received mean of squares: " << *result_float << std::endl;
                log( "Received mean of squares: " + std::to_string( *result_float ) );

                // Add a small delay before sending the next number
                std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );

                // Continue the loop by sending another random number
                send_random_number();
            } else{
                handle_error( ec );
            }
        } );
    }

    void handle_error( const boost::system::error_code& ec ){
        if( ec == asio::error::eof ){
            std::cout << "Server closed connection" << std::endl;
            log( "Server closed connection" );
        } else{
            std::cout << "Error: " << ec.message() << std::endl;
            log( "Error: " + ec.message() );
        }

        stop();
    }

    std::string get_timestamp(){
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t( now );

        std::tm tm_now;
        localtime_s( &tm_now, &time_t_now );

        char buffer[ 32 ];
        std::strftime( buffer, sizeof( buffer ), "%Y%m%d_%H%M%S", &tm_now );

        return std::string( buffer );
    }

    void log( const std::string& message ){
        std::string timestamped_message = get_timestamp() + " - " + message;

        if( log_file_ ){
            log_file_ << timestamped_message << std::endl;
            log_file_.flush();
        }
    }

    asio::io_context& io_context_;
    tcp::socket socket_;
    std::string server_ip_;
    unsigned short server_port_;
    std::atomic<bool> stop_flag_;
    std::mt19937 random_engine_;
    std::uniform_int_distribution<int> distribution_;
    std::ofstream log_file_;
    int32_t result_;
};

int main( int argc, char* argv[] ){
    try{
        std::string server_ip = "127.0.0.1"; // Default to localhost
        unsigned short server_port = 8080;    // Default port

        // Process command line arguments if provided
        if( argc > 1 ) server_ip = argv[ 1 ];
        if( argc > 2 ) server_port = static_cast< unsigned short >( std::stoi( argv[ 2 ] ) );

        asio::io_context io_context;

        Client client( io_context, server_ip, server_port );

        // Start client operation
        client.start();

        // Thread for handling ESC key
        std::thread key_thread( [ & ](){
            while( client.is_running() ){
                if( _kbhit() && _getch() == 27 ){ // ESC key
                    std::cout << "ESC pressed, stopping client..." << std::endl;
                    client.stop();
                    io_context.stop();
                    break;
                }
                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            }
        } );

        // Create work guard to prevent io_context from stopping when there are no more tasks
        asio::executor_work_guard<asio::io_context::executor_type> work_guard =
            asio::make_work_guard( io_context );

        // Run the IO context
        io_context.run();

        if( key_thread.joinable() ){
            key_thread.join();
        }
    } catch( std::exception& e ){
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
