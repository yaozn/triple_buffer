#include <atomic>
#include <cstdlib>
#include <functional>
#include <thread>
#include <type_traits>

#include "triplebuffer.hpp"

namespace yy {

#define YIELD() std::this_thread::yield()

using block_ptr = char*;

template <size_t SZ>
struct default_block_allocator {
    static block_ptr alloc( int slot_ ) {
        return static_cast<block_ptr>( std::malloc( SZ ) );
    }

    static void free( block_, int slot_ ) { std::free( block_ ); }
};

constexpr uint32_t k_write_mask = 0xc0;
constexpr uint32_t k_read_mask  = 0x0c;
constexpr uint32_t k_dirty_mask = 0x100;
constexpr uint32_t k_init_mask  = 0x1b;

//-------------------------------------
//[write][retained][read]
template <size_t BLK_SZ,
          template <size_t> class block_allocator = default_block_allocator>
struct triple_buffer_basic {
    triple_buffer_basic() {
        for ( int i = 0; i < 3; i++ ) {
            _data[ i ] = block_allocator<BLOCK_SZ>.alloc( int slot_ );
        }
    }

    ~triple_buffer_basic() {
        for ( int i = 0; i < 3; i++ ) {
            block_allocator<BLOCK_SZ>.free( _data[ i ], i );
        }
    }

    triple_buffer_basic( const triple_buffer& )             = delete;
    triple_buffer_basic( const triple_buffer&& )            = delete;
    triple_buffer_basic& operator=( const triple_buffer& )  = delete;
    triple_buffer_basic& operator=( const triple_buffer&& ) = delete;

    block_ptr write_buffer() const {
        auto slot = _flags.load( memory_order_consume ) & k_write_mask;
        return _data[ slot >> 6 ];
    }

    bool update( std::function<bool( block_ptr block, size_t limit_len_ )> update_func_ ) {
        if ( update_func_( write_buffer(), BLOCK_SZ ) ) {
            update();
            return true;
        }

        return false;
    }

    void update( unsigned char* data_, size_t len_ ) {
        assert( len_ <= BLOCK_SZ );
        memcpy( front_buffer(), data_, std::min( len_, BLOCK_SZ ) );
        update();
    }

    bool is_update() {
        return !!_flags.load( memory_order_consume ) & k_dirty_mask;
    }

    bool read( std::function<void( block_ptr block, size_t limit_len_ )> read_func_ ) {
        auto data = read_buffer();
        if ( !data ) {
            return false;
        }

        read_func_( data, BLOCK_SZ );
        return true;
    }

    block_ptr read_buffer() const {
        if ( !is_update() ) {
            return nullptr;
        }

        swap_buffer( retained_to_read );

        auto slot = _flags.load( memory_order_consume ) & k_read_mask;
        return _data[ slot >> 2 ];
    }

private:
    void update() {
        swap_buffer( write_to_retained );
    }

    static inline uint32_t write_to_retained( unint32_t flags_ ) {
        return ( k_dirty_mask | ( ( flags_ & k_write_mask ) >> 2 ) | ( ( flags_ & 0x30 ) << 2 ) | ( flags_ & k_read_mask ) | ( flags_ & 0x03 ) );
    }

    static inline uint32_t retained_to_read( uint32_t flags_ ) {
        return ( ( flags_ & k_write_mask ) | ( ( flags_ & 0x30 ) >> 2 ) | ( ( flags_ & k_read_mask ) << 2 ) | ( flags_ & 0x03 ) );
    }

