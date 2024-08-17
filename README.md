# triple_buffer

a header only triple buffer/pingpang buffer impl for data exchange

features:

- header only.
- lock-free.
- c++17 used.
- support both raw data block and object-oriented usage.

# model

image that there are three buffers as named: write(W), retained(N), read(R):

[W][N][R]

firtly, the producer writes data to the W continuously, swaps W and N after each write, and marks "dirty" flag to indicate the data is ready to be read.
then, the consumer check the "dirty" flag, swap R and N if the flag is set, and read the data from R buffer.

# guide

1. add `#include"tripplebuffer.hpp"` to your project ,and ...
2. two facilities are exposed: triple_buffer_basdic `<N>` is for raw block manipulation and triple_buffer `<T>` is object-oriented.
3. you can ref to [https://github.com/yaozn/triple_buffer/blob/main/example.cpp]() for all examples:

3-1. basic usage:

```c++
       net_packet pkt;
       yy::triple_buffer_basic<sizeof( net_packet )> buff;

        //-write to buffer-
       buff.update( &pkt, sizeof pkt );

       //-get the read buffer , nullptr is returned if no data is available-  
       auto ptr = buff.read_buffer();
```

for triple_buffer_basic, you can also give a manipulator function to update(...) for reading/writing in case of certain scenarios like performance optimization.

3-2. object-oriented usage:

```c++
       net_packet pkt;
       yy::triple_buffer<net_packet> buff;

       //-write to buffer-
       buff.put( pkt );

       //-get the read buffer ,false is returned if no data is available-  
       auto sucess=buff.fetch( pkt );
```

# license

BSD 3-Clause

# author

yaozn
wechat: 371536425
