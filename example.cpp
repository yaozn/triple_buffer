#include <assert.h>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <thread>

#include "triplebuffer.hpp"

namespace tb {

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
    triple_buffer() {
        for ( int i = 0; i < 3; i++ ) {
            _data[ i ] = block_allocator<BLOCK_SZ>.alloc( int slot_ );
        }
    }

    ~triple_buffer() {
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

    void update( unsigned char* data_, size_t len_ ) {
        assert( len_ <= BLOCK_SZ );
        memcpy( front_buffer(), data_, std::min( len_, BLOCK_SZ ) );
        update();
    }

    block_ptr read_buffer() const {
        if ( !is_update() ) {
            return nullptr;
        }

        swap_buffer( retained_to_read );

        auto slot = _flags.load( memory_order_consume ) & k_read_mask;
        return _data[ slot >> 2 ];
    }

    bool is_update() {
        return !!_flags.load( memory_order_consume ) & k_dirty_mask;
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

// 在传入对象的情况下,也可以把
// 把对象转成固定长度,这样可以统一成指针版本--必须是pod类型
template <typename T>
struct triple_buffer : triple_buffer_basic<sizeof( T )> {
    triple_buffer() {
        for ( int i = 0; i < 3; i++ ) {
            new ( _impl._data[ i ] ) T();
        }
    }

    // using

    // using tb_impl = triple_buffer<sizeof( T )>;
    bool fetch( T& object_ ) {
        if ( !_impl.is_update() ) {
            return false;
        }

        return true;
    }

    void put( const T& object_ ) {
    }

    // private:
    //    tb_impl _impl;
};
}  // namespace tb

// 如果是指针怎么办
int main() {
    element e;

    triple_buffer tb;

    auto sucess = tb.fetch( &e );
    tb.put( &e );

    sucess = tb.fetch( &p );
    tb.put( p,
            len );  // 这样也不是特别好,会导致多次拷贝,最好是把指针返回,用于接收数据

    return 0;
}