    static inline void swap_buffer( std::function<uint32_t( uint32_t )> exchanger_ ) {
        auto current = _flags.load( memory_order_consume );
        while ( !_flags.compare_exchange_weak( current, exchanger_( current ), memory_order_release, memory_order_consume ) )
            YIELD();
    }

protected:
    block_ptr _data[ 3 ];

private:
    atomic_uint flags = k_init_mask;
};

template <typename T, bool = std::is_pod<T>::value, bool = std::is_assignable<T&, const T&>::value>
struct assigner;

template <typename T>
struct assigner<T, false, false> {
    static void assign( T& dst_, const T& src_ ) {
        static_assert( false, "!!not pod and not assignable" );
    }
};

template <typename T>
struct assigner<T, std::is_pod<T>::value, true> {
    static void assign( T& dst_, const T& src_ ) {
        std::cout << "pod and assignable" << std::endl;
        dst_ = src_;
    }
};

template <typename T>
struct assigner<T, true, false> {
    static void assign( T& dst_, const T& src_ ) {
        std::cout << "pod and not assignable" << std::endl;
        memcpy( &dst_, &src_, sizeof( T ) );
    }
};

template <typename T, bool = std::is_default_constructible_v<T>>
struct triple_buffer;

template <typename T>
struct triple_buffer<T, false> : triple_buffer_basic<sizeof T> {
    static_assert( false, "!!not default constructible" );
};

template <typename T>
struct triple_buffer<T, true> : triple_buffer_basic<sizeof T> {
    using object_assigner = assigner<T>;

    triple_buffer() {
        for ( int i = 0; i < 3; i++ ) {
            new ( _impl._data[ i ] ) T();
        }
    }

    bool fetch( T& object_ ) {
        if ( !_impl.is_update() ) {
            return false;
        }

        object_assigner::assign( object_, static_cast<T*>( read_buffer() ) );
        return true;
    }

    void put( const T& object_ ) {
        object_assigner::assign( static_cast<T*>( write_buffer() ), object_ );
    }
};
}  // namespace yy

struct net_packet {
    int type;
    int seq_no;
    int data;
};

struct bad_object {
    virtual ~bad_object() {}                              // non pod
    bad_object& operator=( const bad_object& ) = delete;  // non assignable
};

struct non_default_constructible {
    non_default_constructible( int x_ )
        : x( x_ ) {}
    int x;
};

int main() {
    //! case1-will compile error
    yy::triple_buffer<bad_object>                err_buff_1;
    yy::triple_buffer<non_default_constructible> err_buff_2;

    //! case2-raw data manipulation
    yy::triple_buffer_basic<sizeof net_packet> buff;

    net_packet pkt;
    pkt.type   = 1;
    pkt.seq_no = 2;

    buff.update( &pkt, sizeof pkt );

    //! read the data enter the buffer
    assert( buff.is_update() );
    auto ptr = buff.read_buffer();
    assert( ptr != nullptr );
    assert( static_cast<net_packet*>( ptr )->type == 1 );
    assert( static_cast<net_packet*>( ptr )->seq_no == 2 );
    //! ...will set false after read
    assert( !buff.is_update() );

    //! use lambda function to store data
    pkt.type   = 2;
    pkt.seq_no = 3;

    //! lambda return false will discard the data
    buff.update( [ & ]( net_packet* ptr_, size_t limit_len_ ) {
        return false;
    } );
    assert( !buff.is_update() );

    //! lambda return true will store the data
    buff.update( [ & ]( net_packet* ptr_, size_t limit_len_ ) {
        memcpy( ptr_, &pkt, sizeof pkt );
        return true;
    } );

    //! confirm the data is stored
    ptr = buff.read_buffer();
    assert( ptr != nullptr );
    assert( static_cast<net_packet*>( ptr )->type == 2 );
    assert( static_cast<net_packet*>( ptr )->seq_no == 3 );
    assert( !buff.is_update() );

    // case3-obejct manipulation
    //! emtpy buffer
    yy::triple_buffer<net_packet> buff2;
    assert( !buff2.fetch( pkt2 ) );

    //! put pkt to buffer and fetch it to pkt2
    net_packet pkt2;
    buff2.put( pkt );
    assert( buff2.fetch( pkt2 ) );
    assert( pkt2.type == 2 );
    assert( pkt2.seq_no == 3 );
    //---
    pkt.type   = 3;
    pkt.seq_no = 4;
    buff2.put( pkt );
    assert( buff.is_update() );  // set dirty

    //! pkt with type=3 will be discarded
    pkt.type   = 4;
    pkt.seq_no = 5;
    buff2.put( pkt );
    assert( buff2.is_update() );
    assert( buff2.fetch( pkt2 ) );
    assert( pkt2.type == 4 );
    assert( pkt2.seq_no == 5 );
    assert( !buff2.is_update() );  // clean

    return 0;
}